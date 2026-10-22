// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CHttpHandlerBase.h"
#include "Utils/N2CLogger.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/DateTime.h"

namespace N2CHttpRateLimitRetryPrivate
{
constexpr int32 MaxRateLimitRetries = 5;
constexpr int32 MaxConcurrentProviderRequests = 2;
constexpr float InitialBackoffSeconds = 1.0f;
constexpr float MaxBackoffSeconds = 60.0f;
constexpr float MaxServerRetryAfterSeconds = 300.0f;
constexpr float JitterFraction = 0.10f;
constexpr float MinimumRetryDelaySeconds = 0.10f;

float AddPositiveJitter(float DelaySeconds)
{
    const uint64 Cycles = FPlatformTime::Cycles64();
    const float UnitJitter = static_cast<float>(Cycles % 1000ULL) / 1000.0f;
    return DelaySeconds + (DelaySeconds * JitterFraction * UnitJitter);
}

bool TryParsePositiveSeconds(const FString& Value, float& OutSeconds)
{
    const float ParsedSeconds = FCString::Atof(*Value);
    if (ParsedSeconds <= 0.0f)
    {
        return false;
    }

    OutSeconds = ParsedSeconds;
    return true;
}

bool TryParseRetrySecondsFromProviderMessage(const FString& ResponseBody, float& OutSeconds)
{
    const FString Marker = TEXT("Please try again in ");
    const int32 MarkerIndex = ResponseBody.Find(Marker, ESearchCase::IgnoreCase);
    if (MarkerIndex == INDEX_NONE)
    {
        return false;
    }

    FString Remaining = ResponseBody.Mid(MarkerIndex + Marker.Len()).TrimStartAndEnd();
    int32 EndIndex = INDEX_NONE;
    if (!Remaining.FindChar(TEXT('s'), EndIndex) || EndIndex <= 0)
    {
        return false;
    }

    return TryParsePositiveSeconds(Remaining.Left(EndIndex), OutSeconds);
}
}

void UN2CHttpHandlerBase::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    RequestTimeout = Config.TimeoutSeconds;
    PendingRequestQueue.Reset();
    ActiveRequestCount = 0;
    ProviderCooldownUntilSeconds = 0.0;
    bQueuePumpScheduled = false;
    bRateLimitObserved = false;
}

void UN2CHttpHandlerBase::PostLLMRequest(
    const FString& Endpoint,
    const FString& AuthToken,
    const FString& Payload,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!ValidateRequest(Endpoint, Payload))
    {
        FN2CLogger::Get().LogError(TEXT("Invalid request parameters"), TEXT("HttpHandler"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Invalid request parameters\"}"));
        return;
    }

    TSharedRef<FN2CQueuedHttpRequest> QueuedRequest = MakeShared<FN2CQueuedHttpRequest>();
    QueuedRequest->Endpoint = Endpoint;
    QueuedRequest->AuthToken = AuthToken;
    QueuedRequest->Payload = Payload;
    QueuedRequest->OnComplete = OnComplete;
    PendingRequestQueue.Add(QueuedRequest);

    if (PendingRequestQueue.Num() > 1 || ActiveRequestCount >= N2CHttpRateLimitRetryPrivate::MaxConcurrentProviderRequests)
    {
        FN2CLogger::Get().Log(
            FString::Printf(
                TEXT("Provider request queued (%d waiting, %d active)"),
                PendingRequestQueue.Num(),
                ActiveRequestCount),
            EN2CLogSeverity::Debug,
            TEXT("HttpHandler"));
    }

    PumpRequestQueue();
}

void UN2CHttpHandlerBase::PumpRequestQueue()
{
    if (PendingRequestQueue.IsEmpty())
    {
        return;
    }

    const double NowSeconds = FPlatformTime::Seconds();
    if (ProviderCooldownUntilSeconds > NowSeconds)
    {
        ScheduleQueuePump(static_cast<float>(ProviderCooldownUntilSeconds - NowSeconds));
        return;
    }

    ProviderCooldownUntilSeconds = 0.0;
    const int32 EffectiveMaxConcurrentRequests = bRateLimitObserved
        ? 1
        : N2CHttpRateLimitRetryPrivate::MaxConcurrentProviderRequests;

    while (ActiveRequestCount < EffectiveMaxConcurrentRequests &&
           !PendingRequestQueue.IsEmpty())
    {
        TSharedPtr<FN2CQueuedHttpRequest> QueuedRequest = PendingRequestQueue[0];
        PendingRequestQueue.RemoveAt(0);
        if (QueuedRequest.IsValid())
        {
            DispatchQueuedRequest(QueuedRequest.ToSharedRef());
        }
    }
}

