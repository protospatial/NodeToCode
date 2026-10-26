// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CHttpHandlerBase.h"
#include "Utils/N2CLogger.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

void UN2CHttpHandlerBase::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
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

    // Create HTTP request
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("POST"));

    // The provider's non-secret headers, then the key in its one credential header (Anthropic x-api-key, Gemini
    // x-goog-api-key, the others Authorization)
    for (const TPair<FString, FString>& Header : BuildRequestHeaders(ExtraHeaders, Credential))
    {
        Request->SetHeader(Header.Key, Header.Value);
    }

    Request->SetContentAsString(Payload);
    Request->SetTimeout(RequestTimeout);

    // A request that receives nothing for ActivityTimeout seconds fails (UE 5.4+; 5.6 is the floor)
    Request->SetActivityTimeout(ActivityTimeout);
    
    // Create a weak pointer to this for safety
    TWeakObjectPtr<UN2CHttpHandlerBase> WeakThis(this);

    // The completion runs on the game thread. The handle gates OnComplete: once Cancel() has won, the cancelled
    // request's failure completion is dropped here, before any logging or broadcast. The lambda holds the handle
    // strongly; the handle holds the request only weakly, so there is no cycle.
    // The completion keeps this request's key only to redact it from an error body
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, OnComplete, Handle, SecretToRedact = Credential.Value](FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bWasSuccessful)
        {
            if (Handle->IsFinished())
            {
                return;
            }

            const FOnLLMResponseReceived GatedOnComplete = Handle->Wrap(OnComplete);

            if (UN2CHttpHandlerBase* StrongThis = WeakThis.Get())
            {
                StrongThis->OnRequestComplete(InRequest, InResponse, bWasSuccessful, GatedOnComplete, SecretToRedact);
            }
            else
            {
                // Handler was destroyed, just call completion callback with error
                GatedOnComplete.ExecuteIfBound(FN2CTranslationError::MakeErrorJson(TEXT("HTTP handler was destroyed")));
            }
        }
    );

    Handle->SetHttpRequest(Request);

    // Send request
    if (!Request->ProcessRequest())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to send HTTP request"), TEXT("HttpHandler"));
        // A failed ProcessRequest may also fire the completion delegate; the handle keeps OnComplete to one call
        Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("Failed to send request"),
            EN2CTranslationErrorKind::Network));
        return Handle;
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("HTTP request sent to %s (%s), %d characters"),
        *UEnum::GetDisplayValueAsText(Config.Provider).ToString(), *Config.Model, Payload.Len()), EN2CLogSeverity::Info, TEXT("HttpHandler"));
    return Handle;
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
    FOnLLMResponseReceived OnComplete,
    const FString& SecretToRedact)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        // No HTTP response at all: connection refused, DNS, TLS or timeout
        const FString Reason = Request.IsValid() ? FString(LexToString(Request->GetFailureReason())) : FString(TEXT("Unknown"));
        FString TimeoutMessage;
        if (IsTimeoutFailure(Request, TimeoutMessage))
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("HTTP request failed (%s): %s"), *Reason, *TimeoutMessage), TEXT("HttpHandler"));
            OnComplete.ExecuteIfBound(FN2CTranslationError::MakeErrorJson(TimeoutMessage, EN2CTranslationErrorKind::Timeout));
            return;
        }
        FN2CLogger::Get().LogError(FString::Printf(TEXT("HTTP request failed (%s)"), *Reason), TEXT("HttpHandler"));
        OnComplete.ExecuteIfBound(FN2CTranslationError::MakeErrorJson(
            FString::Printf(TEXT("No response from the provider (%s)"), *Reason), EN2CTranslationErrorKind::Network));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseContent = Response->GetContentAsString();

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
    FN2CLogger::Get().LogError(FString::Printf(TEXT("HTTP %d error from %s (%s), %d characters"), ResponseCode,
        *ProviderName, *Config.Model, ResponseContent.Len()), TEXT("HttpHandler"));
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

bool UN2CHttpHandlerBase::IsTimeoutFailure(const FHttpRequestPtr& Request, FString& OutMessage) const
{
    if (!Request.IsValid())
    {
        return false;
    }
    const EHttpFailureReason Failure = Request->GetFailureReason();
    const float Elapsed = Request->GetElapsedTime();
    if (Failure == EHttpFailureReason::TimedOut)
    {
        OutMessage = FString::Printf(TEXT("The request ran past its total timeout of %.0f s (%.0f s elapsed)"), RequestTimeout, Elapsed);
        return true;
    }
    // UE reports an activity timeout as ConnectionError (FHttpRequestCommon::HandleRequestFailed), so tell it from a
    // refused or dropped connection by how long the request ran: a connection failure is immediate, an activity
    // timeout takes at least ActivityTimeout seconds of silence
    if (Failure == EHttpFailureReason::ConnectionError && ActivityTimeout > 0.0f && Elapsed >= ActivityTimeout * 0.95f)
    {
        OutMessage = FString::Printf(TEXT("No data from the provider for %.0f s (HTTP Activity Timeout). Raise Project Settings > Plugins > Node to Code > LLM Provider > HTTP Activity Timeout for very long translations or slow local models."), ActivityTimeout);
        return true;
    }
    return false;
}
