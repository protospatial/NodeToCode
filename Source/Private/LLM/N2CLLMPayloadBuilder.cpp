// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMPayloadBuilder.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

void UN2CLLMPayloadBuilder::Initialize(const FString& InModelName)
{
    // Create root JSON object
    RootObject = MakeShared<FJsonObject>();
    MessagesArray.Empty();
    ModelName = InModelName;
    
    // Set model name
    RootObject->SetStringField(TEXT("model"), ModelName);
    
    // Set default values
    SetTemperature(0.0f);
    SetMaxTokens(8192);
}

void UN2CLLMPayloadBuilder::SetModel(const FString& InModelName)
{
    ModelName = InModelName;
    RootObject->SetStringField(TEXT("model"), ModelName);
}

void UN2CLLMPayloadBuilder::SetTemperature(float Value)
{
    switch (ProviderType)
    {
        case EN2CLLMProvider::Gemini:
            {
                // Gemini only accepts temperature in generationConfig
                TSharedPtr<FJsonObject> GenConfig = RootObject->GetObjectField(TEXT("generationConfig"));
                if (!GenConfig.IsValid())
                {
                    GenConfig = MakeShared<FJsonObject>();
                    RootObject->SetObjectField(TEXT("generationConfig"), GenConfig);
                }
                GenConfig->SetNumberField(TEXT("temperature"), Value);
            }
            break;
        case EN2CLLMProvider::OpenAI:
            // OpenAI o1, o3, and o4 models don't support temperature
            if (ModelName.StartsWith(TEXT("o1")) || ModelName.StartsWith(TEXT("o3")) || ModelName.StartsWith(TEXT("o4")))
            {
                // Skip setting temperature for o1 and o3 models
                FN2CLogger::Get().Log(TEXT("Temperature parameter not supported for o1/o3 models, skipping"), EN2CLogSeverity::Debug);
            }
            else
            {
                RootObject->SetNumberField(TEXT("temperature"), Value);
            }
            break;
        case EN2CLLMProvider::LMStudio:
            // Skip setting temperature for LM Studio - let the LM Studio UI handle this
            // This allows users to configure temperature in LM Studio and prevents
            // interference with reasoning models that may perform worse with low temperatures
            FN2CLogger::Get().Log(TEXT("Temperature parameter skipped for LM Studio - use LM Studio UI to configure"), EN2CLogSeverity::Debug);
            break;
        default:
            // All other providers use root-level temperature
            RootObject->SetNumberField(TEXT("temperature"), Value);
            break;
    }
}

void UN2CLLMPayloadBuilder::SetMaxTokens(int32 Value)
{
    // Different providers use different field names for max tokens
    switch (ProviderType)
    {
        case EN2CLLMProvider::Anthropic:
            RootObject->SetNumberField(TEXT("max_tokens"), Value);
            break;
        case EN2CLLMProvider::Gemini:
            {
                // Gemini uses generationConfig.maxOutputTokens
                TSharedPtr<FJsonObject> GenConfig = RootObject->GetObjectField(TEXT("generationConfig"));
                if (!GenConfig.IsValid())
                {
                    GenConfig = MakeShared<FJsonObject>();
                    RootObject->SetObjectField(TEXT("generationConfig"), GenConfig);
                }
                GenConfig->SetNumberField(TEXT("maxOutputTokens"), Value);
            }
            break;
        case EN2CLLMProvider::Ollama:
            {
                // Ollama uses options.num_predict
                TSharedPtr<FJsonObject> Options = RootObject->GetObjectField(TEXT("options"));
                if (!Options.IsValid())
                {
                    Options = MakeShared<FJsonObject>();
                    RootObject->SetObjectField(TEXT("options"), Options);
                }
                Options->SetNumberField(TEXT("num_predict"), Value);
            }
            break;
        case EN2CLLMProvider::OpenAI:
            // OpenAI o1, o3, and o4 models use max_completion_tokens instead of max_tokens
            if (ModelName.StartsWith(TEXT("o1")) || ModelName.StartsWith(TEXT("o3")) || ModelName.StartsWith(TEXT("o4")))
            {
                RootObject->SetNumberField(TEXT("max_completion_tokens"), Value);
            }
            else
            {
                RootObject->SetNumberField(TEXT("max_tokens"), Value);
            }
            break;
        case EN2CLLMProvider::LMStudio:
            // LM Studio uses OpenAI-compatible format with max_tokens
            RootObject->SetNumberField(TEXT("max_tokens"), Value);
            break;
        default:
            // DeepSeek uses max_tokens
            RootObject->SetNumberField(TEXT("max_tokens"), Value);
            break;
    }
}

