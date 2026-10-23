// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CAnthropicService.h"

#include "Auth/N2CAnthropicOAuthTokenManager.h"
#include "Auth/N2COAuthTypes.h"
#include "Core/N2CSettings.h"
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
    if (IsUsingOAuth())
    {
        // OAuth authentication uses Bearer token
        UN2CAnthropicOAuthTokenManager* TokenManager = UN2CAnthropicOAuthTokenManager::Get();
        FString AccessToken = TokenManager->GetAccessToken();
        OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *AccessToken));
        OutHeaders.Add(TEXT("anthropic-beta"), FN2COAuthConstants::BetaHeader);
    }
    else
    {
        // Standard API key authentication
        OutHeaders.Add(TEXT("x-api-key"), Config.ApiKey);
    }

    OutHeaders.Add(TEXT("anthropic-version"), ApiVersion);
    OutHeaders.Add(TEXT("content-type"), TEXT("application/json"));
}

bool UN2CAnthropicService::IsUsingOAuth() const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    return Settings && Settings->IsUsingAnthropicOAuth();
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

    // Max tokens is required by Anthropic. No temperature: the builder sends sampling parameters only
    // for registered models that accept them (Claude Opus 4.7 and later reject them).
    PayloadBuilder->SetMaxTokens(16000);

    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Add system message - use different format for OAuth vs API key
    if (IsUsingOAuth())
    {
        // OAuth requires system to be an array of content blocks with the prefix as first entry
        PayloadBuilder->AddAnthropicOAuthSystemMessages(FN2COAuthConstants::SystemPromptPrefix, SystemMessage);
        FN2CLogger::Get().Log(TEXT("Using OAuth system message format (array of content blocks)"), EN2CLogSeverity::Debug);
    }
    else
    {
        // Standard API key auth uses string format
        PayloadBuilder->AddSystemMessage(SystemMessage);
    }

    PayloadBuilder->AddUserMessage(FinalUserMessage);

    // Build and return the payload
    return PayloadBuilder->Build();
}
