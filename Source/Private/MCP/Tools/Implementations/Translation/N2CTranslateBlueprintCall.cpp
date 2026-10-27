// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CTranslateBlueprintCall.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Utils/N2CPathUtils.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformTime.h"

FN2CTranslateBlueprintCall::FN2CTranslateBlueprintCall(const FN2CToolCallContextRef& InContext, const FString& InProviderId,
    const FString& InModelId, const FString& InLanguageId, const TArray<FString>& InWarnings)
    : Context(InContext)
    , ProviderId(InProviderId)
    , ModelId(InModelId)
    , LanguageId(InLanguageId)
    , Warnings(InWarnings)
{
}

FN2CTranslateBlueprintCall::~FN2CTranslateBlueprintCall()
{
    StopTicker();
    if (!bFinished.exchange(true))
    {
        // Only reachable for a call that was never started: a started call holds itself until its result is set
        Promise.SetValue(FMcpToolCallResult::CreateErrorResult(TEXT("The translation call was dropped before it started")));
    }
}

TFuture<FMcpToolCallResult> FN2CTranslateBlueprintCall::Start()
{
    // Collecting the graph, creating the provider service and sending the request create UObjects and touch editor
    // state: all of it belongs on the game thread. The HTTP completion arrives on the game thread too.
    check(IsInGameThread());
    check(!bStarted);
    bStarted = true;

    TFuture<FMcpToolCallResult> Future = Promise.GetFuture();
    SelfWhileRunning = AsShared();

    if (Context->IsCancelled())
    {
        Finish(FMcpToolCallResult::CreateErrorResult(Context->GetCancelledMessage()));
        return Future;
    }

    // Cancelling the context aborts the provider request. The context may outlive the call: hold it weakly.
    const TWeakPtr<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> WeakCall = AsShared();
    Context->OnCancelled([WeakCall]()
    {
        if (const TSharedPtr<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> Call = WeakCall.Pin())
        {
            Call->AbortRequest(Call->Context->GetCancelledMessage());
        }
    });

    StartTime = FPlatformTime::Seconds();
    Context->ReportProgress(0.02f, TEXT("Collecting the focused Blueprint"));

    // The callback holds the call weakly: a callback that arrives after the result was set touches nothing
    FOnLLMResponseReceived OnLLMResponse = FOnLLMResponseReceived::CreateLambda([WeakCall](const FString& LLMResponse)
    {
        if (const TSharedPtr<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> Call = WeakCall.Pin())
        {
            Call->HandleLLMResponse(LLMResponse);
        }
    });

    RequestHandle = RequestStarter
        ? RequestStarter(ProviderId, ModelId, LanguageId, OnLLMResponse)
        : FN2CEditorIntegration::Get().TranslateFocusedBlueprintAsync(ProviderId, ModelId, LanguageId, OnLLMResponse);

    if (bFinished.load())
    {
        // An error found before sending completed the request inside the call
        return Future;
    }
    Context->ReportProgress(ProgressAfterSend, TEXT("Translation request sent to the LLM provider"));

    // Progress and timeout from the core ticker; nothing waits on a thread
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakCall](float DeltaTime)
    {
        const TSharedPtr<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> Call = WeakCall.Pin();
        return Call.IsValid() && Call->Tick(DeltaTime);
    }), ProgressIntervalSeconds);

    return Future;
}

bool FN2CTranslateBlueprintCall::Tick(float DeltaTime)
{
    check(IsInGameThread());

    if (bFinished.load())
    {
        TickerHandle.Reset();
        return false;
    }

    if (Context->IsCancelled())
    {
        AbortRequest(Context->GetCancelledMessage());
        return false;
    }

    const double Elapsed = FPlatformTime::Seconds() - StartTime;
    if (Elapsed > TimeoutSeconds)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Translate call timed out after %.1f s; cancelling the LLM request"), Elapsed));
        AbortRequest(TEXT("LLM request timed out."));
        return false;
    }

    // A heartbeat while the provider works: clients such as Claude Code give up on a call that reports no progress for
    // several minutes. The value rises with the elapsed time (never reaching the parsing stage) and the message says
    // how long the request has been waiting.
    Context->ReportProgress(GetWaitProgress(Elapsed), FString::Printf(TEXT("Waiting for the LLM response (%d s elapsed)"),
        FMath::FloorToInt32(Elapsed)));
    return true;
}

