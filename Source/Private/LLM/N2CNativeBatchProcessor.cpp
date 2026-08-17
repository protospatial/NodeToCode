// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CNativeBatchProcessor.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/DateTime.h"
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

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
{
    if (!Object.IsValid())
    {
        return TEXT("{}");
    }

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Output;
}

TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
{
    TSharedPtr<FJsonObject> Object;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    FJsonSerializer::Deserialize(Reader, Object);
    return Object;
}

FString MakeErrorResponse(const FString& Message)
{
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetStringField(TEXT("message"), Message);
    Error->SetStringField(TEXT("type"), TEXT("batch_error"));

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("error"), Error);
    return SerializeJsonObject(Root);
}

float AddPositiveJitter(float Seconds)
{
    const uint64 Cycles = FPlatformTime::Cycles64();
    const float UnitJitter = static_cast<float>(Cycles % 1000ULL) / 1000.0f;
    return Seconds + (Seconds * JitterFraction * UnitJitter);
}

float CalculateRetryDelay(const FHttpResponsePtr& Response, int32 RetryCount)
{
    if (Response.IsValid())
    {
        const FString RetryAfter = Response->GetHeader(TEXT("Retry-After")).TrimStartAndEnd();
        if (!RetryAfter.IsEmpty())
        {
            const float NumericDelay = FCString::Atof(*RetryAfter);
            if (NumericDelay > 0.0f)
            {
                return AddPositiveJitter(FMath::Clamp(NumericDelay, 0.1f, 300.0f));
            }

            FDateTime RetryDate;
            if (FDateTime::ParseHttpDate(RetryAfter, RetryDate))
            {
                const double Delay = (RetryDate - FDateTime::UtcNow()).GetTotalSeconds();
                if (Delay > 0.0)
                {
                    return AddPositiveJitter(FMath::Clamp(static_cast<float>(Delay), 0.1f, 300.0f));
                }
            }
        }

        const FString RetryAfterMs = Response->GetHeader(TEXT("retry-after-ms")).TrimStartAndEnd();
        const float Milliseconds = FCString::Atof(*RetryAfterMs);
        if (Milliseconds > 0.0f)
        {
            return AddPositiveJitter(FMath::Clamp(Milliseconds / 1000.0f, 0.1f, 300.0f));
        }
    }

    return AddPositiveJitter(FMath::Min(
        InitialBackoffSeconds * FMath::Pow(2.0f, static_cast<float>(RetryCount)),
        MaxBackoffSeconds));
}