void UN2CLLMPayloadBuilder::AddSystemMessage(const FString& Content)
{
    if (Content.IsEmpty())
    {
        return;
    }
    
    switch (ProviderType)
    {
        case EN2CLLMProvider::Anthropic:
            // Anthropic uses a top-level "system" field
            RootObject->SetStringField(TEXT("system"), Content);
            break;
            
        case EN2CLLMProvider::Gemini:
            {
                // Gemini uses systemInstruction.parts
                TSharedPtr<FJsonObject> SysInstructionObj = MakeShared<FJsonObject>();
                SysInstructionObj->SetStringField(TEXT("role"), TEXT("user"));
                
                TArray<TSharedPtr<FJsonValue>> SysPartsArray;
                TSharedPtr<FJsonObject> PartObj = MakeShared<FJsonObject>();
                PartObj->SetStringField(TEXT("text"), Content);
                SysPartsArray.Add(MakeShared<FJsonValueObject>(PartObj));
                
                SysInstructionObj->SetArrayField(TEXT("parts"), SysPartsArray);
                RootObject->SetObjectField(TEXT("systemInstruction"), SysInstructionObj);
            }
            break;
            
        default:
            // OpenAI, DeepSeek, LMStudio, and Ollama use messages array with role=system
            TSharedPtr<FJsonObject> SystemMessageObject = MakeShared<FJsonObject>();
            SystemMessageObject->SetStringField(TEXT("role"), TEXT("system"));
            SystemMessageObject->SetStringField(TEXT("content"), Content);
            MessagesArray.Add(MakeShared<FJsonValueObject>(SystemMessageObject));
            break;
    }
}

void UN2CLLMPayloadBuilder::AddUserMessage(const FString& Content)
{
    if (Content.IsEmpty())
    {
        return;
    }
    
    switch (ProviderType)
    {
        case EN2CLLMProvider::Gemini:
            {
                // Gemini uses contents array with parts
                TSharedPtr<FJsonObject> UserObject = MakeShared<FJsonObject>();
                UserObject->SetStringField(TEXT("role"), TEXT("user"));
                
                TArray<TSharedPtr<FJsonValue>> PartsArray;
                TSharedPtr<FJsonObject> PartObj = MakeShared<FJsonObject>();
                PartObj->SetStringField(TEXT("text"), Content);
                PartsArray.Add(MakeShared<FJsonValueObject>(PartObj));
                
                UserObject->SetArrayField(TEXT("parts"), PartsArray);
                
                // Wrap in contents array
                TArray<TSharedPtr<FJsonValue>> ContentsArray;
                ContentsArray.Add(MakeShared<FJsonValueObject>(UserObject));
                
                RootObject->SetArrayField(TEXT("contents"), ContentsArray);
            }
            break;
            
        case EN2CLLMProvider::Anthropic:
            {
                // Anthropic uses messages array with content array
                TSharedPtr<FJsonObject> UserContent = MakeShared<FJsonObject>();
                UserContent->SetStringField(TEXT("role"), TEXT("user"));
                
                // Build content array with text entry
                TArray<TSharedPtr<FJsonValue>> ContentEntries;
                TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
                TextContent->SetStringField(TEXT("type"), TEXT("text"));
                TextContent->SetStringField(TEXT("text"), Content);
                ContentEntries.Add(MakeShared<FJsonValueObject>(TextContent));
                
                UserContent->SetArrayField(TEXT("content"), ContentEntries);
                MessagesArray.Add(MakeShared<FJsonValueObject>(UserContent));
                
                // Set messages array in root
                RootObject->SetArrayField(TEXT("messages"), MessagesArray);
            }
            break;
            
        default:
            // OpenAI, DeepSeek, LMStudio, and Ollama use messages array with role=user
            TSharedPtr<FJsonObject> UserMessageObject = MakeShared<FJsonObject>();
            UserMessageObject->SetStringField(TEXT("role"), TEXT("user"));
            UserMessageObject->SetStringField(TEXT("content"), Content);
            MessagesArray.Add(MakeShared<FJsonValueObject>(UserMessageObject));
            
            // Set messages array in root
            RootObject->SetArrayField(TEXT("messages"), MessagesArray);
            break;
    }
}