void UN2CHttpHandlerBase::DispatchQueuedRequest(const TSharedRef<FN2CQueuedHttpRequest>& QueuedRequest)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(QueuedRequest->Endpoint);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    if (!QueuedRequest->AuthToken.IsEmpty())
    {
        Request->SetHeader(
            TEXT("Authorization"),
            FString::Printf(TEXT("Bearer %s"), *QueuedRequest->AuthToken));
    }

    for (const auto& Header : ExtraHeaders)
    {
        Request->SetHeader(Header.Key, Header.Value);
    }

    Request->SetContentAsString(QueuedRequest->Payload);
    Request->SetTimeout(RequestTimeout);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
    Request->SetActivityTimeout(RequestTimeout);
#endif

    ++ActiveRequestCount;
    TWeakObjectPtr<UN2CHttpHandlerBase> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, QueuedRequest](
            FHttpRequestPtr InRequest,
            FHttpResponsePtr InResponse,
            bool bWasSuccessful)
        {
            if (UN2CHttpHandlerBase* StrongThis = WeakThis.Get())
            {
                StrongThis->HandleRequestAttemptComplete(
                    QueuedRequest,
                    InRequest,
                    InResponse,
                    bWasSuccessful);
            }
            else
            {
                QueuedRequest->OnComplete.ExecuteIfBound(
                    TEXT("{\"error\": \"HTTP handler was destroyed\"}"));
            }
        });

    if (!Request->ProcessRequest())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to send HTTP request"), TEXT("HttpHandler"));
        HandleRequestAttemptComplete(QueuedRequest, Request, nullptr, false);
        return;
    }

    if (QueuedRequest->RateLimitRetryCount == 0)
    {
        FN2CLogger::Get().Log(TEXT("HTTP request sent successfully"), EN2CLogSeverity::Info, TEXT("HttpHandler"));
    }
    else
    {
        FN2CLogger::Get().Log(
            FString::Printf(
                TEXT("HTTP rate-limit retry %d/%d sent successfully"),
                QueuedRequest->RateLimitRetryCount,
                N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries),
            EN2CLogSeverity::Info,
            TEXT("HttpHandler"));
    }
}

void UN2CHttpHandlerBase::HandleRequestAttemptComplete(
    const TSharedRef<FN2CQueuedHttpRequest>& QueuedRequest,
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    ActiveRequestCount = FMath::Max(0, ActiveRequestCount - 1);

    const int32 ResponseCode = Response.IsValid() ? Response->GetResponseCode() : 0;
    const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();

    if (Response.IsValid() && ResponseCode == 429)
    {
        if (IsPermanentRateLimitFailure(ResponseBody))
        {
            FN2CLogger::Get().LogError(
                TEXT("HTTP 429 is non-retryable because this request exceeds the provider's per-request/token limit; reduce the request or output budget"),
                TEXT("HttpHandler"));
            OnRequestComplete(Request, Response, bWasSuccessful, QueuedRequest->OnComplete);
            PumpRequestQueue();
            return;
        }

        if (QueuedRequest->RateLimitRetryCount < N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries)
        {
            bRateLimitObserved = true;
            const float RetryDelaySeconds = CalculateRateLimitRetryDelay(
                Response,
                ResponseBody,
                QueuedRequest->RateLimitRetryCount);
            ++QueuedRequest->RateLimitRetryCount;

            const double RequestedCooldownUntil = FPlatformTime::Seconds() + RetryDelaySeconds;
            ProviderCooldownUntilSeconds = FMath::Max(
                ProviderCooldownUntilSeconds,
                RequestedCooldownUntil);

            // Put the throttled request back at the front. The provider-wide cooldown prevents
            // this request and all siblings from independently hammering the same token bucket.
            PendingRequestQueue.Insert(QueuedRequest, 0);

            FN2CLogger::Get().LogWarning(
                FString::Printf(
                    TEXT("HTTP 429 rate limit received. Pausing this provider for %.2f seconds; retry %d/%d queued (%d total waiting). Provider concurrency reduced to 1."),
                    RetryDelaySeconds,
                    QueuedRequest->RateLimitRetryCount,
                    N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries,
                    PendingRequestQueue.Num()),
                TEXT("HttpHandler"));

            ScheduleQueuePump(RetryDelaySeconds);
            return;
        }

        FN2CLogger::Get().LogError(
            FString::Printf(
                TEXT("HTTP 429 rate limit persisted after %d coordinated retries; surfacing the provider response"),
                N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries),
            TEXT("HttpHandler"));
    }

    OnRequestComplete(Request, Response, bWasSuccessful, QueuedRequest->OnComplete);
    PumpRequestQueue();
}

void UN2CHttpHandlerBase::ScheduleQueuePump(float DelaySeconds)
{
    if (bQueuePumpScheduled)
    {
        return;
    }

    bQueuePumpScheduled = true;
    TWeakObjectPtr<UN2CHttpHandlerBase> WeakThis(this);
    FTSTicker::GetCoreTicker().AddTicker(
        TEXT("NodeToCode.ProviderRequestQueue"),
        FMath::Max(DelaySeconds, N2CHttpRateLimitRetryPrivate::MinimumRetryDelaySeconds),
        [WeakThis](float)
        {
            if (UN2CHttpHandlerBase* StrongThis = WeakThis.Get())
            {
                StrongThis->bQueuePumpScheduled = false;
                StrongThis->PumpRequestQueue();
            }
            return false;
        });
}

