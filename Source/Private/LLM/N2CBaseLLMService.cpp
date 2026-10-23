// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/N2CResponseParserBase.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CSecretStore.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UN2CBaseLLMService::GetMissingApiKeyError(EN2CLLMProvider Provider, const FString& ApiKey)
{
    const TCHAR* Name = nullptr;
    switch (Provider)
    {
    case EN2CLLMProvider::OpenAI: Name = TEXT("OpenAI"); break;
    case EN2CLLMProvider::Anthropic: Name = TEXT("Anthropic"); break;
    case EN2CLLMProvider::Gemini: Name = TEXT("Gemini"); break;
    case EN2CLLMProvider::DeepSeek: Name = TEXT("DeepSeek"); break;
    default: return FString(); // Ollama and LM Studio run locally without a key
    }
    if (!ApiKey.TrimStartAndEnd().IsEmpty())
    {
        return FString();
    }

    FString Message = FString::Printf(
        TEXT("No %s API key is set. Add one in Project Settings > Plugins > Node to Code > LLM Services > %s > API Key."),
        Name, Name);
    if (Provider == EN2CLLMProvider::Anthropic || Provider == EN2CLLMProvider::Gemini)
    {
        Message += FString::Printf(TEXT(" Claude Pro/Max and Google account sign-in were removed in NodeToCode 2.0; see %s"),
            N2CSignInRemoval::DocsUrl);
    }
    return Message;
}

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
    
    // A cloud provider without a key would only get a 401 back; stop before any request is made
    const FString MissingKeyError = GetMissingApiKeyError(GetProviderType(), Config.ApiKey);
    if (!MissingKeyError.IsEmpty())
    {
        FN2CLogger::Get().LogError(MissingKeyError, TEXT("BaseLLMService"));
        TSharedRef<FJsonObject> ErrorJson = MakeShared<FJsonObject>();
        ErrorJson->SetStringField(TEXT("error"), MissingKeyError);
        FString ErrorString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ErrorString);
        FJsonSerializer::Serialize(ErrorJson, Writer);
        const bool bExecuted = OnComplete.ExecuteIfBound(ErrorString);
        return;
    }

    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to %s using model: %s"), 
            *UEnum::GetValueAsString(GetProviderType()), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("BaseLLMService")
    );

    // Format request payload
    FString FormattedPayload = FormatRequestPayload(JsonPayload, SystemMessage);

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
