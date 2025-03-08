// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/Providers/N2CAnthropicResponseParser.h"
#include "N2CAnthropicService.generated.h"

// Forward declarations
class UN2CSystemPromptManager;

/**
 * @class UN2CAnthropicService
 * @brief Implementation of Anthropic's Claude API integration
 */
UCLASS()
class NODETOCODE_API UN2CAnthropicService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::Anthropic; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return TEXT("https://api.anthropic.com/v1/messages"); }

private:
    /** API version header value */
    FString ApiVersion = TEXT("2023-06-01");
};
