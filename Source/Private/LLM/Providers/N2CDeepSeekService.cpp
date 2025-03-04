// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CDeepSeekService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModels.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2CDeepSeekService::CreateResponseParser()
{
    UN2CDeepSeekResponseParser* Parser = NewObject<UN2CDeepSeekResponseParser>(this);
    return Parser;
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
