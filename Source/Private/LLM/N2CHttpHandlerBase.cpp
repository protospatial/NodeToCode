// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CHttpHandlerBase.h"
#include "Utils/N2CLogger.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Containers/Ticker.h"
#include "Math/UnrealMathUtility.h"

void UN2CHttpHandlerBase::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    RetryPolicy.MaxRetries = FMath::Clamp(Config.MaxRetries, 0, 10);
    RequestTimeout = Config.TimeoutSeconds;
    ActivityTimeout = Config.ActivityTimeoutSeconds > 0.0f ? FMath::Min(Config.ActivityTimeoutSeconds, RequestTimeout) : RequestTimeout;
}

TArray<TPair<FString, FString>> UN2CHttpHandlerBase::BuildRequestHeaders(const TMap<FString, FString>& Extra,
    const FN2CCredential& Credential)
{
    TArray<TPair<FString, FString>> Headers;
    Headers.Emplace(TEXT("Content-Type"), TEXT("application/json"));
    for (const TPair<FString, FString>& Header : Extra)
    {
        // The key comes from the request's credential only, so it is sent in exactly one header
        if (!FN2CCredential::IsCredentialHeader(Header.Key))
        {
            Headers.Emplace(Header.Key, Header.Value);
        }
    }
    FString Name, Value;
    if (Credential.GetHeader(Name, Value))
    {
        Headers.Emplace(MoveTemp(Name), MoveTemp(Value));
    }
    return Headers;
}

FN2CLLMRequestHandleRef UN2CHttpHandlerBase::PostLLMRequest(
    const FString& Endpoint,
    const FN2CCredential& Credential,
    const FString& Payload,
    const FOnLLMResponseReceived& OnComplete,
    FN2CLLMRequestHandlePtr ExistingHandle)
{
    // Every path, including the early errors, delivers OnComplete through the handle so it runs at most once
    FN2CLLMRequestHandleRef Handle = ExistingHandle.IsValid() ? ExistingHandle.ToSharedRef() : FN2CLLMRequestHandle::Create();

    // Cancelled while the credential was being resolved: nothing is sent
    if (Handle->IsFinished())
    {
        return Handle;
    }

    // Validate request parameters
    if (!ValidateRequest(Endpoint, Payload))
    {
        FN2CLogger::Get().LogError(TEXT("Invalid request parameters"), TEXT("HttpHandler"));
        Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("Invalid request parameters"),
            EN2CTranslationErrorKind::InvalidRequest));
        return Handle;
    }

    // The provider's non-secret headers, then the key in its one credential header (Anthropic x-api-key, Gemini
    // x-goog-api-key, the others Authorization)
    FN2CHttpRequestSpec Request;
    Request.Url = Endpoint;
    Request.Headers = BuildRequestHeaders(ExtraHeaders, Credential);
    Request.Body = Payload;
    Request.TimeoutSeconds = RequestTimeout;
    Request.ActivityTimeoutSeconds = ActivityTimeout;

    FN2CLogger::Get().Log(FString::Printf(TEXT("HTTP request sent to %s (%s), %d characters"),
        *UEnum::GetDisplayValueAsText(Config.Provider).ToString(), *Config.Model, Payload.Len()), EN2CLogSeverity::Info, TEXT("HttpHandler"));

    // The completion keeps this request's key only to redact it from an error body
    SendAttempt(Request, Handle, OnComplete, Credential.Value, 0);
    return Handle;
}