void FN2CTranslateBlueprintCall::HandleLLMResponse(const FString& LLMResponse)
{
    if (bFinished.load())
    {
        return;
    }
    StopTicker();

    // Errors arrive as NodeToCode error JSON (a top-level "error" with the kind, HTTP status and provider message);
    // a translation is JSON with "graphs". Anything that is not a JSON object is an error too.
    TSharedPtr<FJsonObject> ResponseJson;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LLMResponse);
    const bool bIsJsonObject = FJsonSerializer::Deserialize(Reader, ResponseJson) && ResponseJson.IsValid();
    const TSharedPtr<FJsonValue> ErrorField = bIsJsonObject ? ResponseJson->TryGetField(TEXT("error")) : nullptr;
    const bool bIsError = !bIsJsonObject || (ErrorField.IsValid() && !ErrorField->IsNull());
    if (!bIsError)
    {
        Context->ReportProgress(ProgressParsing, TEXT("LLM response received; parsing the translation"));
    }
    const FString Text = WithWarnings(LLMResponse);
    FMcpToolCallResult Result = FMcpToolCallResult::CreateErrorResult(Text);
    if (!bIsError)
    {
        // structuredContent matching the tool's outputSchema, inline or as a saved file (card 0207)
        if (TOptional<FMcpToolCallResult> Success = BuildSuccessResult(Text))
        {
            Result = MoveTemp(Success.GetValue());
        }
    }
    else
    {
        // The error's kind as the stable code (RateLimited -> TRANSLATION_RATE_LIMITED); TRANSLATION_FAILED without one
        FString Kind;
        if (bIsJsonObject && ResponseJson->TryGetStringField(TEXT("error_kind"), Kind) && !Kind.IsEmpty())
        {
            FString Code = TEXT("TRANSLATION_");
            for (int32 Index = 0; Index < Kind.Len(); ++Index)
            {
                if (Index > 0 && FChar::IsUpper(Kind[Index]) && FChar::IsLower(Kind[Index - 1]))
                {
                    Code.AppendChar(TEXT('_'));
                }
                Code.AppendChar(FChar::IsAlnum(Kind[Index]) ? FChar::ToUpper(Kind[Index]) : TEXT('_'));
            }
            Result.SetErrorCode(Code);
        }
        else
        {
            Result.SetErrorCode(TEXT("TRANSLATION_FAILED"));
        }
    }

    Context->ReportProgress(1.0f, Result.bIsError ? TEXT("Translation failed") : TEXT("Translation complete"));
    Finish(Result);
}

void FN2CTranslateBlueprintCall::AbortRequest(const FString& Message)
{
    if (bFinished.load())
    {
        return;
    }
    StopTicker();

    if (RequestHandle.IsValid() && !RequestHandle->Cancel())
    {
        // The completion already ran (or is running) and set the result
        FN2CLogger::Get().LogWarning(TEXT("Translate call: the LLM request had already completed when it was cancelled"));
        if (bFinished.load())
        {
            return;
        }
    }

    Finish(FMcpToolCallResult::CreateErrorResult(Message));
}

void FN2CTranslateBlueprintCall::Finish(const FMcpToolCallResult& Result)
{
    if (bFinished.exchange(true))
    {
        return;
    }
    StopTicker();

    // Setting the value runs the caller's continuation, which may drop the last outside reference to this call: keep
    // it alive until this function returns, then let it go.
    const TSharedPtr<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> KeepAlive = MoveTemp(SelfWhileRunning);
    SelfWhileRunning.Reset();
    Promise.SetValue(Result);
}

TSharedRef<FJsonObject> FN2CTranslateBlueprintCall::BuildOutputSchema()
{
    using namespace N2CMcpOutputSchema;
    const TSharedRef<FJsonObject> Code = Object({
        { TEXT("graphDeclaration"), String(TEXT("Declaration (for C++ the header part)")) },
        { TEXT("graphImplementation"), String(TEXT("Implementation")) },
        { TEXT("implementationNotes"), String() },
    }, { TEXT("graphDeclaration"), TEXT("graphImplementation"), TEXT("implementationNotes") });
    const TSharedRef<FJsonObject> Graph = Object({
        { TEXT("graph_name"), String() },
        { TEXT("graph_type"), String() },
        { TEXT("graph_class"), String() },
        { TEXT("code"), Code },
    }, { TEXT("graph_name"), TEXT("graph_type"), TEXT("graph_class") },
        TEXT("One translated graph; code is left out when the translation was saved to a file (savedTo)"));
    const TSharedRef<FJsonObject> Usage = Object({
        { TEXT("input_tokens"), Integer() },
        { TEXT("output_tokens"), Integer() },
        { TEXT("cache_read_input_tokens"), Integer() },
        { TEXT("cache_write_input_tokens"), Integer() },
        { TEXT("reasoning_tokens"), Integer() },
    });
    return UntrustedObject({
        { TEXT("graphs"), Array(Graph) },
        { TEXT("usage"), Usage },
        { TEXT("warnings"), Array(String()) },
        { TEXT("savedTo"), String(TEXT("When the translation is larger than the result limit: the file holding all of it (read-file, or the resource_link)")) },
        { TEXT("bytes"), Integer(TEXT("Size of the saved file")) },
    }, { TEXT("graphs") }, TEXT("The N2C translation response (the LLM module's graphs and usage)"));
}

