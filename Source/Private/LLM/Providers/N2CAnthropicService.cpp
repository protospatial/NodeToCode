// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CAnthropicService.h"

#include "Core/N2CSettings.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2CAnthropicService::CreateResponseParser()
{
    UN2CAnthropicResponseParser* Parser = NewObject<UN2CAnthropicResponseParser>(this);
    return Parser;
}

void UN2CAnthropicService::GetConfiguration(
    FString& OutEndpoint,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutSupportsSystemPrompts = true;  // Anthropic supports system prompts
}

void UN2CAnthropicService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    // The key goes in x-api-key, added per request from the resolved credential (FN2CCredential)
    OutHeaders.Add(TEXT("anthropic-version"), ApiVersion);
    OutHeaders.Add(TEXT("content-type"), TEXT("application/json"));
}

FString UN2CAnthropicService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Prompts are logged in full only with n2c.LogLLMPayloads 1, else their sizes
    FN2CLogger::Get().LogLLMContent(TEXT("LLM System Message"), TEXT("LLM system message"), SystemMessage);
    FN2CLogger::Get().LogLLMContent(TEXT("LLM User Message"), TEXT("LLM user message"), UserMessage);

    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForAnthropic();

    // No temperature: the builder sends sampling parameters only for registered models that accept them
    // (Claude Opus 4.7 and later reject them).

    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    PayloadBuilder->AddSystemMessage(SystemMessage);

    PayloadBuilder->AddUserMessage(FinalUserMessage);

    // max_tokens is required by the Messages API, so Auto always sends one (see ResolveMaxOutputTokens)
    PayloadBuilder->SetOutputBudget(Config.MaxOutputTokens,
        FN2CTokenEstimationService::EstimateTokensForChars(SystemMessage.Len() + FinalUserMessage.Len()));

    // Build and return the payload
    return PayloadBuilder->Build();
}
