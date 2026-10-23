// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CGeminiService.h"

#include "Core/N2CSettings.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2CGeminiService::CreateResponseParser()
{
    UN2CGeminiResponseParser* Parser = NewObject<UN2CGeminiResponseParser>(this);
    return Parser;
}

void UN2CGeminiService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    // Generative Language API. The key goes in the x-goog-api-key header (GetProviderHeaders), never in the
    // URL: UE's HTTP module logs full URLs on timeouts and failures.
    OutEndpoint = FString::Printf(TEXT("%s%s:generateContent"), *Config.ApiEndpoint, *Config.Model);
    OutAuthToken = TEXT("");  // Gemini takes the key in x-goog-api-key, not a Bearer header

    // Default to supporting system prompts since all Gemini models currently support system prompts
    OutSupportsSystemPrompts = true;
}

void UN2CGeminiService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));

    if (!Config.ApiKey.IsEmpty())
{
        OutHeaders.Add(TEXT("x-goog-api-key"), Config.ApiKey);
    }
}

FString UN2CGeminiService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForGemini();

    // Temperature is applied by ConfigureForGemini only where the registry allows it

    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Add system message and user message
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalUserMessage);

    // generationConfig.maxOutputTokens from the Gemini Max Output Tokens setting (see ResolveMaxOutputTokens)
    PayloadBuilder->SetOutputBudget(Config.MaxOutputTokens,
        FN2CTokenEstimationService::EstimateTokensForChars(SystemMessage.Len() + FinalUserMessage.Len()));

    // Add JSON schema for response format if model supports it
    if (Config.Model != TEXT("gemini-2.0-flash-thinking-exp-01-21"))
    {
        PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    }

    return PayloadBuilder->Build();
}
