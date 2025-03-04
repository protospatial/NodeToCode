// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/IN2CLLMService.h"
#include "N2CBaseLLMService.generated.h"

/**
 * @class UN2CBaseLLMService
 * @brief Abstract base class implementing common functionality for LLM services
 */
UCLASS(Abstract)
class NODETOCODE_API UN2CBaseLLMService : public UObject, public IN2CLLMService
{
    GENERATED_BODY()

public:
    // Common implementations from IN2CLLMService
    virtual bool Initialize(const FN2CLLMConfig& InConfig) override;
    virtual void SendRequest(const FString& JsonPayload, const FString& SystemMessage, 
                           const FOnLLMResponseReceived& OnComplete) override;
    virtual bool IsInitialized() const override { return bIsInitialized; }
    virtual UN2CResponseParserBase* GetResponseParser() const override { return ResponseParser; }
    
    // Provider-specific methods (must be implemented by derived classes)
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, 
                              bool& OutSupportsSystemPrompts) override = 0;
    virtual EN2CLLMProvider GetProviderType() const override = 0;
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override = 0;

protected:
    // Common functionality for derived classes
    virtual void InitializeComponents();
    
    // Abstract methods for provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const = 0;
    virtual UN2CResponseParserBase* CreateResponseParser() = 0;
    virtual FString GetDefaultEndpoint() const = 0;

    // Common protected members
    FN2CLLMConfig Config;
    
    UPROPERTY()
    UN2CHttpHandlerBase* HttpHandler;
    
    UPROPERTY()
    UN2CResponseParserBase* ResponseParser;
    
    UPROPERTY()
    UN2CSystemPromptManager* PromptManager;
    
    bool bIsInitialized;
};