bool IsSuccessCode(int32 Code)
{
    return Code >= 200 && Code < 300;
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
            return MakeCustomId(Request.RequestId).Equals(CustomId, ESearchCase::CaseSensitive);
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
        [Self, Verb, Url, Headers, Body, ContentType, OnComplete = MoveTemp(OnComplete), RetryCount](
            FHttpRequestPtr,
            FHttpResponsePtr Response,
            bool bWasSuccessful) mutable
        {
            const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;
            const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : TEXT("");

            if (bWasSuccessful && StatusCode == 429 && RetryCount < N2CNativeBatchProcessorPrivate::MaxRateLimitRetries)
            {
                const float Delay = N2CNativeBatchProcessorPrivate::CalculateRetryDelay(Response, RetryCount);
                FN2CLogger::Get().LogWarning(
                    FString::Printf(
                        TEXT("Native batch HTTP 429 received. Retrying in %.2f seconds (retry %d/%d)"),
                        Delay,
                        RetryCount + 1,
                        N2CNativeBatchProcessorPrivate::MaxRateLimitRetries),
                    TEXT("NativeBatch"));

                FTSTicker::GetCoreTicker().AddTicker(
                    TEXT("NodeToCode.NativeBatch429"),
                    Delay,
                    [Self, Verb, Url, Headers, Body, ContentType, OnComplete = MoveTemp(OnComplete), RetryCount](float) mutable
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
        OnComplete(0, TEXT(""), nullptr);
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

void FN2CNativeBatchProcessor::CompleteItemByCustomId(const FString& CustomId, const FString& RawResponse)
{
    const FN2CNativeBatchRequest* Request = FindRequestByCustomId(CustomId);
    if (!Request || CompletedRequestIds.Contains(Request->RequestId))
    {
        return;
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

void FN2CNativeBatchProcessor::CompleteItemByIndex(int32 RequestIndex, const FString& RawResponse)
{
    if (!Requests.IsValidIndex(RequestIndex))
    {
        return;
    }
    CompleteItemByCustomId(MakeCustomId(Requests[RequestIndex].RequestId), RawResponse);
}

void FN2CNativeBatchProcessor::CompleteMissingItems(const FString& ErrorMessage)
{
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
        TSharedPtr<FJsonObject> BodyObject = N2CNativeBatchProcessorPrivate::ParseJsonObject(Request.FormattedPayload);
        if (!BodyObject.IsValid())
        {
            FallbackBeforeStart(TEXT("Could not parse an OpenAI request body for batch submission"));
            return;
        }

        TSharedPtr<FJsonObject> LineObject = MakeShared<FJsonObject>();
        LineObject->SetStringField(TEXT("custom_id"), MakeCustomId(Request.RequestId));
        LineObject->SetStringField(TEXT("method"), TEXT("POST"));
        LineObject->SetStringField(TEXT("url"), TEXT("/v1/chat/completions"));
        LineObject->SetObjectField(TEXT("body"), BodyObject);
        JsonLines += N2CNativeBatchProcessorPrivate::SerializeJsonObject(LineObject) + TEXT("\n");
    }

    const FString Boundary = FString::Printf(TEXT("----NodeToCodeBatch%llu"), FPlatformTime::Cycles64());
    FString Multipart;
    Multipart += FString::Printf(TEXT("--%s\r\n"), *Boundary);
    Multipart += TEXT("Content-Disposition: form-data; name=\"purpose\"\r\n\r\n");
    Multipart += TEXT("batch\r\n");
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
        [Self, Headers](int32 Status, const FString& ResponseBody, const FHttpResponsePtr&) mutable
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->FallbackBeforeStart(FString::Printf(TEXT("OpenAI batch input upload failed (HTTP %d)"), Status));
                return;
            }

            TSharedPtr<FJsonObject> UploadObject = N2CNativeBatchProcessorPrivate::ParseJsonObject(ResponseBody);
            FString FileId;
            if (!UploadObject.IsValid() || !UploadObject->TryGetStringField(TEXT("id"), FileId) || FileId.IsEmpty())
            {
                Self->FallbackBeforeStart(TEXT("OpenAI batch input upload did not return a file id"));
                return;
            }

            TSharedPtr<FJsonObject> CreateObject = MakeShared<FJsonObject>();
            CreateObject->SetStringField(TEXT("input_file_id"), FileId);
            CreateObject->SetStringField(TEXT("endpoint"), TEXT("/v1/chat/completions"));
            CreateObject->SetStringField(TEXT("completion_window"), TEXT("24h"));

            Self->SendHttp(
                TEXT("POST"),
                TEXT("https://api.openai.com/v1/batches"),
                Headers,
                N2CNativeBatchProcessorPrivate::SerializeJsonObject(CreateObject),
                TEXT("application/json"),
                [Self](int32 CreateStatus, const FString& CreateBody, const FHttpResponsePtr&)
                {
                    if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(CreateStatus))
                    {
                        Self->FallbackBeforeStart(FString::Printf(TEXT("OpenAI batch creation failed (HTTP %d)"), CreateStatus));
                        return;
                    }

                    TSharedPtr<FJsonObject> BatchObject = N2CNativeBatchProcessorPrivate::ParseJsonObject(CreateBody);
                    FString BatchId;
                    if (!BatchObject.IsValid() || !BatchObject->TryGetStringField(TEXT("id"), BatchId) || BatchId.IsEmpty())
                    {
                        Self->FallbackBeforeStart(TEXT("OpenAI batch creation did not return a batch id"));
                        return;
                    }

                    Self->bProviderJobCreated = true;
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
        TEXT(""),
        TEXT("application/json"),
        [Self, BatchId, Headers](int32 Status, const FString& Body, const FHttpResponsePtr&) mutable
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->CompleteMissingItems(FString::Printf(TEXT("OpenAI batch status request failed (HTTP %d)"), Status));
                Self->FinishBatch();
                return;
            }

            TSharedPtr<FJsonObject> BatchObject = N2CNativeBatchProcessorPrivate::ParseJsonObject(Body);
            FString BatchStatus;
            if (!BatchObject.IsValid() || !BatchObject->TryGetStringField(TEXT("status"), BatchStatus))
            {
                Self->CompleteMissingItems(TEXT("OpenAI batch status response was malformed"));
                Self->FinishBatch();
                return;
            }

            if (BatchStatus == TEXT("completed"))
            {
                FString OutputFileId;
                if (!BatchObject->TryGetStringField(TEXT("output_file_id"), OutputFileId) || OutputFileId.IsEmpty())
                {
                    Self->CompleteMissingItems(TEXT("OpenAI batch completed without an output file"));
                    Self->FinishBatch();
                    return;
                }

                Self->SendHttp(
                    TEXT("GET"),
                    FString::Printf(TEXT("https://api.openai.com/v1/files/%s/content"), *OutputFileId),
                    Headers,
                    TEXT(""),
                    TEXT("application/json"),
                    [Self](int32 OutputStatus, const FString& OutputBody, const FHttpResponsePtr&)
                    {
                        if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(OutputStatus))
                        {
                            Self->CompleteMissingItems(FString::Printf(TEXT("OpenAI batch output download failed (HTTP %d)"), OutputStatus));
                        }
                        else
                        {
                            Self->ProcessOpenAIResults(OutputBody);
                        }
                        Self->FinishBatch();
                    });
                return;
            }

            if (BatchStatus == TEXT("failed") || BatchStatus == TEXT("expired") || BatchStatus == TEXT("cancelled"))
            {
                Self->CompleteMissingItems(FString::Printf(TEXT("OpenAI batch ended with status '%s'"), *BatchStatus));
                Self->FinishBatch();
                return;
            }

            Self->SchedulePoll([Self, BatchId]() { Self->PollOpenAIBatch(BatchId); });
        });
}

void FN2CNativeBatchProcessor::ProcessOpenAIResults(const FString& JsonLines)
{
    TArray<FString> Lines;
    JsonLines.ParseIntoArrayLines(Lines, true);
    for (const FString& Line : Lines)
    {
        TSharedPtr<FJsonObject> Output = N2CNativeBatchProcessorPrivate::ParseJsonObject(Line);
        if (!Output.IsValid())
        {
            continue;
        }

        FString CustomId;
        if (!Output->TryGetStringField(TEXT("custom_id"), CustomId))
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* ResponseObject = nullptr;
        if (Output->TryGetObjectField(TEXT("response"), ResponseObject) && ResponseObject && ResponseObject->IsValid())
        {
            const TSharedPtr<FJsonObject>* BodyObject = nullptr;
            if ((*ResponseObject)->TryGetObjectField(TEXT("body"), BodyObject) && BodyObject && BodyObject->IsValid())
            {
                CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::SerializeJsonObject(*BodyObject));
                continue;
            }
        }

        const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
        if (Output->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
        {
            TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
            Wrapper->SetObjectField(TEXT("error"), *ErrorObject);
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::SerializeJsonObject(Wrapper));
        }
        else
        {
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::MakeErrorResponse(TEXT("OpenAI batch item did not contain a response body")));
        }
    }

    CompleteMissingItems(TEXT("OpenAI batch output did not contain a result for this request"));
}

