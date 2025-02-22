// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COllamaService.h"

#include "Core/N2CSettings.h"
#include "LLM/N2CHttpHandler.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CResponseParserBase.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

bool UN2COllamaService::Initialize(const FN2CLLMConfig& InConfig)
{
    Config = InConfig;
    
    // Set the Ollama endpoint that the user provides
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        if (Settings->OllamaConfig.OllamaEndpoint.IsEmpty())
        {
            FN2CLogger::Get().LogError(TEXT("User provided Ollama endpoint is empty. Defaulting to http://localhost:11434"), TEXT("OllamaService"));
        }

        const FString OllamaBaseEndpoint = Settings->OllamaConfig.OllamaEndpoint;
        Config.ApiEndpoint = FString::Printf(TEXT("%s/api/chat"), *OllamaBaseEndpoint);
    }
    else
    {
        Config.ApiEndpoint = DefaultEndpoint;
    }

    // Create HTTP handler
    HttpHandler = NewObject<UN2CHttpHandler>(this);
    if (!HttpHandler)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create HTTP handler"), TEXT("OllamaService"));
        return false;
    }
    HttpHandler->Initialize(Config);

    // Create Ollama response parser
    ResponseParser = NewObject<UN2COllamaResponseParser>(this);
    if (!ResponseParser)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create response parser"), TEXT("OllamaService"));
        return false;
    }
    ResponseParser->Initialize();

    // Create system prompt manager
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create system prompt manager"), TEXT("OllamaService"));
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

void UN2COllamaService::SendRequest(
    const FString& JsonPayload,
    const FString& SystemMessage,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        FN2CLogger::Get().LogError(TEXT("Service not initialized"), TEXT("OllamaService"));
        const bool bExecuted = OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Service not initialized\"}"));
        return;
    }

    // Log provider and model info
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Sending request to Ollama using model: %s"), *Config.Model),
        EN2CLogSeverity::Info,
        TEXT("OllamaService")
    );
    
    // Format request payload for Ollama
    FString FormattedPayload = FormatRequestPayload(JsonPayload, SystemMessage);

    // Send request through HTTP handler
    HttpHandler->PostLLMRequest(
        Config.ApiEndpoint,
        Config.ApiKey,
        FormattedPayload,
        OnComplete
    );
}

void UN2COllamaService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // Get system prompt support from settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        OutSupportsSystemPrompts = Settings->OllamaConfig.bUseSystemPrompts;
    }
    else
    {
        OutSupportsSystemPrompts = false;
    }
}

void UN2COllamaService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
}

FString UN2COllamaService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{

    // Build JSON payload structure using JSON API
    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("model"), Config.Model);

    // Build messages array
    TArray<TSharedPtr<FJsonValue>> MessagesArray;

    // Check if we should use system prompts
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load settings"), TEXT("OllamaService"));
        return FString();
    }

    const bool bSupportsSystemPrompts = Settings->OllamaConfig.bUseSystemPrompts;
    const FN2COllamaConfig& OllamaConf = Settings->OllamaConfig;

    if (bSupportsSystemPrompts && !SystemMessage.IsEmpty())
    {
        // Add system message
        TSharedPtr<FJsonObject> SystemMessageObject = MakeShared<FJsonObject>();
        SystemMessageObject->SetStringField(TEXT("role"), TEXT("system"));
        SystemMessageObject->SetStringField(TEXT("content"), SystemMessage);
        MessagesArray.Add(MakeShared<FJsonValueObject>(SystemMessageObject));
    }

    // Try prepending source files to user message
    FString FinalUserMessage = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalUserMessage);

    // Add user message
    TSharedPtr<FJsonObject> UserMessageObject = MakeShared<FJsonObject>();
    UserMessageObject->SetStringField(TEXT("role"), TEXT("user"));
    UserMessageObject->SetStringField(TEXT("content"), bSupportsSystemPrompts ? UserMessage : 
        PromptManager->MergePrompts(SystemMessage, FinalUserMessage));
    MessagesArray.Add(MakeShared<FJsonValueObject>(UserMessageObject));

    RootObject->SetArrayField(TEXT("messages"), MessagesArray);

    // Define the JSON schema for structured output
    const FString JsonSchema = TEXT(R"({
        "type": "object",
        "properties": {
            "graphs": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "graph_name": {"type": "string"},
                        "graph_type": {"type": "string"},
                        "graph_class": {"type": "string"},
                        "code": {
                            "type": "object",
                            "properties": {
                                "graphDeclaration": {"type": "string"},
                                "graphImplementation": {"type": "string"},
                                "implementationNotes": {"type": "string"}
                            },
                            "required": ["graphDeclaration", "graphImplementation"]
                        }
                    },
                    "required": ["graph_name", "graph_type", "graph_class", "code"]
                }
            }
        },
        "required": ["graphs"]
    })");

    // Parse the schema string into a JSON object
    TSharedPtr<FJsonObject> SchemaObject;
    TSharedRef<TJsonReader<>> SchemaReader = TJsonReaderFactory<>::Create(JsonSchema);
    if (!FJsonSerializer::Deserialize(SchemaReader, SchemaObject))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse JSON schema"), TEXT("OllamaService"));
        return FString();
    }

    // Add Ollama-specific options
    TSharedPtr<FJsonObject> OptionsObject = MakeShared<FJsonObject>();
    OptionsObject->SetNumberField(TEXT("temperature"), OllamaConf.Temperature);
    OptionsObject->SetNumberField(TEXT("num_predict"), OllamaConf.NumPredict);
    OptionsObject->SetNumberField(TEXT("top_p"), OllamaConf.TopP);
    OptionsObject->SetNumberField(TEXT("top_k"), OllamaConf.TopK);
    OptionsObject->SetNumberField(TEXT("min_p"), OllamaConf.MinP);
    OptionsObject->SetNumberField(TEXT("repeat_penalty"), OllamaConf.RepeatPenalty);
    OptionsObject->SetNumberField(TEXT("mirostat"), OllamaConf.Mirostat);
    OptionsObject->SetNumberField(TEXT("mirostat_eta"), OllamaConf.MirostatEta);
    OptionsObject->SetNumberField(TEXT("mirostat_tau"), OllamaConf.MirostatTau);
    OptionsObject->SetNumberField(TEXT("num_ctx"), OllamaConf.NumCtx);
    OptionsObject->SetNumberField(TEXT("seed"), OllamaConf.Seed);

    RootObject->SetObjectField(TEXT("options"), OptionsObject);
    RootObject->SetBoolField(TEXT("stream"), false);
    RootObject->SetNumberField(TEXT("keep_alive"), OllamaConf.KeepAlive);

    // Define response format for Structured Outputs (JSON)
    TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
    RootObject->SetObjectField(TEXT("format"), SchemaObject);

    // Serialize JSON to string
    FString Payload;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Request Payload:\n\n%s"), *Payload), EN2CLogSeverity::Debug);

    return Payload;
}
