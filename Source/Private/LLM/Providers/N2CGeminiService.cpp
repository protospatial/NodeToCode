// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CGeminiService.h"

#include "LLM/N2CSystemPromptManager.h"

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
    // Build full endpoint (Gemini typically uses "model_name:generateContent")
    OutEndpoint = FString::Printf(TEXT("%s%s:generateContent?key=%s"),
                                  *Config.ApiEndpoint, *Config.Model, *Config.ApiKey);
    OutAuthToken = TEXT("");  // Gemini uses key in URL, not in auth header
    
    // Default to supporting system prompts since all Gemini models currently support system prompts
    OutSupportsSystemPrompts = true;
}

void UN2CGeminiService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2CGeminiService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForGemini();
    
    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Gemini 2.5 Pro seems to respond with more reliable structured outputs with a temp of 1.0
    if (Config.Model.Contains("gemini-2.5-pro"))
    {
        PayloadBuilder->SetTemperature(1.0f); 
    }
    
    // Add system message and user message
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalUserMessage);
    
    // Add JSON schema for response format if model supports it
    if (Config.Model != TEXT("gemini-2.0-flash-thinking-exp-01-21"))
    {
        PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    }
    
    // Build and return the payload
    return PayloadBuilder->Build();
}