TOptional<FMcpToolCallResult> FN2CTranslateBlueprintCall::BuildSuccessResult(const FString& TranslationJson, int32 MaxInline)
{
    const TSharedPtr<FJsonObject> Translation = N2CMcpOutputSchema::ParseObject(TranslationJson);
    if (!Translation.IsValid())
    {
        return {};
    }

    // The translation was written from the Blueprint's own text, so it is untrusted data too (card 0124): the text
    // escapes closing tags, and text and structuredContent both carry the notice first
    auto Marked = [](const TSharedRef<FJsonObject>& Object)
    {
        FString Condensed;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Condensed);
        FJsonSerializer::Serialize(Object, Writer);
        FMcpToolCallResult Result = FMcpToolCallResult::CreateTextResult(FN2CMcpUntrustedContent::MarkJson(Condensed));
        Result.StructuredContent = N2CMcpOutputSchema::MarkUntrusted(Object);
        return Result;
    };

    FMcpToolCallResult Inline = Marked(Translation.ToSharedRef());
    FString InlineText;
    Inline.Content[0]->TryGetStringField(TEXT("text"), InlineText);
    if (InlineText.Len() <= MaxInline)
    {
        return Inline;
    }

    // Too large for one result: the whole translation goes to a file, the result is its summary and a link to it
    FString Path;
    FString WriteError;
    int64 Bytes = 0;
    const FString FileName = FString::Printf(TEXT("Translations/Translation__%s__%s.json"),
        *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    if (!N2CMcpBlueprintExport::WriteExport(InlineText, FileName, Path, Bytes, WriteError))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("translate-focused-blueprint: the translation (%d characters) could not be saved (%s); it is returned inline"),
            InlineText.Len(), *WriteError));
        return Inline;
    }

    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Graphs;
    const TArray<TSharedPtr<FJsonValue>>* Translated = nullptr;
    if (Translation->TryGetArrayField(TEXT("graphs"), Translated) && Translated)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Translated)
        {
            const TSharedPtr<FJsonObject>* GraphObject = nullptr;
            if (!Value.IsValid() || !Value->TryGetObject(GraphObject) || !GraphObject)
            {
                continue;
            }
            const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            for (const TCHAR* Field : { TEXT("graph_name"), TEXT("graph_type"), TEXT("graph_class") })
            {
                FString FieldValue;
                (*GraphObject)->TryGetStringField(Field, FieldValue);
                Entry->SetStringField(Field, FieldValue);
            }
            Graphs.Add(MakeShared<FJsonValueObject>(Entry));
        }
    }
    Summary->SetArrayField(TEXT("graphs"), Graphs);
    for (const TCHAR* Field : { TEXT("usage"), TEXT("warnings") })
    {
        if (const TSharedPtr<FJsonValue> Value = Translation->TryGetField(Field))
        {
            Summary->SetField(Field, Value);
        }
    }
    const FString DisplayPath = FN2CPathUtils::MakeAgentDisplayPath(Path);
    Summary->SetStringField(TEXT("savedTo"), DisplayPath);
    Summary->SetNumberField(TEXT("bytes"), static_cast<double>(Bytes));

    FMcpToolCallResult Saved = Marked(Summary);
    FString FilePath = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(FilePath);
    while (FilePath.StartsWith(TEXT("/")))
    {
        FilePath.RightChopInline(1);
    }
    Saved.Content.Add(FMcpToolCallResult::MakeResourceLink(TEXT("file:///") + FilePath, FPaths::GetCleanFilename(Path),
        TEXT("application/json"), FString::Printf(TEXT("The whole translation (%d characters, over the %d-character result limit); read it with read-file %s"),
            InlineText.Len(), MaxInline, *DisplayPath), Bytes));
    return Saved;
}

FString FN2CTranslateBlueprintCall::WithWarnings(const FString& Text) const
{
    if (Warnings.Num() == 0)
    {
        return Text;
    }
    TSharedPtr<FJsonObject> Object;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object.IsValid())
    {
        return Text;
    }
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Warning : Warnings)
    {
        Values.Add(MakeShared<FJsonValueString>(Warning));
    }
    Object->SetArrayField(TEXT("warnings"), Values);

    FString Out;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer) ? Out : Text;
}

void FN2CTranslateBlueprintCall::StopTicker()
{
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
}
