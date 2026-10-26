// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"
#include "Models/N2CTranslation.h"
#include "Interfaces/IHttpRequest.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CCredentialSource.h"
#include "N2CHttpHandlerBase.generated.h"

/**
 * @class UN2CHttpHandlerBase
 * @brief Base class for handling HTTP communication with LLM providers
 */
UCLASS(Abstract)
class NODETOCODE_API UN2CHttpHandlerBase : public UObject
{
    GENERATED_BODY()

public:
    /** Delegate for translation responses */
    FOnTranslationResponseReceived OnTranslationResponseReceived;

    /** Configuration for request timeouts */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    float RequestTimeout = 3600.0f;

    /**
     * Seconds without any data received before the request fails with a Timeout error (FN2CLLMConfig::
     * ActivityTimeoutSeconds, from the HTTP Activity Timeout setting). Lower than RequestTimeout, so a provider that
     * accepts the connection and then stalls fails in minutes rather than after an hour.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    float ActivityTimeout = 300.0f;

    /**
     * Non-secret headers every request of this handler carries (the provider's GetProviderHeaders: anthropic-version,
     * content-type, OpenAI-Organization). Never a credential: a credential header here is dropped when the request is
     * built, and the key comes from the request's FN2CCredential only.
     */
    UPROPERTY()
    TMap<FString, FString> ExtraHeaders;

    /** Initialize with configuration */
    virtual void Initialize(const FN2CLLMConfig& Config);

    /**
     * Core request method. Returns the request's handle: Cancel() on it cancels the HTTP request, and OnComplete runs
     * at most once and never after a successful Cancel(). Errors found before sending complete synchronously, so the
     * returned handle is then already completed.
     *
     * @param Credential the key this one request is sent with, in its provider's one credential header
     *                   (UN2CBaseLLMService::SendRequest resolves it from the credential source right before calling)
     * @param ExistingHandle the handle to deliver through (the service's, created before the credential was
     *                       resolved); a new one when unset. A handle already cancelled sends nothing.
     */
    virtual FN2CLLMRequestHandleRef PostLLMRequest(
        const FString& Endpoint,
        const FN2CCredential& Credential,
        const FString& Payload,
        const FOnLLMResponseReceived& OnComplete,
        FN2CLLMRequestHandlePtr ExistingHandle = nullptr
    );

    /**
     * The headers a request carries, in the order they are set: Content-Type, then Extra without any credential
     * header, then Credential's one header (when it has a key). The payload golden specs use it too.
     */
    static TArray<TPair<FString, FString>> BuildRequestHeaders(const TMap<FString, FString>& Extra, const FN2CCredential& Credential);

    /** True when Headers already carries a credential (Authorization, x-api-key or x-goog-api-key, any case) */
    static bool HasCredentialHeader(const TMap<FString, FString>& Headers);

protected:
    /** Validate request parameters */
    virtual bool ValidateRequest(
        const FString& Endpoint,
        const FString& Payload
    ) const;

    /** Handle request completion */
    virtual void OnRequestComplete(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        FOnLLMResponseReceived OnComplete,
        const FString& SecretToRedact
    );

    /** True when a failed request timed out (total or activity timeout); OutMessage says which, for the user */
    bool IsTimeoutFailure(const FHttpRequestPtr& Request, FString& OutMessage) const;

    /** Current configuration */
    FN2CLLMConfig Config;
};
