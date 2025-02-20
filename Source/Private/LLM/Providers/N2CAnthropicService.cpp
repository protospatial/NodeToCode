// Copyright ProtoSpatial. All Rights Reserved.

#include "LLM/Providers/N2CAnthropicService.h"

#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2CAnthropicService::Initialize(const FN2CLLMConfig& InConfig)
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
        FN2CLogger::Get().LogError(TEXT("Failed to create HTTP handler"), TEXT("AnthropicService"));
        return false;
    }
    HttpHandler->Initialize(Config);

    // Create Anthropic response parser
    ResponseParser = NewObject<UN2CAnthropicResponseParser>(this);
    if (!ResponseParser)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create response parser"), TEXT("AnthropicService"));
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

void UN2CAnthropicService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("AnthropicService"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }
    
    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to Anthropic using model: %s"), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("AnthropicService")
    );

    // Format request payload for Anthropic
    FString FormattedPayload = FormatRequestPayload(JsonPayload, SystemMessage);

    // Send request through HTTP handler
    HttpHandler->PostLLMRequest(
        Config.ApiEndpoint,
        Config.ApiKey,
        FormattedPayload,
        OnComplete
    );
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
