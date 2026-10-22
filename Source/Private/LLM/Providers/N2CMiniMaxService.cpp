// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CMiniMaxService.h"
#include "LLM/Providers/N2CMiniMaxResponseParser.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2CMiniMaxService::Initialize(const FN2CLLMConfig& InConfig)
{
    // Create a copy of the input config
    FN2CLLMConfig UpdatedConfig = InConfig;

    // Load MiniMax-specific settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        // Use custom endpoint if provided, otherwise use default
        if (!Settings->MiniMaxEndpoint.IsEmpty())
        {
            // Normalize the base URL (remove trailing slash if present)
            FString BaseUrl = Settings->MiniMaxEndpoint;
            if (BaseUrl.EndsWith(TEXT("/")))
            {
                BaseUrl.RemoveAt(BaseUrl.Len() - 1);
            }

            // Ensure we have the correct endpoint path for MiniMax (OpenAI-compatible)
            if (!BaseUrl.EndsWith(TEXT("/v1/chat/completions")))
            {
                UpdatedConfig.ApiEndpoint = BaseUrl + TEXT("/v1/chat/completions");
            }
            else
            {
                UpdatedConfig.ApiEndpoint = BaseUrl;
            }

            MiniMaxEndpoint = UpdatedConfig.ApiEndpoint;

            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Using MiniMax endpoint: %s"), *UpdatedConfig.ApiEndpoint),
                EN2CLogSeverity::Info,
                TEXT("MiniMaxService")
            );
        }
        else
        {
            MiniMaxEndpoint = GetDefaultEndpoint();
            UpdatedConfig.ApiEndpoint = MiniMaxEndpoint;
        }
    }

    // Call base class initialization with the updated config
    return Super::Initialize(UpdatedConfig);
}

UN2CResponseParserBase* UN2CMiniMaxService::CreateResponseParser()
{
    UN2CMiniMaxResponseParser* Parser = NewObject<UN2CMiniMaxResponseParser>(this);
    return Parser;
}

void UN2CMiniMaxService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;

    // MiniMax supports system prompts
    OutSupportsSystemPrompts = true;
}

void UN2CMiniMaxService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));

    // Add authorization header with MiniMax API key
    if (!Config.ApiKey.IsEmpty())
    {
        OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    }
}

FString UN2CMiniMaxService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Create and configure payload builder for MiniMax
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForMiniMax();

    // MiniMax models need higher token limits for large Blueprint graphs
    PayloadBuilder->SetMaxTokens(16384);

    // Try prepending source files to user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Add messages - MiniMax supports system prompts
    if (!SystemMessage.IsEmpty())
    {
        PayloadBuilder->AddSystemMessage(SystemMessage);
    }
    PayloadBuilder->AddUserMessage(FinalUserMessage);

    // NOTE: MiniMax may not support json_schema structured output
    // Commenting out to test if that's causing the failure
    // PayloadBuilder->SetStructuredOutput(UN2CLLMPayloadBuilder::GetN2CResponseSchema());

    // Build and return the payload
    return PayloadBuilder->Build();
}