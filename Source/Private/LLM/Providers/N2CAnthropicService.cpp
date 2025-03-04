// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CAnthropicService.h"

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
    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM User Message:\n\n%s"), *SystemMessage), EN2CLogSeverity::Debug);

    // Build JSON payload structure using JSON API
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("model"), Config.Model);
    RootObject->SetNumberField(TEXT("temperature"), 0.0);
    RootObject->SetNumberField(TEXT("max_tokens"), 8192);
    
    if (!SystemMessage.IsEmpty())
    {
        RootObject->SetStringField(TEXT("system"), SystemMessage);
    }

    // Try prepending source files to the user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Build messages array
    TArray<TSharedPtr<FJsonValue>> MessagesArray;
    TSharedPtr<FJsonObject> UserContent = MakeShared<FJsonObject>();
    UserContent->SetStringField(TEXT("role"), TEXT("user"));
    
    // Build content array with text entry
    TArray<TSharedPtr<FJsonValue>> ContentEntries;
    TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), FinalUserMessage);
    ContentEntries.Add(MakeShared<FJsonValueObject>(TextContent));
    
    UserContent->SetArrayField(TEXT("content"), ContentEntries);
    MessagesArray.Add(MakeShared<FJsonValueObject>(UserContent));
    RootObject->SetArrayField(TEXT("messages"), MessagesArray);

    // Serialize JSON to string
    FString Payload;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Request Payload:\n\n%s"), *Payload), EN2CLogSeverity::Debug);
    
    return Payload;
}
