// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CNativeBatchProcessor.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Misc/DateTime.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

namespace N2CNativeBatchProcessorPrivate
{
constexpr int32 MaxRateLimitRetries = 5;
constexpr float InitialBackoffSeconds = 1.0f;
constexpr float MaxBackoffSeconds = 60.0f;
constexpr float JitterFraction = 0.20f;
constexpr float PollDelaySeconds = 10.0f;
constexpr int32 MaxDiagnosticCharacters = 2000;
constexpr int32 MaxStructuredErrors = 8;

FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
{
    if (!Object.IsValid())
    {
        return TEXT("{}");
    }

    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Output;
}

TSharedPtr<FJsonObject> ParseObject(const FString& Json)
{
    TSharedPtr<FJsonObject> Object;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Object))
    {
        return nullptr;
    }
    return Object;
}

FString TruncateDiagnostic(const FString& Text)
{
    FString Diagnostic = Text.TrimStartAndEnd();
    if (Diagnostic.Len() > MaxDiagnosticCharacters)
    {
        Diagnostic = Diagnostic.Left(MaxDiagnosticCharacters) + TEXT("...");
    }
    return Diagnostic;
}

FString DescribeErrorFields(const TSharedPtr<FJsonObject>& ErrorObject)
{
    if (!ErrorObject.IsValid())
    {
        return FString();
    }

    TArray<FString> Parts;
    FString Value;
    if (ErrorObject->TryGetStringField(TEXT("type"), Value) && !Value.IsEmpty())
    {
        Parts.Add(FString::Printf(TEXT("type=%s"), *Value));
    }

    if (ErrorObject->TryGetStringField(TEXT("code"), Value) && !Value.IsEmpty())
    {
        Parts.Add(FString::Printf(TEXT("code=%s"), *Value));
    }
    else
    {
        double NumericCode = 0.0;
        if (ErrorObject->TryGetNumberField(TEXT("code"), NumericCode))
        {
            Parts.Add(FString::Printf(TEXT("code=%.0f"), NumericCode));
        }
    }

    if (ErrorObject->TryGetStringField(TEXT("status"), Value) && !Value.IsEmpty())
    {
        Parts.Add(FString::Printf(TEXT("status=%s"), *Value));
    }

    if (ErrorObject->TryGetStringField(TEXT("message"), Value) && !Value.IsEmpty())
    {
        Parts.Add(FString::Printf(TEXT("message=%s"), *Value));
    }

    if (ErrorObject->TryGetStringField(TEXT("param"), Value) && !Value.IsEmpty())
    {
        Parts.Add(FString::Printf(TEXT("param=%s"), *Value));
    }

    double Line = 0.0;
    if (ErrorObject->TryGetNumberField(TEXT("line"), Line))
    {
        Parts.Add(FString::Printf(TEXT("line=%.0f"), Line));
    }

    return Parts.IsEmpty()
        ? FString()
        : TruncateDiagnostic(FString::Join(Parts, TEXT(", ")));
}

FString DescribeProviderErrorObject(const TSharedPtr<FJsonObject>& Root)
{
    if (!Root.IsValid())
    {
        return FString();
    }

    const TSharedPtr<FJsonObject>* Error = nullptr;
    if (Root->TryGetObjectField(TEXT("error"), Error) && Error && Error->IsValid())
    {
        return DescribeErrorFields(*Error);
    }

    const TSharedPtr<FJsonObject>* Response = nullptr;
    if (Root->TryGetObjectField(TEXT("response"), Response) && Response && Response->IsValid())
    {
        const TSharedPtr<FJsonObject>* ResponseError = nullptr;
        if ((*Response)->TryGetObjectField(TEXT("error"), ResponseError) &&
            ResponseError && ResponseError->IsValid())
        {
            return DescribeErrorFields(*ResponseError);
        }
    }

    return FString();
}

FString DescribeHttpFailure(const FString& Body)
{
    const TSharedPtr<FJsonObject> Root = ParseObject(Body);
    const FString Structured = DescribeProviderErrorObject(Root);
    if (!Structured.IsEmpty())
    {
        return Structured;
    }

    const FString Raw = TruncateDiagnostic(Body);
    return Raw.IsEmpty() ? TEXT("no response body") : Raw;
}

FString DescribeOpenAIBatchErrors(const TSharedPtr<FJsonObject>& Batch)
{
    if (!Batch.IsValid())
    {
        return FString();
    }

    const TSharedPtr<FJsonObject>* Errors = nullptr;
    if (!Batch->TryGetObjectField(TEXT("errors"), Errors) || !Errors || !Errors->IsValid())
    {
        return FString();
    }

    const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
    if (!(*Errors)->TryGetArrayField(TEXT("data"), Data) || !Data)
    {
        return DescribeErrorFields(*Errors);
    }

    TArray<FString> Details;
    const int32 Count = FMath::Min(Data->Num(), MaxStructuredErrors);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const TSharedPtr<FJsonObject> Error = (*Data)[Index].IsValid()
            ? (*Data)[Index]->AsObject()
            : nullptr;
        const FString Detail = DescribeErrorFields(Error);
        if (!Detail.IsEmpty())
        {
            Details.Add(FString::Printf(TEXT("[%d] %s"), Index + 1, *Detail));
        }
    }

    if (Data->Num() > MaxStructuredErrors)
    {
        Details.Add(FString::Printf(
            TEXT("... %d additional error(s) omitted"),
            Data->Num() - MaxStructuredErrors));
    }

    return Details.IsEmpty()
        ? DescribeErrorFields(*Errors)
        : TruncateDiagnostic(FString::Join(Details, TEXT(" | ")));
}

