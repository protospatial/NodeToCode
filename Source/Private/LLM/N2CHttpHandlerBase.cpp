// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CHttpHandlerBase.h"
#include "Utils/N2CLogger.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/DateTime.h"
#include "Misc/LexFromString.h"

namespace N2CHttpRateLimitRetryPrivate
{
constexpr int32 MaxRateLimitRetries = 5;
constexpr float InitialBackoffSeconds = 1.0f;
constexpr float MaxBackoffSeconds = 60.0f;
constexpr float MaxServerRetryAfterSeconds = 300.0f;
constexpr float JitterFraction = 0.20f;
constexpr float MinimumRetryDelaySeconds = 0.10f;

float AddPositiveJitter(float DelaySeconds)
{
    const uint64 Cycles = FPlatformTime::Cycles64();
    const float UnitJitter = static_cast<float>(Cycles % 1000ULL) / 1000.0f;
    return DelaySeconds + (DelaySeconds * JitterFraction * UnitJitter);
}

bool TryParsePositiveSeconds(const FString& Value, float& OutSeconds)
{
    float ParsedSeconds = 0.0f;
    if (!LexTryParseString(ParsedSeconds, *Value) || ParsedSeconds <= 0.0f)
    {
        return false;
    }

    OutSeconds = ParsedSeconds;
    return true;
}
}

void UN2CHttpHandlerBase::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    RequestTimeout = Config.TimeoutSeconds;
}

void UN2CHttpHandlerBase::PostLLMRequest(
    const FString& Endpoint,
    const FString& AuthToken,
    const FString& Payload,
    const FOnLLMResponseReceived& OnComplete)
{
    // Validate request parameters
    if (!ValidateRequest(Endpoint, Payload))
    {
        FN2CLogger::Get().LogError(TEXT("Invalid request parameters"), TEXT("HttpHandler"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Invalid request parameters\"}"));
        return;
    }

    SendRequestAttempt(Endpoint, AuthToken, Payload, OnComplete, 0);
}

void UN2CHttpHandlerBase::SendRequestAttempt(
    const FString& Endpoint,
    const FString& AuthToken,
    const FString& Payload,
    const FOnLLMResponseReceived& OnComplete,
    int32 RateLimitRetryCount)
{
    // Create HTTP request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    // Add authorization if provided
    if (!AuthToken.IsEmpty())
    {
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AuthToken));
    }

    // Add any extra headers
    for (const auto& Header : ExtraHeaders)
    {
        Request->SetHeader(Header.Key, Header.Value);
    }

    Request->SetContentAsString(Payload);
    Request->SetTimeout(RequestTimeout);

    // SetActivityTimeout is only available in UE5.4 and later
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
    Request->SetActivityTimeout(RequestTimeout);
#endif
    
    // Create a weak pointer to this for safety
    TWeakObjectPtr<UN2CHttpHandlerBase> WeakThis(this);

    // Create a lambda to handle the completion and perform shared 429 handling before provider parsing.
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis,
         Endpoint,
         AuthToken,
         Payload,
         OnComplete,
         RateLimitRetryCount](
            FHttpRequestPtr InRequest,
            FHttpResponsePtr InResponse,
            bool bWasSuccessful)
        {
            if (UN2CHttpHandlerBase* StrongThis = WeakThis.Get())
            {
                if (bWasSuccessful &&
                    InResponse.IsValid() &&
                    InResponse->GetResponseCode() == 429 &&
                    StrongThis->TryScheduleRateLimitRetry(
                        Endpoint,
                        AuthToken,
                        Payload,
                        OnComplete,
                        InResponse,
                        RateLimitRetryCount))
                {
                    return;
                }

                StrongThis->OnRequestComplete(InRequest, InResponse, bWasSuccessful, OnComplete);
            }
            else
            {
                // Handler was destroyed, just call completion callback with error
                OnComplete.ExecuteIfBound(TEXT("{\"error\": \"HTTP handler was destroyed\"}"));
            }
        }
    );

    // Send request
    if (!Request->ProcessRequest())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to send HTTP request"), TEXT("HttpHandler"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Failed to send request\"}"));
        return;
    }

    if (RateLimitRetryCount == 0)
    {
        FN2CLogger::Get().Log(TEXT("HTTP request sent successfully"), EN2CLogSeverity::Info, TEXT("HttpHandler"));
    }
    else
    {
        FN2CLogger::Get().Log(
            FString::Printf(
                TEXT("HTTP rate-limit retry %d/%d sent successfully"),
                RateLimitRetryCount,
                N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries),
            EN2CLogSeverity::Info,
            TEXT("HttpHandler"));
    }
}