void UN2CHttpHandlerBase::SendAttempt(const FN2CHttpRequestSpec& Request, const FN2CLLMRequestHandleRef& Handle,
    const FOnLLMResponseReceived& OnComplete, const FString& SecretToRedact, int32 RetryIndex)
{
    if (Handle->IsFinished())
    {
        return;
    }
    const TSharedRef<IN2CHttpTransport> Sender = Transport.IsValid() ? Transport.ToSharedRef() : FN2CHttpModuleTransport::Get();
    TWeakObjectPtr<UN2CHttpHandlerBase> WeakThis(this);

    // The handle gates OnComplete: once Cancel() has won, a late answer is dropped before any logging or broadcast
    Sender->Send(Request, Handle, [WeakThis, Request, Handle, OnComplete, SecretToRedact, RetryIndex](const FN2CHttpResponseData& Response)
    {
        if (Handle->IsFinished())
        {
            return;
        }
        UN2CHttpHandlerBase* StrongThis = WeakThis.Get();
        if (!StrongThis)
        {
            // Handler was destroyed, just call completion callback with error
            Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("HTTP handler was destroyed")));
            return;
        }

        // Rate limited or overloaded: wait as the provider asks (or back off) and send the same request again
        const float Delay = StrongThis->RetryPolicy.GetDelaySeconds(RetryIndex, Response, FMath::FRand(), FDateTime::UtcNow());
        if (Delay >= 0.0f)
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("HTTP %d from %s (%s): retrying in %.1f s (retry %d of %d)"),
                Response.Status, *UEnum::GetDisplayValueAsText(StrongThis->Config.Provider).ToString(), *StrongThis->Config.Model,
                Delay, RetryIndex + 1, StrongThis->RetryPolicy.MaxRetries), TEXT("HttpHandler"));
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                [WeakThis, Request, Handle, OnComplete, SecretToRedact, RetryIndex](float)
                {
                    if (UN2CHttpHandlerBase* Handler = WeakThis.Get())
                    {
                        Handler->SendAttempt(Request, Handle, OnComplete, SecretToRedact, RetryIndex + 1);
                    }
                    else if (!Handle->IsFinished())
                    {
                        Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("HTTP handler was destroyed")));
                    }
                    return false;
                }), Delay);
            return;
        }
        StrongThis->HandleResponse(Response, Handle->Wrap(OnComplete), SecretToRedact, RetryIndex + 1);
    });
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

void UN2CHttpHandlerBase::HandleResponse(const FN2CHttpResponseData& Response, const FOnLLMResponseReceived& OnComplete,
    const FString& SecretToRedact, int32 Attempts)
{
    if (!Response.bReceived)
    {
        // No HTTP response at all: connection refused, DNS, TLS or timeout
        FN2CLogger::Get().LogError(FString::Printf(TEXT("HTTP request failed: %s"), *Response.FailureMessage), TEXT("HttpHandler"));
        OnComplete.ExecuteIfBound(FN2CTranslationError::MakeErrorJson(Response.FailureMessage,
            Response.bTimedOut ? EN2CTranslationErrorKind::Timeout : EN2CTranslationErrorKind::Network));
        return;
    }

    const int32 ResponseCode = Response.Status;
    const FString& ResponseContent = Response.Body;

    // Provider, model, status and size only; the body is the model's answer (or the provider's error), logged in
    // full only with n2c.LogLLMPayloads 1
    const FString ProviderName = UEnum::GetDisplayValueAsText(Config.Provider).ToString();

    // Successful responses (200-299) go to the parser as they are
    if (ResponseCode >= 200 && ResponseCode < 300)
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("Response from %s (%s): HTTP %d, %d characters"), *ProviderName,
            *Config.Model, ResponseCode, ResponseContent.Len()), EN2CLogSeverity::Info, TEXT("HttpHandler"));
        OnComplete.ExecuteIfBound(ResponseContent);
        return;
    }

    // An error body that echoes the key (a misbehaving proxy, a provider debug echo) must not carry it into the log,
    // the typed error, a saved translation folder or BatchSummary.json
    const FString SafeContent = FN2CLogger::RedactSecret(ResponseContent, SecretToRedact);

    // 4xx and 5xx: the provider's JSON error gains the status; any other body (an HTML proxy page, plain text) is
    // wrapped as escaped JSON, so the parser always gets valid JSON and can report the status and the real message
    // (the parser logs that message as the translation error)
    const FString Tries = Attempts > 1 ? FString::Printf(TEXT(" after %d attempts"), Attempts) : FString();
    FN2CLogger::Get().LogError(FString::Printf(TEXT("HTTP %d error from %s (%s)%s, %d characters"), ResponseCode,
        *ProviderName, *Config.Model, *Tries, ResponseContent.Len()), TEXT("HttpHandler"));
    FN2CLogger::Get().LogLLMContent(TEXT("HTTP error body"), TEXT("HTTP error body"), SafeContent.Left(2000), TEXT("HttpHandler"));

    OnComplete.ExecuteIfBound(FN2CTranslationError::WrapHttpErrorBody(ResponseCode, SafeContent));
}

bool UN2CHttpHandlerBase::HasCredentialHeader(const TMap<FString, FString>& Headers)
{
    for (const TPair<FString, FString>& Header : Headers)
    {
        if (FN2CCredential::IsCredentialHeader(Header.Key))
        {
            return true;
        }
    }
    return false;
}