FString MakeErrorResponse(const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("message"), Message);
    Error->SetStringField(TEXT("type"), TEXT("batch_error"));

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("error"), Error);
    return SerializeObject(Root);
}

bool IsSuccessCode(int32 StatusCode)
{
    return StatusCode >= 200 && StatusCode < 300;
}

float AddPositiveJitter(float Seconds)
{
    const float UnitJitter = static_cast<float>(FPlatformTime::Cycles64() % 1000ULL) / 1000.0f;
    return Seconds + Seconds * JitterFraction * UnitJitter;
}

float RetryDelay(const FHttpResponsePtr& Response, int32 RetryCount)
{
    if (Response.IsValid())
    {
        const FString RetryAfter = Response->GetHeader(TEXT("Retry-After")).TrimStartAndEnd();
        if (!RetryAfter.IsEmpty())
        {
            const float NumericSeconds = FCString::Atof(*RetryAfter);
            if (NumericSeconds > 0.0f)
            {
                return AddPositiveJitter(FMath::Clamp(NumericSeconds, 0.1f, 300.0f));
            }

            FDateTime RetryDate;
            if (FDateTime::ParseHttpDate(RetryAfter, RetryDate))
            {
                const double Seconds = (RetryDate - FDateTime::UtcNow()).GetTotalSeconds();
                if (Seconds > 0.0)
                {
                    return AddPositiveJitter(FMath::Clamp(static_cast<float>(Seconds), 0.1f, 300.0f));
                }
            }
        }

        const float RetryAfterMs = FCString::Atof(*Response->GetHeader(TEXT("retry-after-ms")));
        if (RetryAfterMs > 0.0f)
        {
            return AddPositiveJitter(FMath::Clamp(RetryAfterMs / 1000.0f, 0.1f, 300.0f));
        }
    }

    return AddPositiveJitter(FMath::Min(
        InitialBackoffSeconds * FMath::Pow(2.0f, static_cast<float>(RetryCount)),
        MaxBackoffSeconds));
}

TSharedPtr<FJsonObject> WrapErrorObject(const TSharedPtr<FJsonObject>& ErrorObject)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    if (ErrorObject.IsValid())
    {
        Root->SetObjectField(TEXT("error"), ErrorObject);
    }
    return Root;
}
}

bool FN2CNativeBatchProcessor::SupportsProvider(EN2CLLMProvider Provider)
{
    return Provider == EN2CLLMProvider::OpenAI ||
           Provider == EN2CLLMProvider::Anthropic ||
           Provider == EN2CLLMProvider::Gemini;
}

TSharedRef<FN2CNativeBatchProcessor> FN2CNativeBatchProcessor::Create(
    const FN2CLLMConfig& Config,
    TArray<FN2CNativeBatchRequest> Requests,
    FOnItemComplete OnItemComplete,
    FOnBatchComplete OnBatchComplete,
    FOnBatchUnavailable OnBatchUnavailable)
{
    return MakeShareable(new FN2CNativeBatchProcessor(
        Config,
        MoveTemp(Requests),
        MoveTemp(OnItemComplete),
        MoveTemp(OnBatchComplete),
        MoveTemp(OnBatchUnavailable)));
}

FN2CNativeBatchProcessor::FN2CNativeBatchProcessor(
    const FN2CLLMConfig& InConfig,
    TArray<FN2CNativeBatchRequest> InRequests,
    FOnItemComplete InOnItemComplete,
    FOnBatchComplete InOnBatchComplete,
    FOnBatchUnavailable InOnBatchUnavailable)
    : Config(InConfig)
    , Requests(MoveTemp(InRequests))
    , OnItemComplete(MoveTemp(InOnItemComplete))
    , OnBatchComplete(MoveTemp(InOnBatchComplete))
    , OnBatchUnavailable(MoveTemp(InOnBatchUnavailable))
{
}

void FN2CNativeBatchProcessor::Start()
{
    if (Requests.Num() < 2 || !SupportsProvider(Config.Provider))
    {
        FallbackBeforeStart(TEXT("Native batch API is not applicable to this request set"));
        return;
    }

    switch (Config.Provider)
    {
        case EN2CLLMProvider::OpenAI:
            StartOpenAIBatch();
            break;
        case EN2CLLMProvider::Anthropic:
            StartAnthropicBatch();
            break;
        case EN2CLLMProvider::Gemini:
            StartGeminiBatch();
            break;
        default:
            FallbackBeforeStart(TEXT("Provider does not expose a supported native batch API"));
            break;
    }
}

FString FN2CNativeBatchProcessor::MakeCustomId(int32 RequestId) const
{
    return FString::Printf(TEXT("n2c-%d"), RequestId);
}

