// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CDeepSeekService.h"

#include "Core/Services/N2CTokenEstimationService.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2CDeepSeekService::CreateResponseParser()
{
    UN2CDeepSeekResponseParser* Parser = NewObject<UN2CDeepSeekResponseParser>(this);
    return Parser;
}

void UN2CDeepSeekService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    OutSupportsSystemPrompts = true;  // DeepSeek always supports system prompts
}

void UN2CDeepSeekService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2CDeepSeekService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Everything below is decided from the request (Config.Model and Config.ReasoningEffort) through the registry,
    // never from the Project Settings selection, so an MCP model override gets its own model's mode
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForDeepSeek(Config.ReasoningEffort);

    // Temperature is applied by ConfigureForDeepSeek only in non-thinking mode
    
    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
    
    // Add messages
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalUserMessage);

    // Output budget from the DeepSeek Max Output Tokens setting; thinking output counts against it, and the
    // request's thinking mode picks the registry entry (see ResolveMaxOutputTokens)
    PayloadBuilder->SetOutputBudget(Config.MaxOutputTokens,
        FN2CTokenEstimationService::EstimateTokensForChars(SystemMessage.Len() + FinalUserMessage.Len()));
    
    // json_object response format; the builder sends it only for entries flagged for structured output
    PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    
    // Build and return the payload
    return PayloadBuilder->Build();
}