void UN2CLLMPayloadBuilder::SetJsonResponseFormat(const TSharedPtr<FJsonObject>& Schema)
{
    if (!Schema.IsValid())
    {
        return;
    }
    
    switch (ProviderType)
    {
        case EN2CLLMProvider::OpenAI:
            {
                // Special handling for different OpenAI models
                if (ModelName == TEXT("o1-preview-2024-09-12") || ModelName == TEXT("o1-mini-2024-09-12"))
                {
                    // o1-preview and o1-mini don't support response_format at all
                    // Skip setting response_format for these models
                    FN2CLogger::Get().Log(TEXT("Response format not supported for o1-preview/o1-mini, skipping"), EN2CLogSeverity::Debug);
                }
                else if (ModelName.StartsWith(TEXT("o1")) || ModelName.StartsWith(TEXT("o3")))
                {
                    // Other o1/o3 models use json_object type without schema
                    TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
                    ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_object"));
                    RootObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
                }
                else
                {
                    // Other models use json_schema with schema object
                    TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
                    ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_schema"));
                    
                    // Create a wrapper object with name and schema fields
                    TSharedPtr<FJsonObject> JsonSchemaWrapper = MakeShared<FJsonObject>();
                    JsonSchemaWrapper->SetStringField(TEXT("name"), TEXT("n2c_translation_schema"));
                    JsonSchemaWrapper->SetObjectField(TEXT("schema"), Schema);
                    
                    ResponseFormatObject->SetObjectField(TEXT("json_schema"), JsonSchemaWrapper);
                    RootObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
                }
            }
            break;
            
        case EN2CLLMProvider::Gemini:
            {
                // Gemini uses generationConfig
                TSharedPtr<FJsonObject> GenConfigObj = RootObject->GetObjectField(TEXT("generationConfig"));
                if (!GenConfigObj.IsValid())
                {
                    GenConfigObj = MakeShared<FJsonObject>();
                    RootObject->SetObjectField(TEXT("generationConfig"), GenConfigObj);
                }
                
                // Set MIME type and schema
                GenConfigObj->SetStringField(TEXT("responseMimeType"), TEXT("application/json"));
                GenConfigObj->SetObjectField(TEXT("responseSchema"), Schema);
            }
            break;
            
        case EN2CLLMProvider::DeepSeek:
            {
                // DeepSeek uses response_format.type = json_object
                TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
                ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_object"));
                RootObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
            }
            break;
            
        case EN2CLLMProvider::Ollama:
            // Ollama uses format field
            RootObject->SetObjectField(TEXT("format"), Schema);
            break;
            
        case EN2CLLMProvider::LMStudio:
            {
                // LM Studio uses OpenAI-compatible structured output format
                TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
                ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_schema"));
                
                // Create a wrapper object with name and schema fields
                TSharedPtr<FJsonObject> JsonSchemaWrapper = MakeShared<FJsonObject>();
                JsonSchemaWrapper->SetStringField(TEXT("name"), TEXT("n2c_translation_schema"));
                JsonSchemaWrapper->SetStringField(TEXT("strict"), TEXT("true"));
                JsonSchemaWrapper->SetObjectField(TEXT("schema"), Schema);
                
                ResponseFormatObject->SetObjectField(TEXT("json_schema"), JsonSchemaWrapper);
                RootObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
            }
            break;
            
        case EN2CLLMProvider::Anthropic:
            // Anthropic doesn't have a specific JSON schema format yet
            // but we can set the format to json
            break;
    }
}

void UN2CLLMPayloadBuilder::ConfigureForOpenAI()
{
    ProviderType = EN2CLLMProvider::OpenAI;
    
    // Clear messages array and recreate it
    MessagesArray.Empty();
    
    // Remove temperature for o1, o3, o4 models as they don't support it
    if (ModelName.StartsWith(TEXT("o1")) || ModelName.StartsWith(TEXT("o3")) || ModelName.StartsWith(TEXT("o4")))
    {
        if (RootObject->HasField(TEXT("temperature")))
        {
            RootObject->RemoveField(TEXT("temperature"));
        }
    }
}