const FN2CNativeBatchRequest* FN2CNativeBatchProcessor::FindRequestByCustomId(const FString& CustomId) const
{
    return Requests.FindByPredicate(
        [this, &CustomId](const FN2CNativeBatchRequest& Request)
        {
            return MakeCustomId(Request.RequestId) == CustomId;
        });
}

void FN2CNativeBatchProcessor::SendHttp(
    const FString& Verb,
    const FString& Url,
    const TMap<FString, FString>& Headers,
    const FString& Body,
    const FString& ContentType,
    TFunction<void(int32, const FString&, const FHttpResponsePtr&)> OnComplete,
    int32 RetryCount)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(Verb);
    if (!ContentType.IsEmpty())
    {
        Request->SetHeader(TEXT("Content-Type"), ContentType);
    }
    for (const TPair<FString, FString>& Header : Headers)
    {
        Request->SetHeader(Header.Key, Header.Value);
    }
    if (!Body.IsEmpty())
    {
        Request->SetContentAsString(Body);
    }
    Request->SetTimeout(Config.TimeoutSeconds);
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
    Request->SetActivityTimeout(Config.TimeoutSeconds);
#endif

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    Request->OnProcessRequestComplete().BindLambda(
        [Self, Verb, Url, Headers, Body, ContentType, OnComplete, RetryCount](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bWasSuccessful) mutable
        {
            const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
            const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();

            if (bWasSuccessful && StatusCode == 429 &&
                RetryCount < N2CNativeBatchProcessorPrivate::MaxRateLimitRetries)
            {
                const float Delay = N2CNativeBatchProcessorPrivate::RetryDelay(Response, RetryCount);
                const FString Diagnostic =
                    N2CNativeBatchProcessorPrivate::DescribeHttpFailure(ResponseBody);
                FN2CLogger::Get().LogWarning(
                    FString::Printf(
                        TEXT("Native batch HTTP 429 received. Retrying in %.2f seconds (retry %d/%d). Provider response: %s"),
                        Delay,
                        RetryCount + 1,
                        N2CNativeBatchProcessorPrivate::MaxRateLimitRetries,
                        *Diagnostic),
                    TEXT("NativeBatch"));

                FTSTicker::GetCoreTicker().AddTicker(
                    TEXT("NodeToCode.NativeBatch429"),
                    Delay,
                    [Self, Verb, Url, Headers, Body, ContentType,
                     OnComplete = MoveTemp(OnComplete), RetryCount](float) mutable
                    {
                        Self->SendHttp(
                            Verb,
                            Url,
                            Headers,
                            Body,
                            ContentType,
                            MoveTemp(OnComplete),
                            RetryCount + 1);
                        return false;
                    });
                return;
            }

            OnComplete(StatusCode, ResponseBody, Response);
        });

    if (!Request->ProcessRequest())
    {
        OnComplete(0, FString(), nullptr);
    }
}

void FN2CNativeBatchProcessor::SchedulePoll(TFunction<void()> PollFunction, float DelaySeconds)
{
    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    FTSTicker::GetCoreTicker().AddTicker(
        TEXT("NodeToCode.NativeBatchPoll"),
        DelaySeconds,
        [Self, PollFunction = MoveTemp(PollFunction)](float) mutable
        {
            PollFunction();
            return false;
        });
}

void FN2CNativeBatchProcessor::FallbackBeforeStart(const FString& Reason)
{
    if (bFinished)
    {
        return;
    }
    bFinished = true;
    FN2CLogger::Get().LogWarning(
        FString::Printf(TEXT("Native batch unavailable; falling back to ordinary requests: %s"), *Reason),
        TEXT("NativeBatch"));
    if (OnBatchUnavailable)
    {
        OnBatchUnavailable(Reason);
    }
}

void FN2CNativeBatchProcessor::CompleteItemByCustomId(
    const FString& CustomId,
    const FString& RawResponse)
{
    const FN2CNativeBatchRequest* Request = FindRequestByCustomId(CustomId);
    if (!Request || CompletedRequestIds.Contains(Request->RequestId))
    {
        return;
    }

    const FString ProviderError = N2CNativeBatchProcessorPrivate::DescribeProviderErrorObject(
        N2CNativeBatchProcessorPrivate::ParseObject(RawResponse));
    if (!ProviderError.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(
                TEXT("Native batch item %s (%s, model '%s') failed: %s"),
                *CustomId,
                *Request->RequestLabel,
                *Config.Model,
                *ProviderError),
            TEXT("NativeBatch"));
    }

    CompletedRequestIds.Add(Request->RequestId);
    if (OnItemComplete)
    {
        FN2CNativeBatchResult Result;
        Result.RequestId = Request->RequestId;
        Result.RequestLabel = Request->RequestLabel;
        Result.FormattedPayload = Request->FormattedPayload;
        Result.RawResponse = RawResponse;
        OnItemComplete(Result);
    }
}

void FN2CNativeBatchProcessor::CompleteItemByIndex(
    int32 RequestIndex,
    const FString& RawResponse)
{
    if (Requests.IsValidIndex(RequestIndex))
    {
        CompleteItemByCustomId(MakeCustomId(Requests[RequestIndex].RequestId), RawResponse);
    }
}