bool UN2CHttpHandlerBase::TryScheduleRateLimitRetry(
    const FString& Endpoint,
    const FString& AuthToken,
    const FString& Payload,
    const FOnLLMResponseReceived& OnComplete,
    FHttpResponsePtr Response,
    int32 RateLimitRetryCount)
{
    if (RateLimitRetryCount >= N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(
                TEXT("HTTP 429 rate limit persisted after %d retries; surfacing the provider response"),
                N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries),
            TEXT("HttpHandler"));
        return false;
    }

    const float RetryDelaySeconds = CalculateRateLimitRetryDelay(Response, RateLimitRetryCount);
    const int32 NextRetryCount = RateLimitRetryCount + 1;

    FN2CLogger::Get().LogWarning(
        FString::Printf(
            TEXT("HTTP 429 rate limit received. Retrying in %.2f seconds (retry %d/%d)"),
            RetryDelaySeconds,
            NextRetryCount,
            N2CHttpRateLimitRetryPrivate::MaxRateLimitRetries),
        TEXT("HttpHandler"));

    TWeakObjectPtr<UN2CHttpHandlerBase> WeakThis(this);
    FTSTicker::GetCoreTicker().AddTicker(
        TEXT("NodeToCode.Http429Backoff"),
        RetryDelaySeconds,
        [WeakThis,
         Endpoint,
         AuthToken,
         Payload,
         OnComplete,
         NextRetryCount](float)
        {
            if (UN2CHttpHandlerBase* StrongThis = WeakThis.Get())
            {
                StrongThis->SendRequestAttempt(
                    Endpoint,
                    AuthToken,
                    Payload,
                    OnComplete,
                    NextRetryCount);
            }
            else
            {
                OnComplete.ExecuteIfBound(TEXT("{\"error\": \"HTTP handler was destroyed during rate-limit backoff\"}"));
            }

            return false;
        });

    return true;
}

float UN2CHttpHandlerBase::CalculateRateLimitRetryDelay(
    FHttpResponsePtr Response,
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
        float ParsedMilliseconds = 0.0f;
        if (!RetryAfterMilliseconds.IsEmpty() &&
            LexTryParseString(ParsedMilliseconds, *RetryAfterMilliseconds) &&
            ParsedMilliseconds > 0.0f)
        {
            return AddPositiveJitter(FMath::Clamp(
                ParsedMilliseconds / 1000.0f,
                MinimumRetryDelaySeconds,
                MaxServerRetryAfterSeconds));
        }
    }

    const float ExponentialDelay = FMath::Min(
        InitialBackoffSeconds * FMath::Pow(2.0f, static_cast<float>(RateLimitRetryCount)),
        MaxBackoffSeconds);

    return AddPositiveJitter(ExponentialDelay);
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
        const bool bExecuted = OnComplete.ExecuteIfBound(ErrorMsg);
        OnTranslationResponseReceived.Broadcast(FN2CTranslationResponse(), false);
        return;
    }

    // Check response code
    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseContent = Response->GetContentAsString();

    // Handle successful responses (200-299)
    if (ResponseCode >= 200 && ResponseCode < 300)
    {
        // Return successful response
        const bool bExecuted = OnComplete.ExecuteIfBound(ResponseContent);
        return;
    }

    // For 4xx and 5xx responses
    FString ErrorMsg;
    if (!ResponseContent.IsEmpty() && ResponseContent.StartsWith(TEXT("{")))
    {
        // Pass through JSON error response
        ErrorMsg = ResponseContent;
    }
    else
    {
        // Wrap non-JSON error in our format
        ErrorMsg = FString::Printf(
            TEXT("{\"error\": \"HTTP %d - %s\"}"),
            ResponseCode,
            *ResponseContent
        );
    }

    FN2CLogger::Get().LogError(
        FString::Printf(TEXT("HTTP %d error. Response: %s"), 
            ResponseCode,
            *ResponseContent),
        TEXT("HttpHandler")
    );

    const bool bExecuted = OnComplete.ExecuteIfBound(ErrorMsg);
    OnTranslationResponseReceived.Broadcast(FN2CTranslationResponse(), false);
}