void FN2CNativeBatchProcessor::StartAnthropicBatch()
{
    TArray<TSharedPtr<FJsonValue>> RequestValues;
    for (const FN2CNativeBatchRequest& Request : Requests)
    {
        TSharedPtr<FJsonObject> Params = N2CNativeBatchProcessorPrivate::ParseJsonObject(Request.FormattedPayload);
        if (!Params.IsValid())
        {
            FallbackBeforeStart(TEXT("Could not parse an Anthropic request body for batch submission"));
            return;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("custom_id"), MakeCustomId(Request.RequestId));
        Entry->SetObjectField(TEXT("params"), Params);
        RequestValues.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("requests"), RequestValues);

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-api-key"), Config.ApiKey);
    Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("POST"),
        TEXT("https://api.anthropic.com/v1/messages/batches"),
        Headers,
        N2CNativeBatchProcessorPrivate::SerializeJsonObject(Root),
        TEXT("application/json"),
        [Self](int32 Status, const FString& Body, const FHttpResponsePtr&)
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->FallbackBeforeStart(FString::Printf(TEXT("Anthropic batch creation failed (HTTP %d)"), Status));
                return;
            }

            TSharedPtr<FJsonObject> Batch = N2CNativeBatchProcessorPrivate::ParseJsonObject(Body);
            FString BatchId;
            if (!Batch.IsValid() || !Batch->TryGetStringField(TEXT("id"), BatchId) || BatchId.IsEmpty())
            {
                Self->FallbackBeforeStart(TEXT("Anthropic batch creation did not return a batch id"));
                return;
            }

            Self->bProviderJobCreated = true;
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

    const TSharedRef<FN2CNativeBatchProcessor> Self = AsShared();
    SendHttp(
        TEXT("GET"),
        FString::Printf(TEXT("https://api.anthropic.com/v1/messages/batches/%s"), *BatchId),
        Headers,
        TEXT(""),
        TEXT("application/json"),
        [Self, BatchId, Headers](int32 Status, const FString& Body, const FHttpResponsePtr&) mutable
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->CompleteMissingItems(FString::Printf(TEXT("Anthropic batch status request failed (HTTP %d)"), Status));
                Self->FinishBatch();
                return;
            }

            TSharedPtr<FJsonObject> Batch = N2CNativeBatchProcessorPrivate::ParseJsonObject(Body);
            FString ProcessingStatus;
            if (!Batch.IsValid() || !Batch->TryGetStringField(TEXT("processing_status"), ProcessingStatus))
            {
                Self->CompleteMissingItems(TEXT("Anthropic batch status response was malformed"));
                Self->FinishBatch();
                return;
            }

            if (ProcessingStatus == TEXT("ended"))
            {
                FString ResultsUrl;
                if (!Batch->TryGetStringField(TEXT("results_url"), ResultsUrl) || ResultsUrl.IsEmpty())
                {
                    Self->CompleteMissingItems(TEXT("Anthropic batch ended without a results URL"));
                    Self->FinishBatch();
                    return;
                }

                Self->SendHttp(
                    TEXT("GET"),
                    ResultsUrl,
                    Headers,
                    TEXT(""),
                    TEXT("application/json"),
                    [Self](int32 ResultStatus, const FString& ResultBody, const FHttpResponsePtr&)
                    {
                        if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(ResultStatus))
                        {
                            Self->CompleteMissingItems(FString::Printf(TEXT("Anthropic batch results download failed (HTTP %d)"), ResultStatus));
                        }
                        else
                        {
                            Self->ProcessAnthropicResults(ResultBody);
                        }
                        Self->FinishBatch();
                    });
                return;
            }

            Self->SchedulePoll([Self, BatchId]() { Self->PollAnthropicBatch(BatchId); });
        });
}