void FN2CNativeBatchProcessor::CompleteMissingItems(const FString& ErrorMessage)
{
    FN2CLogger::Get().LogError(
        FString::Printf(TEXT("Native batch failure for model '%s': %s"), *Config.Model, *ErrorMessage),
        TEXT("NativeBatch"));

    const FString ErrorResponse = N2CNativeBatchProcessorPrivate::MakeErrorResponse(ErrorMessage);
    for (const FN2CNativeBatchRequest& Request : Requests)
    {
        if (!CompletedRequestIds.Contains(Request.RequestId))
        {
            CompleteItemByCustomId(MakeCustomId(Request.RequestId), ErrorResponse);
        }
    }
}

void FN2CNativeBatchProcessor::FinishBatch()
{
    if (bFinished)
    {
        return;
    }
    bFinished = true;
    if (OnBatchComplete)
    {
        OnBatchComplete();
    }
}

void FN2CNativeBatchProcessor::StartOpenAIBatch()
{
    FString JsonLines;
    for (const FN2CNativeBatchRequest& Request : Requests)
    {
        const TSharedPtr<FJsonObject> BodyObject =
            N2CNativeBatchProcessorPrivate::ParseObject(Request.FormattedPayload);
        if (!BodyObject.IsValid())
        {
            FallbackBeforeStart(TEXT("Could not parse an OpenAI request body for batch submission"));
            return;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("custom_id"), MakeCustomId(Request.RequestId));
        Entry->SetStringField(TEXT("method"), TEXT("POST"));
        Entry->SetStringField(TEXT("url"), TEXT("/v1/chat/completions"));
        Entry->SetObjectField(TEXT("body"), BodyObject);
        JsonLines += N2CNativeBatchProcessorPrivate::SerializeObject(Entry) + TEXT("\n");
    }

    const FString Boundary = FString::Printf(TEXT("----NodeToCodeBatch%llu"), FPlatformTime::Cycles64());
    FString Multipart;
    Multipart += FString::Printf(TEXT("--%s\r\n"), *Boundary);
    Multipart += TEXT("Content-Disposition: form-data; name=\"purpose\"\r\n\r\nbatch\r\n");
    Multipart += FString::Printf(TEXT("--%s\r\n"), *Boundary);
    Multipart += TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"node_to_code_batch.jsonl\"\r\n");
    Multipart += TEXT("Content-Type: application/jsonl\r\n\r\n");
    Multipart += JsonLines;
    Multipart += TEXT("\r\n");
    Multipart += FString::Printf(TEXT("--%s--\r\n"), *Boundary);

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("POST"),
        TEXT("https://api.openai.com/v1/files"),
        Headers,
        Multipart,
        FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary),
        [Self, Headers](int32 Status, const FString& Body, const FHttpResponsePtr&) mutable
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->FallbackBeforeStart(FString::Printf(
                    TEXT("OpenAI batch input upload failed (HTTP %d): %s"),
                    Status,
                    *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(Body)));
                return;
            }

            const TSharedPtr<FJsonObject> Upload = N2CNativeBatchProcessorPrivate::ParseObject(Body);
            FString FileId;
            if (!Upload.IsValid() || !Upload->TryGetStringField(TEXT("id"), FileId) || FileId.IsEmpty())
            {
                Self->FallbackBeforeStart(FString::Printf(
                    TEXT("OpenAI batch input upload did not return a file id. Response: %s"),
                    *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(Body)));
                return;
            }

            TSharedPtr<FJsonObject> Create = MakeShared<FJsonObject>();
            Create->SetStringField(TEXT("input_file_id"), FileId);
            Create->SetStringField(TEXT("endpoint"), TEXT("/v1/chat/completions"));
            Create->SetStringField(TEXT("completion_window"), TEXT("24h"));

            Self->SendHttp(
                TEXT("POST"),
                TEXT("https://api.openai.com/v1/batches"),
                Headers,
                N2CNativeBatchProcessorPrivate::SerializeObject(Create),
                TEXT("application/json"),
                [Self](int32 CreateStatus, const FString& CreateBody, const FHttpResponsePtr&)
                {
                    if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(CreateStatus))
                    {
                        Self->FallbackBeforeStart(FString::Printf(
                            TEXT("OpenAI batch creation failed (HTTP %d): %s"),
                            CreateStatus,
                            *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(CreateBody)));
                        return;
                    }

                    const TSharedPtr<FJsonObject> Batch = N2CNativeBatchProcessorPrivate::ParseObject(CreateBody);
                    FString BatchId;
                    if (!Batch.IsValid() || !Batch->TryGetStringField(TEXT("id"), BatchId) || BatchId.IsEmpty())
                    {
                        Self->FallbackBeforeStart(FString::Printf(
                            TEXT("OpenAI batch creation did not return a batch id. Response: %s"),
                            *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(CreateBody)));
                        return;
                    }

                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Submitted OpenAI native batch %s with %d requests"), *BatchId, Self->Requests.Num()),
                        EN2CLogSeverity::Info,
                        TEXT("NativeBatch"));
                    Self->PollOpenAIBatch(BatchId);
                });
        });
}

