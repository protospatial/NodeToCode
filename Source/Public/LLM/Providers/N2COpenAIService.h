// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2COpenAIResponseParser.h"
#include "N2COpenAIService.generated.h"

// Forward declarations
class UN2CSystemPromptManager;

/**
 * @class UN2COpenAIService
 * @brief Implementation of OpenAI's Chat Completion API integration
 */
UCLASS()
class NODETOCODE_API UN2COpenAIService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::OpenAI; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return TEXT("https://api.openai.com/v1/chat/completions"); }

private:
    /** API version header value */
    FString ApiVersion = TEXT("2024-01-25");

    /** Organization ID (optional) */
    FString OrganizationId;
};
