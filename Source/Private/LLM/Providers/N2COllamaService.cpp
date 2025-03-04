// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COllamaService.h"

#include "Core/N2CSettings.h"
#include "Utils/N2CLogger.h"

bool UN2COllamaService::Initialize(const FN2CLLMConfig& InConfig)
{
    // Load Ollama-specific settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        OllamaConfig = Settings->OllamaConfig;
    }
    
    // Call base class initialization
    return Super::Initialize(InConfig);
}

UN2CResponseParserBase* UN2COllamaService::CreateResponseParser()
{
    UN2COllamaResponseParser* Parser = NewObject<UN2COllamaResponseParser>(this);
    return Parser;
}


void UN2COllamaService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // Get system prompt support from settings
    OutSupportsSystemPrompts = OllamaConfig.bUseSystemPrompts;
}

void UN2COllamaService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2COllamaService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForOllama(OllamaConfig);
    
    // Try prepending source files to user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
    
    const bool bSupportsSystemPrompts = OllamaConfig.bUseSystemPrompts;
    
    // Add messages
    if (bSupportsSystemPrompts && !SystemMessage.IsEmpty())
    {
        PayloadBuilder->AddSystemMessage(SystemMessage);
        PayloadBuilder->AddUserMessage(FinalUserMessage);
    }
    else
    {
        // Merge system and user prompts if model doesn't support system prompts
        FString MergedContent = PromptManager->MergePrompts(SystemMessage, FinalUserMessage);
        PayloadBuilder->AddUserMessage(MergedContent);
    }
    
    // Add JSON schema for response format
    PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    
    // Build and return the payload
    return PayloadBuilder->Build();
}
