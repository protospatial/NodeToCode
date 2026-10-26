// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CCredentialSource.h"
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

    /**
     * Resolves the provider's credential from the credential source, then sends the request with it: the key is read
     * for every request, so a key changed since Initialize (in Project Settings, the environment or the source) is
     * the one the next request uses. A cloud provider without a key fails with an Auth error before anything is sent.
     */
    virtual FN2CLLMRequestHandleRef SendRequest(const FString& JsonPayload, const FString& SystemMessage,
                           const FOnLLMResponseReceived& OnComplete) override;
    virtual bool IsInitialized() const override { return bIsInitialized; }
    virtual UN2CResponseParserBase* GetResponseParser() const override { return ResponseParser; }
    
    // Provider-specific methods (must be implemented by derived classes)
    virtual void GetConfiguration(FString& OutEndpoint, bool& OutSupportsSystemPrompts) override { OutEndpoint = TEXT(""); OutSupportsSystemPrompts = false; }
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::Anthropic; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override { }

    /**
     * The error SendRequest reports instead of sending when Provider needs an API key and ApiKey is empty: it names
     * the provider and where to set the key (IN2CCredentialSource::MakeMissingKeyMessage). Empty when no key is
     * needed or one is set.
     */
    static FString GetMissingApiKeyError(EN2CLLMProvider Provider, const FString& ApiKey);

    /** The source this service resolves each request's key from: the one set here, else IN2CCredentialSource::Get() */
    TSharedRef<const IN2CCredentialSource> GetCredentialSource() const;

    /** Resolve this service's keys from Source (tests, integrations); null goes back to IN2CCredentialSource::Get() */
    void SetCredentialSource(TSharedPtr<const IN2CCredentialSource> Source) { CredentialSource = MoveTemp(Source); }

    /** Builds the request body this service would send, without sending it (used by the payload automation tests) */
    FString BuildRequestPayloadForTesting(const FString& UserMessage, const FString& SystemMessage) const
    {
        return FormatRequestPayload(UserMessage, SystemMessage);
    }

protected:
    // Common functionality for derived classes
    virtual void InitializeComponents();
    
    // Virtual methods for provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const { return TEXT("{}"); }
    virtual UN2CResponseParserBase* CreateResponseParser() { return nullptr; }
    virtual FString GetDefaultEndpoint() const { return TEXT(""); }

    /** Sends the request with a resolved credential, through Handle (SendRequest's) */
    void SendWithCredential(const FN2CCredential& Credential, const FString& JsonPayload, const FString& SystemMessage,
        const FOnLLMResponseReceived& OnComplete, const FN2CLLMRequestHandleRef& Handle);

    // Common protected members
    FN2CLLMConfig Config;

    /** Set by SetCredentialSource; unset means IN2CCredentialSource::Get() */
    TSharedPtr<const IN2CCredentialSource> CredentialSource;

    UPROPERTY()
    UN2CHttpHandlerBase* HttpHandler;
    
    UPROPERTY()
    UN2CResponseParserBase* ResponseParser;
    
    UPROPERTY()
    UN2CSystemPromptManager* PromptManager;
    
    bool bIsInitialized;
};
