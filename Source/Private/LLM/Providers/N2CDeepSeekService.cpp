// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CDeepSeekService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2CDeepSeekService::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    
    // Use default endpoint if none provided
    if (Config.ApiEndpoint.IsEmpty())
    {
        Config.ApiEndpoint = DefaultEndpoint;
    }

    // Create HTTP handler
    HttpHandler = NewObject<UN2CHttpHandler>(this);
    if (!HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create HTTP handler"), TEXT("DeepSeekService"));
        return false;
    }
    HttpHandler->Initialize(Config);

    // Create DeepSeek response parser
    ResponseParser = NewObject<UN2CDeepSeekResponseParser>(this);
    if (!ResponseParser)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create response parser"), TEXT("DeepSeekService"));
        return false;
    }
    ResponseParser->Initialize();

    // Create system prompt manager
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create system prompt manager"), TEXT("DeepSeekService"));
        return false;
    }
    PromptManager->Initialize(Config);

    // Set required headers
    TMap<FString, FString> Headers;
    GetProviderHeaders(Headers);
    HttpHandler->ExtraHeaders = Headers;

    bIsInitialized = true;
    return true;
}

void UN2CDeepSeekService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("DeepSeekService"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }

    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to DeepSeek using model: %s"), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("DeepSeekService")
    );

    // Format request payload for DeepSeek
    FString FormattedPayload = FormatRequestPayload(JsonPayload, SystemMessage);

    // Send request through HTTP handler
    HttpHandler->PostLLMRequest(
        Config.ApiEndpoint,
        Config.ApiKey,
        FormattedPayload,
        OnComplete
    );
}

void UN2CDeepSeekService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    OutSupportsSystemPrompts = true;  // DeepSeek always supports system prompts
}

void UN2CDeepSeekService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2CDeepSeekService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{

    // Load settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load plugin settings"), TEXT("LLMModule"));
    }

    // Build JSON payload structure using JSON API
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("model"), Config.Model);
    RootObject->SetNumberField(TEXT("temperature"), 0.0);
    RootObject->SetNumberField(TEXT("max_tokens"), 8000);

    // Build messages array
    TArray<TSharedPtr<FJsonValue>> MessagesArray;

    // Add system message
    TSharedPtr<FJsonObject> SystemMessageObject = MakeShared<FJsonObject>();
    SystemMessageObject->SetStringField(TEXT("role"), TEXT("system"));
    SystemMessageObject->SetStringField(TEXT("content"), SystemMessage);
    MessagesArray.Add(MakeShared<FJsonValueObject>(SystemMessageObject));

    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);
    
    // Add user message
    TSharedPtr<FJsonObject> UserMessageObject = MakeShared<FJsonObject>();
    UserMessageObject->SetStringField(TEXT("role"), TEXT("user"));
    UserMessageObject->SetStringField(TEXT("content"), FinalUserMessage);
    MessagesArray.Add(MakeShared<FJsonValueObject>(UserMessageObject));

    RootObject->SetArrayField(TEXT("messages"), MessagesArray);

    // Define response format if we're using a DeepSeek model that supports it
    if (FN2CLLMModelUtils::GetDeepSeekModelValue(Settings->DeepSeekModel) == TEXT("deepseek-chat"))
    {
        TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
        ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_object"));
        RootObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
    }

    // Serialize JSON to string
    FString Payload;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Request Payload:\n\n%s"), *Payload), EN2CLogSeverity::Debug);
    
    return Payload;
}
