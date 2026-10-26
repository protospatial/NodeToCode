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
 * @brief Implementation of OpenAI's Chat Completion and Responses API integration
 */
UCLASS()
class NODETOCODE_API UN2COpenAIService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::OpenAI; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override;

private:
    /** Check if the current model requires the Responses API */
    bool RequiresResponsesAPI() const;

    /** Format payload for the Responses API (used by GPT-5.2 Pro) */
    FString FormatResponsesAPIPayload(const FString& UserMessage, const FString& SystemMessage, bool bSupportsSystemPrompts) const;

    /** Format payload for the Chat Completions API (standard models) */
    FString FormatChatCompletionsPayload(const FString& UserMessage, const FString& SystemMessage, bool bSupportsSystemPrompts) const;

    /** API version header value */
    FString ApiVersion = TEXT("2024-01-25");

    /** Organization ID (optional) */
    FString OrganizationId;

    /** Chat Completions endpoint */
    static constexpr const TCHAR* ChatCompletionsEndpoint = TEXT("https://api.openai.com/v1/chat/completions");

    /** Responses API endpoint */
    static constexpr const TCHAR* ResponsesAPIEndpoint = TEXT("https://api.openai.com/v1/responses");
};
