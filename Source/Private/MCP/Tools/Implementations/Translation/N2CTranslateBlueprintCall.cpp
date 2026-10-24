// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CTranslateBlueprintCall.h"
#include "Core/N2CEditorIntegration.h"
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
    const FMcpToolCallResult Result = bIsError
        ? FMcpToolCallResult::CreateErrorResult(Text)
        : FMcpToolCallResult::CreateTextResult(Text);

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