float UN2CHttpHandlerBase::CalculateRateLimitRetryDelay(
    FHttpResponsePtr Response,
    const FString& ResponseBody,
    int32 RateLimitRetryCount) const
{
    using namespace N2CHttpRateLimitRetryPrivate;

    if (Response.IsValid())
    {
        const FString RetryAfter = Response->GetHeader(TEXT("Retry-After")).TrimStartAndEnd();
        if (!RetryAfter.IsEmpty())
        {
            float RetryAfterSeconds = 0.0f;
            if (TryParsePositiveSeconds(RetryAfter, RetryAfterSeconds))
            {
                return AddPositiveJitter(FMath::Clamp(
                    RetryAfterSeconds,
                    MinimumRetryDelaySeconds,
                    MaxServerRetryAfterSeconds));
            }

            FDateTime RetryDate;
            if (FDateTime::ParseHttpDate(RetryAfter, RetryDate))
            {
                const double SecondsUntilRetry = (RetryDate - FDateTime::UtcNow()).GetTotalSeconds();
                if (SecondsUntilRetry > 0.0)
                {
                    return AddPositiveJitter(FMath::Clamp(
                        static_cast<float>(SecondsUntilRetry),
                        MinimumRetryDelaySeconds,
                        MaxServerRetryAfterSeconds));
                }
            }
        }

        const FString RetryAfterMilliseconds =
            Response->GetHeader(TEXT("retry-after-ms")).TrimStartAndEnd();
        const float ParsedMilliseconds = FCString::Atof(*RetryAfterMilliseconds);
        if (ParsedMilliseconds > 0.0f)
        {
            return AddPositiveJitter(FMath::Clamp(
                ParsedMilliseconds / 1000.0f,
                MinimumRetryDelaySeconds,
                MaxServerRetryAfterSeconds));
        }
    }

    float ProviderMessageSeconds = 0.0f;
    if (TryParseRetrySecondsFromProviderMessage(ResponseBody, ProviderMessageSeconds))
    {
        return AddPositiveJitter(FMath::Clamp(
            ProviderMessageSeconds,
            MinimumRetryDelaySeconds,
            MaxServerRetryAfterSeconds));
    }

    const float ExponentialDelay = FMath::Min(
        InitialBackoffSeconds * FMath::Pow(2.0f, static_cast<float>(RateLimitRetryCount)),
        MaxBackoffSeconds);

    return AddPositiveJitter(ExponentialDelay);
}

bool UN2CHttpHandlerBase::IsPermanentRateLimitFailure(const FString& ResponseBody) const
{
    return ResponseBody.Contains(TEXT("Request too large"), ESearchCase::IgnoreCase) ||
           ResponseBody.Contains(TEXT("must be reduced"), ESearchCase::IgnoreCase);
}

bool UN2CHttpHandlerBase::ValidateRequest(const FString& Endpoint, const FString& Payload) const
{
    if (Endpoint.IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("Empty endpoint URL"), TEXT("HttpHandler"));
        return false;
    }

    if (Payload.IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("Empty request payload"), TEXT("HttpHandler"));
        return false;
    }

    return true;
}

void UN2CHttpHandlerBase::OnRequestComplete(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bWasSuccessful,
    FOnLLMResponseReceived OnComplete)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        FString ErrorMsg = TEXT("{\"error\": \"Request failed\"}");
        FN2CLogger::Get().LogError(TEXT("HTTP request failed"), TEXT("HttpHandler"));
        OnComplete.ExecuteIfBound(ErrorMsg);
        OnTranslationResponseReceived.Broadcast(FN2CTranslationResponse(), false);
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseContent = Response->GetContentAsString();

    if (ResponseCode >= 200 && ResponseCode < 300)
    {
        OnComplete.ExecuteIfBound(ResponseContent);
        return;
    }

    FString ErrorMsg;
    if (!ResponseContent.IsEmpty() && ResponseContent.StartsWith(TEXT("{")))
    {
        ErrorMsg = ResponseContent;
    }
    else
    {
        ErrorMsg = FString::Printf(
            TEXT("{\"error\": \"HTTP %d - %s\"}"),
            ResponseCode,
            *ResponseContent);
    }

    FN2CLogger::Get().LogError(
        FString::Printf(
            TEXT("HTTP %d error. Response: %s"),
            ResponseCode,
            *ResponseContent),
        TEXT("HttpHandler"));

    OnComplete.ExecuteIfBound(ErrorMsg);
    OnTranslationResponseReceived.Broadcast(FN2CTranslationResponse(), false);
}