void FN2CNativeBatchProcessor::ProcessAnthropicResults(const FString& JsonLines)
{
    TArray<FString> Lines;
    JsonLines.ParseIntoArrayLines(Lines, true);
    for (const FString& Line : Lines)
    {
        TSharedPtr<FJsonObject> Entry = N2CNativeBatchProcessorPrivate::ParseJsonObject(Line);
        if (!Entry.IsValid())
        {
            continue;
        }

        FString CustomId;
        if (!Entry->TryGetStringField(TEXT("custom_id"), CustomId))
        {
            continue;
        }

        const TSharedPtr<FJsonObject>* ResultObject = nullptr;
        if (!Entry->TryGetObjectField(TEXT("result"), ResultObject) || !ResultObject || !ResultObject->IsValid())
        {
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::MakeErrorResponse(TEXT("Anthropic batch item did not contain a result")));
            continue;
        }

        FString ResultType;
        (*ResultObject)->TryGetStringField(TEXT("type"), ResultType);
        if (ResultType == TEXT("succeeded"))
        {
            const TSharedPtr<FJsonObject>* MessageObject = nullptr;
            if ((*ResultObject)->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject && MessageObject->IsValid())
            {
                CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::SerializeJsonObject(*MessageObject));
                continue;
            }
        }

        const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
        if ((*ResultObject)->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
        {
            TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
            Wrapper->SetObjectField(TEXT("error"), *ErrorObject);
            CompleteItemByCustomId(CustomId, N2CNativeBatchProcessorPrivate::SerializeJsonObject(Wrapper));
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
    int64 ApproximateBytes = 0;
    for (const FN2CNativeBatchRequest& Request : Requests)
    {
        TSharedPtr<FJsonObject> RequestObject = N2CNativeBatchProcessorPrivate::ParseJsonObject(Request.FormattedPayload);
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
        ApproximateBytes += Request.FormattedPayload.Len();
    }

    // Inline Gemini batches are limited to 20 MB. Leave headroom for wrapper JSON and UTF-8 expansion.
    if (ApproximateBytes > 16 * 1024 * 1024)
    {
        FallbackBeforeStart(TEXT("Gemini batch request is too large for the inline batch API"));
        return;
    }

    TSharedPtr<FJsonObject> RequestsObject = MakeShared<FJsonObject>();
    RequestsObject->SetArrayField(TEXT("requests"), WrappedRequests);

    TSharedPtr<FJsonObject> InputConfig = MakeShared<FJsonObject>();
    InputConfig->SetObjectField(TEXT("requests"), RequestsObject);

    TSharedPtr<FJsonObject> BatchObject = MakeShared<FJsonObject>();
    BatchObject->SetStringField(TEXT("display_name"), TEXT("NodeToCode Blueprint Translation"));
    BatchObject->SetObjectField(TEXT("input_config"), InputConfig);

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetObjectField(TEXT("batch"), BatchObject);

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
        N2CNativeBatchProcessorPrivate::SerializeJsonObject(Root),
        TEXT("application/json"),
        [Self](int32 Status, const FString& Body, const FHttpResponsePtr&)
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->FallbackBeforeStart(FString::Printf(TEXT("Gemini batch creation failed (HTTP %d)"), Status));
                return;
            }

            TSharedPtr<FJsonObject> Job = N2CNativeBatchProcessorPrivate::ParseJsonObject(Body);
            FString BatchName;
            if (!Job.IsValid() || !Job->TryGetStringField(TEXT("name"), BatchName) || BatchName.IsEmpty())
            {
                Self->FallbackBeforeStart(TEXT("Gemini batch creation did not return a batch name"));
                return;
            }

            Self->bProviderJobCreated = true;
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
        TEXT(""),
        TEXT("application/json"),
        [Self, BatchName](int32 Status, const FString& Body, const FHttpResponsePtr&)
        {
            if (!N2CNativeBatchProcessorPrivate::IsSuccessCode(Status))
            {
                Self->CompleteMissingItems(FString::Printf(TEXT("Gemini batch status request failed (HTTP %d)"), Status));
                Self->FinishBatch();
                return;
            }

            TSharedPtr<FJsonObject> Job = N2CNativeBatchProcessorPrivate::ParseJsonObject(Body);
            if (!Job.IsValid())
            {
                Self->CompleteMissingItems(TEXT("Gemini batch status response was malformed"));
                Self->FinishBatch();
                return;
            }

            bool bDone = false;
            Job->TryGetBoolField(TEXT("done"), bDone);
            const TSharedPtr<FJsonObject>* Metadata = nullptr;
            FString State;
            if (Job->TryGetObjectField(TEXT("metadata"), Metadata) && Metadata && Metadata->IsValid())
            {
                (*Metadata)->TryGetStringField(TEXT("state"), State);
            }

            if (!bDone && State != TEXT("JOB_STATE_SUCCEEDED") && State != TEXT("JOB_STATE_FAILED") &&
                State != TEXT("JOB_STATE_CANCELLED") && State != TEXT("JOB_STATE_EXPIRED"))
            {
                Self->SchedulePoll([Self, BatchName]() { Self->PollGeminiBatch(BatchName); });
                return;
            }

            if (State == TEXT("JOB_STATE_SUCCEEDED"))
            {
                Self->ProcessGeminiResults(Job);
            }
            else
            {
                Self->CompleteMissingItems(FString::Printf(TEXT("Gemini batch ended with state '%s'"), *State));
            }
            Self->FinishBatch();
        });
}

