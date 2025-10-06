// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CxAIService.h"

#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CLLMPayloadBuilder.h"

UN2CResponseParserBase* UN2CxAIService::CreateResponseParser()
{
    UN2CxAIResponseParser* Parser = NewObject<UN2CxAIResponseParser>(this);
    return Parser;
}

void UN2CxAIService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint.IsEmpty() ? GetDefaultEndpoint() : Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;

    // xAI Grok chat completions support system prompts (OpenAI-compatible)
    OutSupportsSystemPrompts = true;
}

void UN2CxAIService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2CxAIService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder (xAI is OpenAI-compatible)
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForOpenAI();

    // Set common parameters
    PayloadBuilder->SetTemperature(0.0f);
    PayloadBuilder->SetMaxTokens(8192);

    // xAI Grok supports JSON schema through the OpenAI-style response_format
    PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());

    // Prepare content (prepend source files paths/content if configured)
    FString FinalContent = UserMessage;
    if (PromptManager)
    {
        // Try prepending source files to the user message
        PromptManager->PrependSourceFilesToUserMessage(FinalContent);
    }

    // Add messages (xAI supports system role)
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalContent);

    // Build and return the payload
    return PayloadBuilder->Build();
}