void UN2CLLMPayloadBuilder::ConfigureForAnthropic()
{
    ProviderType = EN2CLLMProvider::Anthropic;
    
    // Clear messages array and recreate it
    MessagesArray.Empty();
}

void UN2CLLMPayloadBuilder::ConfigureForGemini()
{
    ProviderType = EN2CLLMProvider::Gemini;
    
    // Create generationConfig object if it doesn't exist
    if (!RootObject->HasField(TEXT("generationConfig")))
    {
        TSharedPtr<FJsonObject> GenConfigObj = MakeShared<FJsonObject>();
        GenConfigObj->SetNumberField(TEXT("topK"), 40.0);
        GenConfigObj->SetNumberField(TEXT("topP"), 0.95);
        RootObject->SetObjectField(TEXT("generationConfig"), GenConfigObj);
    }
    
    // Remove any root-level temperature that might have been set
    if (RootObject->HasField(TEXT("temperature")))
    {
        RootObject->RemoveField(TEXT("temperature"));
    }
    
    // Remove any root-level max_tokens that might have been set
    if (RootObject->HasField(TEXT("max_tokens")))
    {
        RootObject->RemoveField(TEXT("max_tokens"));
    }
    
    // Set temperature and maxOutputTokens in generationConfig
    SetTemperature(0.0f);
    SetMaxTokens(8192);
}

void UN2CLLMPayloadBuilder::ConfigureForDeepSeek()
{
    ProviderType = EN2CLLMProvider::DeepSeek;
    
    // Clear messages array and recreate it
    MessagesArray.Empty();
}

void UN2CLLMPayloadBuilder::ConfigureForOllama(const FN2COllamaConfig& OllamaConfig)
{
    ProviderType = EN2CLLMProvider::Ollama;
    
    // Clear messages array and recreate it
    MessagesArray.Empty();
    
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
}

void UN2CLLMPayloadBuilder::ConfigureForLMStudio()
{
    ProviderType = EN2CLLMProvider::LMStudio;
    
    // Clear messages array and recreate it
    MessagesArray.Empty();
    
    // Remove temperature if it was set during Initialize() - let LM Studio UI handle this
    if (RootObject->HasField(TEXT("temperature")))
    {
        RootObject->RemoveField(TEXT("temperature"));
        FN2CLogger::Get().Log(TEXT("Removed temperature from LM Studio payload - use LM Studio UI to configure"), EN2CLogSeverity::Debug);
    }
    
    // LM Studio uses OpenAI-compatible format with stream=false
    RootObject->SetBoolField(TEXT("stream"), false);
}

FString UN2CLLMPayloadBuilder::Build()
{
    // Serialize JSON to string
    FString Payload;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
    
    // Log the payload for debugging
    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Request Payload:\n\n%s"), *Payload), EN2CLogSeverity::Debug);
    
    return Payload;
}

TSharedPtr<FJsonObject> UN2CLLMPayloadBuilder::GetN2CResponseSchema()
{
    // Define the JSON schema for N2C translation responses
    const FString JsonSchema = TEXT(R"(
      {
        "type": "object",
        "properties": {
          "graphs": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "graph_name": {
                  "type": "string"
                },
                "graph_type": {
                  "type": "string"
                },
                "graph_class": {
                  "type": "string"
                },
                "code": {
                  "type": "object",
                  "properties": {
                    "graphDeclaration": {
                      "type": "string"
                    },
                    "graphImplementation": {
                      "type": "string"
                    },
                    "implementationNotes": {
                      "type": "string"
                    }
                  },
                  "required": [
                    "graphDeclaration",
                    "graphImplementation"
                  ]
                }
              },
              "required": [
                "graph_name",
                "graph_type",
                "graph_class",
                "code"
              ]
            }
          }
        },
        "required": [
          "graphs"
        ]
      }
    )");
    
    // Parse the schema string into a JSON object
    TSharedPtr<FJsonObject> SchemaObject;
    TSharedRef<TJsonReader<>> SchemaReader = TJsonReaderFactory<>::Create(JsonSchema);
    if (!FJsonSerializer::Deserialize(SchemaReader, SchemaObject))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse N2C JSON schema"), TEXT("LLMPayloadBuilder"));
        return MakeShared<FJsonObject>();
    }
    
    return SchemaObject;
}
