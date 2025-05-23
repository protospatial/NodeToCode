// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COllamaService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2COllamaService::Initialize(const FN2CLLMConfig& InConfig)
{

    // Create a copy of the input config
    FN2CLLMConfig UpdatedConfig = InConfig;
    
    // Load Ollama-specific settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        OllamaConfig = Settings->OllamaConfig;
        
        // Transfer the custom Ollama endpoint to the config
        if (!OllamaConfig.OllamaEndpoint.IsEmpty())
        {
            // Normalize the base URL (remove trailing slash if present)
            FString BaseUrl = OllamaConfig.OllamaEndpoint;
            if (BaseUrl.EndsWith(TEXT("/")))
            {
                BaseUrl.RemoveAt(BaseUrl.Len() - 1);
            }
            
            // Make sure we're not double-appending /api/chat
            if (!BaseUrl.EndsWith(TEXT("/api/chat")))
            {
                UpdatedConfig.ApiEndpoint = BaseUrl + TEXT("/api/chat");
            }
            else
            {
                UpdatedConfig.ApiEndpoint = BaseUrl;
            }
            
            // Log the actual endpoint being used for debugging
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Using Ollama endpoint: %s"), *UpdatedConfig.ApiEndpoint),
                EN2CLogSeverity::Info,
                TEXT("OllamaService")
            );
        }
    }
    
    // Call base class initialization with the updated config
    return Super::Initialize(UpdatedConfig);
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
    
    // Prepend user message text if configured
    if (!OllamaConfig.PrependedModelCommand.IsEmpty())
    {
        FinalUserMessage = OllamaConfig.PrependedModelCommand + TEXT("\n\n") + FinalUserMessage;
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Prepended model command text: %s"), *OllamaConfig.PrependedModelCommand),
            EN2CLogSeverity::Debug,
            TEXT("OllamaService")
        );
    }
    
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
