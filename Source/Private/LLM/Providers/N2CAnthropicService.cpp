// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CAnthropicService.h"

#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2CAnthropicService::CreateResponseParser()
{
    UN2CAnthropicResponseParser* Parser = NewObject<UN2CAnthropicResponseParser>(this);
    return Parser;
}

void UN2CAnthropicService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    OutSupportsSystemPrompts = true;  // Anthropic supports system prompts
}

void UN2CAnthropicService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("x-api-key"), Config.ApiKey);
    OutHeaders.Add(TEXT("anthropic-version"), ApiVersion);
    OutHeaders.Add(TEXT("content-type"), TEXT("application/json"));
}

FString UN2CAnthropicService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Log original content (no escaping needed for logging system)
    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM System Message:\n\n%s"), *SystemMessage), EN2CLogSeverity::Debug);
    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM User Message:\n\n%s"), *UserMessage), EN2CLogSeverity::Debug);

    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForAnthropic();
    
    // Set common parameters
    PayloadBuilder->SetTemperature(0.0f);
    PayloadBuilder->SetMaxTokens(8192);
    
    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
    
    // Add messages
    PayloadBuilder->AddSystemMessage(SystemMessage);
    PayloadBuilder->AddUserMessage(FinalUserMessage);
    
    // Build and return the payload
    return PayloadBuilder->Build();
}
