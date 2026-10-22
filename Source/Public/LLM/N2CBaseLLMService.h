// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "N2CBaseLLMService.generated.h"

// Forward declarations
class UN2CHttpHandlerBase;
class UN2CResponseParserBase;
class UN2CSystemPromptManager;

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

    /** Exact provider-specific POST body most recently produced by SendRequest. */
    const FString& GetLastFormattedRequestPayload() const { return LastFormattedRequestPayload; }

    /** Build the same final provider request body used by SendRequest without dispatching it. */
    FString BuildFormattedRequestPayload(
        const FString& JsonPayload,
        const FString& SystemMessage);

    /**
     * Format already-prepared semantic messages for this provider without rebuilding the original
     * custom/ad-hoc prompt content. Used by cross-provider request retries.
     */
    FString BuildFormattedRequestPayloadFromPreparedMessages(
        const FString& UserMessage,
        const FString& SystemMessage);

    /** Replay an already formatted provider request body without rebuilding prompts/context. */
    void ResendFormattedRequest(
        const FString& FormattedPayload,
        const FOnLLMResponseReceived& OnComplete);
    
    // Provider-specific methods (must be implemented by derived classes)
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken,
                              bool& OutSupportsSystemPrompts) override { OutEndpoint = TEXT(""); OutAuthToken = TEXT(""); OutSupportsSystemPrompts = false; }
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::Anthropic; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override { }

protected:
    // Common functionality for derived classes
    virtual void InitializeComponents();
    
    // Virtual methods for provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const { return TEXT("{}"); }
    virtual UN2CResponseParserBase* CreateResponseParser() { return nullptr; }
    virtual FString GetDefaultEndpoint() const { return TEXT(""); }

    // Common protected members
    FN2CLLMConfig Config;
    FString LastFormattedRequestPayload;
    
    UPROPERTY()
    UN2CHttpHandlerBase* HttpHandler;
    
    UPROPERTY()
    UN2CResponseParserBase* ResponseParser;
    
    UPROPERTY()
    UN2CSystemPromptManager* PromptManager;
    
    bool bIsInitialized;
};