void FN2CNativeBatchProcessor::PollOpenAIBatch(const FString& BatchId)
{
    TMap<FString, FString> Headers;
    Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("GET"),
        FString::Printf(TEXT("https://api.openai.com/v1/batches/%s"), *BatchId),
        Headers,
        FString(),
        TEXT("application/json"),
        [Self, BatchId, Headers](int32 Status, const FString& Body, const FHttpResponsePtr&) mutable
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->CompleteMissingItems(FString::Printf(
                    TEXT("OpenAI batch %s status request failed (HTTP %d): %s"),
                    *BatchId,
                    Status,
                    *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(Body)));
                Self->FinishBatch();
                return;
            }

            const TSharedPtr<FJsonObject> Batch = N2CNativeBatchProcessorPrivate::ParseObject(Body);
            FString BatchStatus;
            if (!Batch.IsValid() || !Batch->TryGetStringField(TEXT("status"), BatchStatus))
            {
                Self->CompleteMissingItems(FString::Printf(
                    TEXT("OpenAI batch %s status response was malformed. Response: %s"),
                    *BatchId,
                    *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(Body)));
                Self->FinishBatch();
                return;
            }

            if (BatchStatus == TEXT("completed"))
            {
                FString OutputFileId;
                if (!Batch->TryGetStringField(TEXT("output_file_id"), OutputFileId) || OutputFileId.IsEmpty())
                {
                    const FString BatchErrors =
                        N2CNativeBatchProcessorPrivate::DescribeOpenAIBatchErrors(Batch);
                    Self->CompleteMissingItems(BatchErrors.IsEmpty()
                        ? FString::Printf(
                            TEXT("OpenAI batch %s completed without an output file"),
                            *BatchId)
                        : FString::Printf(
                            TEXT("OpenAI batch %s completed without an output file: %s"),
                            *BatchId,
                            *BatchErrors));
                    Self->FinishBatch();
                    return;
                }

                Self->SendHttp(
                    TEXT("GET"),
                    FString::Printf(TEXT("https://api.openai.com/v1/files/%s/content"), *OutputFileId),
                    Headers,
                    FString(),
                    TEXT("application/json"),
                    [Self, BatchId](int32 OutputStatus, const FString& OutputBody, const FHttpResponsePtr&)
                    {
                        if (N2CNativeBatchProcessorPrivate::IsSuccessCode(OutputStatus))
                        {
                            Self->ProcessOpenAIResults(OutputBody);
                        }
                        else
                        {
                            Self->CompleteMissingItems(FString::Printf(
                                TEXT("OpenAI batch %s output download failed (HTTP %d): %s"),
                                *BatchId,
                                OutputStatus,
                                *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(OutputBody)));
                        }
                        Self->FinishBatch();
                    });
                return;
            }

            if (BatchStatus == TEXT("failed") || BatchStatus == TEXT("expired") ||
                BatchStatus == TEXT("cancelled"))
            {
                const FString BatchErrors =
                    N2CNativeBatchProcessorPrivate::DescribeOpenAIBatchErrors(Batch);
                Self->CompleteMissingItems(BatchErrors.IsEmpty()
                    ? FString::Printf(
                        TEXT("OpenAI batch %s ended with status '%s' for model '%s'"),
                        *BatchId,
                        *BatchStatus,
                        *Self->Config.Model)
                    : FString::Printf(
                        TEXT("OpenAI batch %s ended with status '%s' for model '%s': %s"),
                        *BatchId,
                        *BatchStatus,
                        *Self->Config.Model,
                        *BatchErrors));
                Self->FinishBatch();
                return;
            }

            Self->SchedulePoll(
                [Self, BatchId]() { Self->PollOpenAIBatch(BatchId); },
                N2CNativeBatchProcessorPrivate::PollDelaySeconds);
        });
}

void FN2CNativeBatchProcessor::ProcessOpenAIResults(const FString& JsonLines)
{
    TArray<FString> Lines;
    JsonLines.ParseIntoArrayLines(Lines, true);
    for (const FString& Line : Lines)
    {
        const TSharedPtr<FJsonObject> Entry = N2CNativeBatchProcessorPrivate::ParseObject(Line);
        if (!Entry.IsValid())
        {
            continue;
        }

        FString CustomId;
        if (!Entry->TryGetStringField(TEXT("custom_id"), CustomId))
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* Response = nullptr;
        if (Entry->TryGetObjectField(TEXT("response"), Response) && Response && Response->IsValid())
        {
            const TSharedPtr<FJsonObject>* Body = nullptr;
            if ((*Response)->TryGetObjectField(TEXT("body"), Body) && Body && Body->IsValid())
            {
                CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::SerializeObject(*Body));
                continue;
            }
        }

        const TSharedPtr<FJsonObject>* Error = nullptr;
        if (Entry->TryGetObjectField(TEXT("error"), Error) && Error && Error->IsValid())
        {
            CompleteItemByCustomId(
                CustomId,
                N2CNativeBatchProcessorPrivate::SerializeObject(
                    N2CNativeBatchProcessorPrivate::WrapErrorObject(*Error)));
        }
        else
        {
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::MakeErrorResponse(
                TEXT("OpenAI batch item did not contain a response body")));
        }
    }

    CompleteMissingItems(TEXT("OpenAI batch output did not contain a result for this request"));
}

