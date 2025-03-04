// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2COllamaResponseParser.h"
#include "LLM/N2COllamaConfig.h"
#include "N2COllamaService.generated.h"

// Forward declarations
class UN2CSystemPromptManager;

/**
 * @class UN2COllamaService
 * @brief Implementation of Ollama's local LLM API integration
 */
UCLASS()
class NODETOCODE_API UN2COllamaService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Override Initialize to handle Ollama-specific setup
    virtual bool Initialize(const FN2CLLMConfig& InConfig) override;
    
    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::Ollama; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return TEXT("http://localhost:11434/api/chat"); }

private:
    /** Ollama configuration settings */
    FN2COllamaConfig OllamaConfig;
};
