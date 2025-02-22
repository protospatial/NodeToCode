// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/IN2CLLMService.h"
#include "N2CGeminiResponseParser.h"
#include "N2CGeminiService.generated.h"

/**
 * @class UN2CGeminiService
 * @brief Implementation of Gemini's API integration
 */
UCLASS()
class NODETOCODE_API UN2CGeminiService : public UObject, public IN2CLLMService
{
    GENERATED_BODY()

public:
    // Begin IN2CLLMService Interface
    virtual bool Initialize(const FN2CLLMConfig& Config) override;
    virtual void SendRequest(const FString& JsonPayload, const FString& SystemMessage, const FOnLLMResponseReceived& OnComplete) override;
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::Gemini; }
    virtual bool IsInitialized() const override { return bIsInitialized; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;
    virtual UN2CResponseParserBase* GetResponseParser() const override { return ResponseParser; }
    // End IN2CLLMService Interface

private:
    /** Format the request payload for Gemini's API */
    FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const;

    /** Current configuration */
    FN2CLLMConfig Config;

    /** HTTP handler for requests */
    UPROPERTY()
    class UN2CHttpHandlerBase* HttpHandler;

    /** Response parser */
    UPROPERTY()
    class UN2CGeminiResponseParser* ResponseParser;

    /** System prompt manager */
    UPROPERTY()
    class UN2CSystemPromptManager* PromptManager;

    /** Initialization state */
    bool bIsInitialized;

    /** Default API endpoint to use */
    FString DefaultEndpoint = TEXT("https://generativelanguage.googleapis.com/v1beta/models/");

    /** Default model to use */
    FString DefaultModel = TEXT("gemini-2.0-pro-exp-02-05");
    
};