void FN2CNativeBatchProcessor::StartAnthropicBatch()
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FN2CNativeBatchRequest& Request : Requests)
    {
        const TSharedPtr<FJsonObject> Params =
            N2CNativeBatchProcessorPrivate::ParseObject(Request.FormattedPayload);
        if (!Params.IsValid())
        {
            FallbackBeforeStart(TEXT("Could not parse an Anthropic request body for batch submission"));
            return;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("custom_id"), MakeCustomId(Request.RequestId));
        Entry->SetObjectField(TEXT("params"), Params);
        Values.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("requests"), Values);

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-api-key"), Config.ApiKey);
    Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
    Headers.Add(TEXT("anthropic-beta"), TEXT("message-batches-2024-09-24"));

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("POST"),
        TEXT("https://api.anthropic.com/v1/messages/batches"),
        Headers,
        N2CNativeBatchProcessorPrivate::SerializeObject(Root),
        TEXT("application/json"),
        [Self](int32 Status, const FString& Body, const FHttpResponsePtr&)
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->FallbackBeforeStart(FString::Printf(
                    TEXT("Anthropic batch creation failed (HTTP %d): %s"),
                    Status,
                    *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(Body)));
                return;
            }

            const TSharedPtr<FJsonObject> Batch = N2CNativeBatchProcessorPrivate::ParseObject(Body);
            FString BatchId;
            if (!Batch.IsValid() || !Batch->TryGetStringField(TEXT("id"), BatchId) || BatchId.IsEmpty())
            {
                Self->FallbackBeforeStart(FString::Printf(
                    TEXT("Anthropic batch creation did not return a batch id. Response: %s"),
                    *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(Body)));
                return;
            }

            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Submitted Anthropic native batch %s with %d requests"), *BatchId, Self->Requests.Num()),
                EN2CLogSeverity::Info,
                TEXT("NativeBatch"));
            Self->PollAnthropicBatch(BatchId);
        });
}

void FN2CNativeBatchProcessor::PollAnthropicBatch(const FString& BatchId)
{
    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-api-key"), Config.ApiKey);
    Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
    Headers.Add(TEXT("anthropic-beta"), TEXT("message-batches-2024-09-24"));

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("GET"),
        FString::Printf(TEXT("https://api.anthropic.com/v1/messages/batches/%s"), *BatchId),
        Headers,
        FString(),
        TEXT("application/json"),
        [Self, BatchId, Headers](int32 Status, const FString& Body, const FHttpResponsePtr&) mutable
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->CompleteMissingItems(FString::Printf(
                    TEXT("Anthropic batch %s status request failed (HTTP %d): %s"),
                    *BatchId,
                    Status,
                    *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(Body)));
                Self->FinishBatch();
                return;
            }

            const TSharedPtr<FJsonObject> Batch = N2CNativeBatchProcessorPrivate::ParseObject(Body);
            FString ProcessingStatus;
            if (!Batch.IsValid() || !Batch->TryGetStringField(TEXT("processing_status"), ProcessingStatus))
            {
                Self->CompleteMissingItems(FString::Printf(
                    TEXT("Anthropic batch %s status response was malformed. Response: %s"),
                    *BatchId,
                    *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(Body)));
                Self->FinishBatch();
                return;
            }

            if (ProcessingStatus == TEXT("ended"))
            {
                Self->SendHttp(
                    TEXT("GET"),
                    FString::Printf(TEXT("https://api.anthropic.com/v1/messages/batches/%s/results"), *BatchId),
                    Headers,
                    FString(),
                    TEXT("application/json"),
                    [Self, BatchId](int32 ResultStatus, const FString& ResultBody, const FHttpResponsePtr&)
                    {
                        if (N2CNativeBatchProcessorPrivate::IsSuccessCode(ResultStatus))
                        {
                            Self->ProcessAnthropicResults(ResultBody);
                        }
                        else
                        {
                            Self->CompleteMissingItems(FString::Printf(
                                TEXT("Anthropic batch %s results download failed (HTTP %d): %s"),
                                *BatchId,
                                ResultStatus,
                                *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(ResultBody)));
                        }
                        Self->FinishBatch();
                    });
                return;
            }

            Self->SchedulePoll(
                [Self, BatchId]() { Self->PollAnthropicBatch(BatchId); },
                N2CNativeBatchProcessorPrivate::PollDelaySeconds);
        });
}

void FN2CNativeBatchProcessor::ProcessAnthropicResults(const FString& JsonLines)
{
    TArray<FString> Lines;
    JsonLines.ParseIntoArrayLines(Lines, true);
    for (const FString& Line : Lines)
    {
        const TSharedPtr<FJsonObject> Entry = N2CNativeBatchProcessorPrivate::ParseObject(Line);
        if (!Entry.IsValid())
        {
            continue;
        }

        FString CustomId;
        if (!Entry->TryGetStringField(TEXT("custom_id"), CustomId))
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* Result = nullptr;
        if (!Entry->TryGetObjectField(TEXT("result"), Result) || !Result || !Result->IsValid())
        {
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::MakeErrorResponse(
                TEXT("Anthropic batch item did not contain a result")));
            continue;
        }

        FString ResultType;
        (*Result)->TryGetStringField(TEXT("type"), ResultType);
        if (ResultType == TEXT("succeeded"))
        {
            const TSharedPtr<FJsonObject>* Message = nullptr;
            if ((*Result)->TryGetObjectField(TEXT("message"), Message) && Message && Message->IsValid())
            {
                CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::SerializeObject(*Message));
                continue;
            }
        }

        const TSharedPtr<FJsonObject>* Error = nullptr;
        if ((*Result)->TryGetObjectField(TEXT("error"), Error) && Error && Error->IsValid())
        {
            CompleteItemByCustomId(
                CustomId,
                N2CNativeBatchProcessorPrivate::SerializeObject(
                    N2CNativeBatchProcessorPrivate::WrapErrorObject(*Error)));
        }
        else
        {
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::MakeErrorResponse(
                FString::Printf(TEXT("Anthropic batch item ended with result type '%s'"), *ResultType)));
        }
    }

    CompleteMissingItems(TEXT("Anthropic batch output did not contain a result for this request"));
}