void FN2CNativeBatchProcessor::ProcessGeminiResults(const TSharedPtr<FJsonObject>& JobObject)
{
    const TSharedPtr<FJsonObject>* ResponseObject = nullptr;
    if (!JobObject.IsValid() || !JobObject->TryGetObjectField(TEXT("response"), ResponseObject) ||
        !ResponseObject || !ResponseObject->IsValid())
    {
        CompleteMissingItems(TEXT("Gemini batch completed without an inline response object"));
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* InlineResponses = nullptr;
    if (!(*ResponseObject)->TryGetArrayField(TEXT("inlinedResponses"), InlineResponses) || !InlineResponses)
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
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::MakeErrorResponse(TEXT("Gemini batch item was malformed")));
            continue;
        }

        const TSharedPtr<FJsonObject>* ItemResponse = nullptr;
        if (Entry->TryGetObjectField(TEXT("response"), ItemResponse) && ItemResponse && ItemResponse->IsValid())
        {
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::SerializeJsonObject(*ItemResponse));
            continue;
        }

        const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
        if (Entry->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
        {
            TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
            Wrapper->SetObjectField(TEXT("error"), *ErrorObject);
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::SerializeJsonObject(Wrapper));
        }
        else
        {
            CompleteItemByIndex(Index, N2CNativeBatchProcessorPrivate::MakeErrorResponse(TEXT("Gemini batch item contained neither response nor error")));
        }
    }

    CompleteMissingItems(TEXT("Gemini batch output did not contain a result for this request"));
}
