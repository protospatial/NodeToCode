// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.h"
#include "Models/N2CTranslation.h"
#include "LLM/N2CResponseParserBase.h"
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

    /** Send N2C JSON to LLM and receive translation response */
    virtual void SendRequest(
        const FString& JsonPayload,
        const FString& SystemMessage,
        const FOnLLMResponseReceived& OnComplete
    ) = 0;

    /** Get service-specific configuration */
    virtual void GetConfiguration(
        FString& OutEndpoint,
        FString& OutAuthToken,
        bool& OutSupportsSystemPrompts
    ) = 0;

    /** Get the provider type */
    virtual EN2CLLMProvider GetProviderType() const = 0;

    /** Check if service is properly initialized */
    virtual bool IsInitialized() const = 0;

    /** Get any provider-specific headers needed */
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const = 0;

    /** Get the response parser */
    virtual UN2CResponseParserBase* GetResponseParser() const = 0;
    
};