void FN2CNativeBatchProcessor::StartGeminiBatch()
{
    TArray<TSharedPtr<FJsonValue>> WrappedRequests;
    int64 ApproximateCharacters = 0;

    for (const FN2CNativeBatchRequest& Request : Requests)
    {
        const TSharedPtr<FJsonObject> RequestObject =
            N2CNativeBatchProcessorPrivate::ParseObject(Request.FormattedPayload);
        if (!RequestObject.IsValid())
        {
            FallbackBeforeStart(TEXT("Could not parse a Gemini request body for batch submission"));
            return;
        }

        TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
        Metadata->SetStringField(TEXT("key"), MakeCustomId(Request.RequestId));

        TSharedPtr<FJsonObject> Wrapped = MakeShared<FJsonObject>();
        Wrapped->SetObjectField(TEXT("request"), RequestObject);
        Wrapped->SetObjectField(TEXT("metadata"), Metadata);
        WrappedRequests.Add(MakeShared<FJsonValueObject>(Wrapped));
        ApproximateCharacters += Request.FormattedPayload.Len();
    }

    if (ApproximateCharacters > 8 * 1024 * 1024)
    {
        FallbackBeforeStart(TEXT("Gemini batch is too large for safe inline submission"));
        return;
    }

    TSharedPtr<FJsonObject> InlinedRequests = MakeShared<FJsonObject>();
    InlinedRequests->SetArrayField(TEXT("requests"), WrappedRequests);

    TSharedPtr<FJsonObject> InputConfig = MakeShared<FJsonObject>();
    InputConfig->SetObjectField(TEXT("requests"), InlinedRequests);

    TSharedPtr<FJsonObject> Batch = MakeShared<FJsonObject>();
    Batch->SetStringField(TEXT("display_name"), TEXT("NodeToCode Blueprint Translation"));
    Batch->SetObjectField(TEXT("input_config"), InputConfig);

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("batch"), Batch);

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-goog-api-key"), Config.ApiKey);

    const FString Url = FString::Printf(
        TEXT("https://generativelanguage.googleapis.com/v1beta/models/%s:batchGenerateContent"),
        *Config.Model);

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("POST"),
        Url,
        Headers,
        N2CNativeBatchProcessorPrivate::SerializeObject(Root),
        TEXT("application/json"),
        [Self](int32 Status, const FString& Body, const FHttpResponsePtr&)
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->FallbackBeforeStart(FString::Printf(
                    TEXT("Gemini batch creation failed (HTTP %d): %s"),
                    Status,
                    *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(Body)));
                return;
            }

            const TSharedPtr<FJsonObject> BatchResource = N2CNativeBatchProcessorPrivate::ParseObject(Body);
            FString BatchName;
            if (!BatchResource.IsValid() || !BatchResource->TryGetStringField(TEXT("name"), BatchName) || BatchName.IsEmpty())
            {
                Self->FallbackBeforeStart(FString::Printf(
                    TEXT("Gemini batch creation did not return a batch name. Response: %s"),
                    *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(Body)));
                return;
            }

            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Submitted Gemini native batch %s with %d requests"), *BatchName, Self->Requests.Num()),
                EN2CLogSeverity::Info,
                TEXT("NativeBatch"));
            Self->PollGeminiBatch(BatchName);
        });
}

