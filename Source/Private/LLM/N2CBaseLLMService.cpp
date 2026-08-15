// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBaseLLMService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CResponseParserBase.h"
#include "Utils/N2CLogger.h"

bool UN2CBaseLLMService::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    
    // Use default endpoint if none provided
    if (Config.ApiEndpoint.IsEmpty())
    {
        Config.ApiEndpoint = GetDefaultEndpoint();
    }

    // Initialize components
    InitializeComponents();
    
    // Set provider-specific headers
    TMap<FString, FString> Headers;
    GetProviderHeaders(Headers);
    if (HttpHandler)
    {
        HttpHandler->ExtraHeaders = Headers;
    }

    bIsInitialized = true;
    return true;
}

void UN2CBaseLLMService::InitializeComponents()
{
    // Create HTTP handler
    HttpHandler = NewObject<UN2CHttpHandler>(this);
    if (!HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create HTTP handler"), TEXT("BaseLLMService"));
        return;
    }
    HttpHandler->Initialize(Config);

    // Create response parser
    ResponseParser = CreateResponseParser();
    if (!ResponseParser)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create response parser"), TEXT("BaseLLMService"));
        return;
    }
    ResponseParser->Initialize();

    // Create system prompt manager
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create system prompt manager"), TEXT("BaseLLMService"));
        return;
    }
    PromptManager->Initialize(Config);
}

void UN2CBaseLLMService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("BaseLLMService"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }
    
    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to %s using model: %s"), 
            *UEnum::GetValueAsString(GetProviderType()), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("BaseLLMService")
    );

    // Compose request-wide custom instructions only after the concrete provider service has been
    // initialized. This ensures model-specific matching uses the model that will actually receive
    // the request, including transient provider choices and named custom providers.
    FString EffectiveSystemMessage = SystemMessage;
    if (const UN2CSettings* RequestSettings = GetDefault<UN2CSettings>())
    {
        const FString CustomInstructions = RequestSettings->GetEffectiveCustomInstructions(
            Config.Provider,
            Config.Model);

        if (!CustomInstructions.IsEmpty())
        {
            if (!EffectiveSystemMessage.IsEmpty())
            {
                EffectiveSystemMessage += TEXT("\n\n");
            }

            EffectiveSystemMessage += TEXT("<customInstructions>\n");
            EffectiveSystemMessage += CustomInstructions;
            EffectiveSystemMessage += TEXT("\n</customInstructions>");

            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Applied custom instructions for model: %s"), *Config.Model),
                EN2CLogSeverity::Debug,
                TEXT("BaseLLMService"));
        }
    }

    // Format request payload. Providers without a separate system-message channel already merge
    // this system message into the user content in their provider-specific payload builder.
    FString FormattedPayload = FormatRequestPayload(JsonPayload, EffectiveSystemMessage);

    // Get endpoint and auth token
    FString Endpoint, AuthToken;
    bool bSupportsSystemPrompts;
    GetConfiguration(Endpoint, AuthToken, bSupportsSystemPrompts);

    // Send request through HTTP handler
    HttpHandler->PostLLMRequest(
        Endpoint,
        AuthToken,
        FormattedPayload,
        OnComplete
    );
}
