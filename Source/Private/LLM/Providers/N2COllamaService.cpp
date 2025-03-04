// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COllamaService.h"

#include "Core/N2CSettings.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2COllamaService::CreateResponseParser()
{
    UN2COllamaResponseParser* Parser = NewObject<UN2COllamaResponseParser>(this);
    return Parser;
}


void UN2COllamaService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // Get system prompt support from settings
    OutSupportsSystemPrompts = OllamaConfig.bUseSystemPrompts;
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

    const bool bSupportsSystemPrompts = OllamaConfig.bUseSystemPrompts;

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
    UserMessageObject->SetStringField(TEXT("content"), bSupportsSystemPrompts ? FinalUserMessage : 
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
    OptionsObject->SetNumberField(TEXT("temperature"), OllamaConfig.Temperature);
    OptionsObject->SetNumberField(TEXT("num_predict"), OllamaConfig.NumPredict);
    OptionsObject->SetNumberField(TEXT("top_p"), OllamaConfig.TopP);
    OptionsObject->SetNumberField(TEXT("top_k"), OllamaConfig.TopK);
    OptionsObject->SetNumberField(TEXT("min_p"), OllamaConfig.MinP);
    OptionsObject->SetNumberField(TEXT("repeat_penalty"), OllamaConfig.RepeatPenalty);
    OptionsObject->SetNumberField(TEXT("mirostat"), OllamaConfig.Mirostat);
    OptionsObject->SetNumberField(TEXT("mirostat_eta"), OllamaConfig.MirostatEta);
    OptionsObject->SetNumberField(TEXT("mirostat_tau"), OllamaConfig.MirostatTau);
    OptionsObject->SetNumberField(TEXT("num_ctx"), OllamaConfig.NumCtx);
    OptionsObject->SetNumberField(TEXT("seed"), OllamaConfig.Seed);

    RootObject->SetObjectField(TEXT("options"), OptionsObject);
    RootObject->SetBoolField(TEXT("stream"), false);
    RootObject->SetNumberField(TEXT("keep_alive"), OllamaConfig.KeepAlive);

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