void FN2CNativeBatchProcessor::PollGeminiBatch(const FString& BatchName)
{
    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-goog-api-key"), Config.ApiKey);

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("GET"),
        FString::Printf(TEXT("https://generativelanguage.googleapis.com/v1beta/%s"), *BatchName),
        Headers,
        FString(),
        TEXT("application/json"),
        [Self, BatchName](int32 Status, const FString& Body, const FHttpResponsePtr&)
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->CompleteMissingItems(FString::Printf(
                    TEXT("Gemini batch %s status request failed (HTTP %d): %s"),
                    *BatchName,
                    Status,
                    *N2CNativeBatchProcessorPrivate::DescribeHttpFailure(Body)));
                Self->FinishBatch();
                return;
            }

            const TSharedPtr<FJsonObject> BatchResource = N2CNativeBatchProcessorPrivate::ParseObject(Body);
            if (!BatchResource.IsValid())
            {
                Self->CompleteMissingItems(FString::Printf(
                    TEXT("Gemini batch %s status response was malformed. Response: %s"),
                    *BatchName,
                    *N2CNativeBatchProcessorPrivate::TruncateDiagnostic(Body)));
                Self->FinishBatch();
                return;
            }

            FString State;
            BatchResource->TryGetStringField(TEXT("state"), State);

            const TSharedPtr<FJsonObject>* ResponseObject = nullptr;
            if (State.IsEmpty() && BatchResource->TryGetObjectField(TEXT("response"), ResponseObject) &&
                ResponseObject && ResponseObject->IsValid())
            {
                (*ResponseObject)->TryGetStringField(TEXT("state"), State);
            }

            const bool bSucceeded =
                State == TEXT("JOB_STATE_SUCCEEDED") || State == TEXT("BATCH_STATE_SUCCEEDED");
            const bool bFailed =
                State == TEXT("JOB_STATE_FAILED") || State == TEXT("JOB_STATE_CANCELLED") ||
                State == TEXT("JOB_STATE_EXPIRED") || State == TEXT("BATCH_STATE_FAILED") ||
                State == TEXT("BATCH_STATE_CANCELLED") || State == TEXT("BATCH_STATE_EXPIRED");

            if (bSucceeded)
            {
                Self->ProcessGeminiResults(BatchResource);
                Self->FinishBatch();
                return;
            }

            if (bFailed)
            {
                const FString ProviderError =
                    N2CNativeBatchProcessorPrivate::DescribeProviderErrorObject(BatchResource);
                Self->CompleteMissingItems(ProviderError.IsEmpty()
                    ? FString::Printf(
                        TEXT("Gemini batch %s ended with state '%s' for model '%s'"),
                        *BatchName,
                        *State,
                        *Self->Config.Model)
                    : FString::Printf(
                        TEXT("Gemini batch %s ended with state '%s' for model '%s': %s"),
                        *BatchName,
                        *State,
                        *Self->Config.Model,
                        *ProviderError));
                Self->FinishBatch();
                return;
            }

            Self->SchedulePoll(
                [Self, BatchName]() { Self->PollGeminiBatch(BatchName); },
                N2CNativeBatchProcessorPrivate::PollDelaySeconds);
        });
}

void FN2CNativeBatchProcessor::ProcessGeminiResults(const TSharedPtr<FJsonObject>& BatchObject)
{
    if (!BatchObject.IsValid())
    {
        CompleteMissingItems(TEXT("Gemini batch completed without a batch resource"));
        return;
    }

    const TSharedPtr<FJsonObject>* Resource = &BatchObject;
    const TSharedPtr<FJsonObject>* WrappedResource = nullptr;
    if (BatchObject->TryGetObjectField(TEXT("response"), WrappedResource) && WrappedResource && WrappedResource->IsValid())
    {
        Resource = WrappedResource;
    }

    const TArray<TSharedPtr<FJsonValue>>* InlineResponses = nullptr;

    const TSharedPtr<FJsonObject>* Dest = nullptr;
    if ((*Resource)->TryGetObjectField(TEXT("dest"), Dest) && Dest && Dest->IsValid())
    {
        (*Dest)->TryGetArrayField(TEXT("inlinedResponses"), InlineResponses);
    }

    if (!InlineResponses)
    {
        const TSharedPtr<FJsonObject>* Output = nullptr;
        if ((*Resource)->TryGetObjectField(TEXT("output"), Output) && Output && Output->IsValid())
        {
            const TSharedPtr<FJsonObject>* InlinedWrapper = nullptr;
            if ((*Output)->TryGetObjectField(TEXT("inlinedResponses"), InlinedWrapper) &&
                InlinedWrapper && InlinedWrapper->IsValid())
            {
                (*InlinedWrapper)->TryGetArrayField(TEXT("inlinedResponses"), InlineResponses);
            }
            else
            {
                (*Output)->TryGetArrayField(TEXT("inlinedResponses"), InlineResponses);
            }
        }
    }

    if (!InlineResponses)
    {
        CompleteMissingItems(TEXT("Gemini batch completed without inline responses"));
        return;
    }

    for (int32 Index = 0; Index < InlineResponses->Num() && Index < Requests.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Entry = (*InlineResponses)[Index].IsValid()
            ? (*InlineResponses)[Index]->AsObject()
            : nullptr;
        if (!Entry.IsValid())
        {
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::MakeErrorResponse(
                TEXT("Gemini batch item was malformed")));
            continue;
        }

        const TSharedPtr<FJsonObject>* ItemResponse = nullptr;
        if (Entry->TryGetObjectField(TEXT("response"), ItemResponse) && ItemResponse && ItemResponse->IsValid())
        {
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::SerializeObject(*ItemResponse));
            continue;
        }

        const TSharedPtr<FJsonObject>* Error = nullptr;
        if (Entry->TryGetObjectField(TEXT("error"), Error) && Error && Error->IsValid())
        {
            CompleteItemByIndex(
                Index,
                N2CNativeBatchProcessorPrivate::SerializeObject(
                    N2CNativeBatchProcessorPrivate::WrapErrorObject(*Error)));
        }
        else
        {
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::MakeErrorResponse(
                TEXT("Gemini batch item contained neither response nor error")));
        }
    }

    CompleteMissingItems(TEXT("Gemini batch output did not contain a result for this request"));
}
