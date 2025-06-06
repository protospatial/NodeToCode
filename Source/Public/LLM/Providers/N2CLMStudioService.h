// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2CLMStudioResponseParser.h"
#include "N2CLMStudioService.generated.h"

// Forward declarations
class UN2CSystemPromptManager;

/**
 * @class UN2CLMStudioService
 * @brief Implementation of LM Studio's local LLM API integration
 * 
 * LM Studio provides an OpenAI-compatible REST API for local models.
 * This service supports structured output via JSON schema for reliable parsing.
 */
UCLASS()
class NODETOCODE_API UN2CLMStudioService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Override Initialize to handle LM Studio-specific setup
    virtual bool Initialize(const FN2CLLMConfig& InConfig) override;
    
    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::LMStudio; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return TEXT("http://localhost:1234/v1/chat/completions"); }

private:
    /** LM Studio endpoint from settings */
    FString LMStudioEndpoint;
};