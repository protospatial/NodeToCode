// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CDeepSeekService.h"

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

    // Max tokens from the registry entry (thinking output counts against it); temperature is applied by
    // ConfigureForDeepSeek only in non-thinking mode
    const FN2CModelMetadata* ModelMeta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Config.Model, Config.ReasoningEffort);
    PayloadBuilder->SetMaxTokens(ModelMeta && ModelMeta->Provider == EN2CLLMProvider::DeepSeek ? ModelMeta->DefaultMaxTokens : 16000);
    
    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
    
    // Add messages
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalUserMessage);
    
    // json_object response format; the builder sends it only for entries flagged for structured output
    PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    
    // Build and return the payload
    return PayloadBuilder->Build();
}
