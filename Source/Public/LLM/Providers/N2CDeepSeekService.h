// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2CDeepSeekResponseParser.h"
#include "N2CDeepSeekService.generated.h"

// Forward declarations
class UN2CSystemPromptManager;

/**
 * @class UN2CDeepSeekService
 * @brief Implementation of DeepSeek's API integration
 */
UCLASS()
class NODETOCODE_API UN2CDeepSeekService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::DeepSeek; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return TEXT("https://api.deepseek.com/chat/completions"); }
};
