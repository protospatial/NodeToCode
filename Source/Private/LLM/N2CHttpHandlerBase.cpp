// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CHttpHandlerBase.h"
#include "Utils/N2CLogger.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

void UN2CHttpHandlerBase::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    RequestTimeout = Config.TimeoutSeconds;
}

FN2CLLMRequestHandleRef UN2CHttpHandlerBase::PostLLMRequest(
    const FString& Endpoint,
    const FString& AuthToken,
    const FString& Payload,
    const FOnLLMResponseReceived& OnComplete)
{
    // Every path, including the early errors, delivers OnComplete through the handle so it runs at most once
    FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();

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

    // The completion runs on the game thread. The handle gates OnComplete: once Cancel() has won, the cancelled
    // request's failure completion is dropped here, before any logging or broadcast. The lambda holds the handle
    // strongly; the handle holds the request only weakly, so there is no cycle.
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, OnComplete, Handle](FHttpRequestPtr InRequest, FHttpResponsePtr InResponse, bool bWasSuccessful)
        {
            if (Handle->IsFinished())
            {
                return;
            }

            const FOnLLMResponseReceived GatedOnComplete = Handle->Wrap(OnComplete);

            if (UN2CHttpHandlerBase* StrongThis = WeakThis.Get())
            {
                StrongThis->OnRequestComplete(InRequest, InResponse, bWasSuccessful, GatedOnComplete);
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

    FN2CLogger::Get().Log(TEXT("HTTP request sent successfully"), EN2CLogSeverity::Info, TEXT("HttpHandler"));
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
    FOnLLMResponseReceived OnComplete)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        // No HTTP response at all: connection refused, DNS, TLS or timeout
        const FString Reason = Request.IsValid() ? FString(LexToString(Request->GetFailureReason())) : FString(TEXT("Unknown"));
        FN2CLogger::Get().LogError(FString::Printf(TEXT("HTTP request failed (%s)"), *Reason), TEXT("HttpHandler"));
        OnComplete.ExecuteIfBound(FN2CTranslationError::MakeErrorJson(
            FString::Printf(TEXT("No response from the provider (%s)"), *Reason), EN2CTranslationErrorKind::Network));
        return;
    }

    const int32 ResponseCode = Response->GetResponseCode();
    const FString ResponseContent = Response->GetContentAsString();

    // Successful responses (200-299) go to the parser as they are
    if (ResponseCode >= 200 && ResponseCode < 300)
    {
        OnComplete.ExecuteIfBound(ResponseContent);
        return;
    }

    // 4xx and 5xx: the provider's JSON error gains the status; any other body (an HTML proxy page, plain text) is
    // wrapped as escaped JSON, so the parser always gets valid JSON and can report the status and the real message
    FN2CLogger::Get().LogError(
        FString::Printf(TEXT("HTTP %d error. Response: %s"), ResponseCode, *ResponseContent.Left(2000)),
        TEXT("HttpHandler")
    );

    OnComplete.ExecuteIfBound(FN2CTranslationError::WrapHttpErrorBody(ResponseCode, ResponseContent));
}
