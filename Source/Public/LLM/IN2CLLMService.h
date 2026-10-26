// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.h"
#include "Models/N2CTranslation.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "IN2CLLMService.generated.h"

/**
 * @class UN2CLLMService
 * @brief Interface for LLM service providers
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UN2CLLMService : public UInterface
{
    GENERATED_BODY()
};

/**
 * @class IN2CLLMService
 * @brief Interface defining the contract for LLM service providers
 */
class NODETOCODE_API IN2CLLMService
{
    GENERATED_BODY()

public:
    /** Initialize the service with configuration */
    virtual bool Initialize(const FN2CLLMConfig& Config) = 0;

    /**
     * Send N2C JSON to LLM and receive translation response. Returns the request's handle (see FN2CLLMRequestHandle):
     * Cancel() stops the HTTP request and OnComplete; callers that do not cancel can ignore it.
     */
    virtual FN2CLLMRequestHandleRef SendRequest(
        const FString& JsonPayload,
        const FString& SystemMessage,
        const FOnLLMResponseReceived& OnComplete
    ) = 0;

    /**
     * Get service-specific configuration: the endpoint requests go to and whether the model takes a system prompt.
     * Credentials are not configuration: each request resolves its key from the credential source when it is sent
     * (IN2CCredentialSource, UN2CBaseLLMService::SendRequest).
     */
    virtual void GetConfiguration(
        FString& OutEndpoint,
        bool& OutSupportsSystemPrompts
    ) = 0;

    /** Get the provider type */
    virtual EN2CLLMProvider GetProviderType() const = 0;

    /** Check if service is properly initialized */
    virtual bool IsInitialized() const = 0;

    /**
     * Get the provider's non-secret headers (anthropic-version, content-type, OpenAI-Organization). Never a key: the
     * credential header is added per request from the resolved FN2CCredential.
     */
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const = 0;

    /** Get the response parser */
    virtual UN2CResponseParserBase* GetResponseParser() const = 0;
    
};
