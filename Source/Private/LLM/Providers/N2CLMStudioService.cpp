// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CLMStudioService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2CLMStudioService::Initialize(const FN2CLLMConfig& InConfig)
{
    // Create a copy of the input config
    FN2CLLMConfig UpdatedConfig = InConfig;
    
    // Load LM Studio-specific settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        // Use custom endpoint if provided, otherwise use default
        if (!Settings->LMStudioEndpoint.IsEmpty())
        {
            // Normalize the base URL (remove trailing slash if present)
            FString BaseUrl = Settings->LMStudioEndpoint;
            if (BaseUrl.EndsWith(TEXT("/")))
            {
                BaseUrl.RemoveAt(BaseUrl.Len() - 1);
            }
            
            // Ensure we have the correct endpoint path for LM Studio
            if (!BaseUrl.EndsWith(TEXT("/v1/chat/completions")))
            {
                UpdatedConfig.ApiEndpoint = BaseUrl + TEXT("/v1/chat/completions");
            }
            else
            {
                UpdatedConfig.ApiEndpoint = BaseUrl;
            }
            
            LMStudioEndpoint = UpdatedConfig.ApiEndpoint;
            
            // Log the actual endpoint being used for debugging
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Using LM Studio endpoint: %s"), *UpdatedConfig.ApiEndpoint),
                EN2CLogSeverity::Info,
                TEXT("LMStudioService")
            );
        }
        else
        {
            LMStudioEndpoint = GetDefaultEndpoint();
            UpdatedConfig.ApiEndpoint = LMStudioEndpoint;
        }
    }
    
    // Call base class initialization with the updated config
    return Super::Initialize(UpdatedConfig);
}

UN2CResponseParserBase* UN2CLMStudioService::CreateResponseParser()
{
    UN2CLMStudioResponseParser* Parser = NewObject<UN2CLMStudioResponseParser>(this);
    return Parser;
}

void UN2CLMStudioService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // LM Studio supports system prompts
    OutSupportsSystemPrompts = true;
}

void UN2CLMStudioService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
    
    // Add authorization header with LM Studio API key
    if (!Config.ApiKey.IsEmpty())
    {
        OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    }
}

FString UN2CLMStudioService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder for LM Studio
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForLMStudio();
    
    // Try prepending source files to user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
    
    // Prepend user message text if configured
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings && !Settings->LMStudioPrependedModelCommand.IsEmpty())
    {
        FinalUserMessage = Settings->LMStudioPrependedModelCommand + TEXT("\n\n") + FinalUserMessage;
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Prepended model command text: %s"), *Settings->LMStudioPrependedModelCommand),
            EN2CLogSeverity::Debug,
            TEXT("LMStudioService")
        );
    }
    
    // Add messages - LM Studio supports system prompts
    if (!SystemMessage.IsEmpty())
    {
        PayloadBuilder->AddSystemMessage(SystemMessage);
    }
    PayloadBuilder->AddUserMessage(FinalUserMessage);
    
    // IMPORTANT: Use structured output for reliable JSON parsing
    // This ensures LM Studio returns properly formatted JSON responses
    PayloadBuilder->SetStructuredOutput(UN2CLLMPayloadBuilder::GetN2CResponseSchema());
    
    // Build and return the payload
    return PayloadBuilder->Build();
}