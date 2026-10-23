// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMPayloadBuilder.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

void UN2CLLMPayloadBuilder::Initialize(const FString& InModelName)
{
    // Create root JSON object
    RootObject = MakeShared<FJsonObject>();
    MessagesArray.Empty();
    ModelName = InModelName;
    bProviderConfigured = false;

    // Only the model is written here. Every other field depends on the provider, so it is written after a
    // ConfigureFor* call; sampling fields are written only when the model's registry flag allows them,
    // and each service sets its own max tokens.
    RootObject->SetStringField(TEXT("model"), ModelName);
}

void UN2CLLMPayloadBuilder::SetModel(const FString& InModelName)
{
    ModelName = InModelName;
    RootObject->SetStringField(TEXT("model"), ModelName);
}

TSharedPtr<FJsonObject> UN2CLLMPayloadBuilder::GetOrCreateRootObjectField(const FString& FieldName)
{
    // GetObjectField never returns null: for a missing field it hands back a process-wide static empty
    // object, so writes would land there. TryGetObjectField tells us whether the field really exists.
    const TSharedPtr<FJsonObject>* Existing = nullptr;
    if (RootObject->TryGetObjectField(FieldName, Existing) && Existing && Existing->IsValid())
    {
        return *Existing;
    }

    TSharedPtr<FJsonObject> Created = MakeShared<FJsonObject>();
    RootObject->SetObjectField(FieldName, Created);
    return Created;
}

bool UN2CLLMPayloadBuilder::EnsureProviderConfigured(const TCHAR* Caller) const
{
    if (!bProviderConfigured)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("%s called before a ConfigureFor* call; field not written"), Caller),
            TEXT("LLMPayloadBuilder"));
        return false;
    }
    return true;
}

const FN2CModelMetadata* UN2CLLMPayloadBuilder::FindModelMetadata() const
{
    const FN2CModelMetadata* Meta = FN2CLLMModelRegistry::Get().GetModelMetadataById(ModelName);
    return (Meta && Meta->Provider == ProviderType) ? Meta : nullptr;
}

bool UN2CLLMPayloadBuilder::AcceptsSamplingParams() const
{
    switch (ProviderType)
    {
        case EN2CLLMProvider::LMStudio:
            // Sampling is configured in the LM Studio UI
            return false;
        case EN2CLLMProvider::Ollama:
            // Local models: sampling comes from the user's Ollama settings (options object)
            return true;
        default:
            {
                // Cloud providers: only what the registry says. IDs not in the registry (for example an MCP
                // model override) get no sampling fields, so the provider default applies.
                const FN2CModelMetadata* Meta = FindModelMetadata();
                return Meta && Meta->bAcceptsSamplingParams;
            }
    }
}

bool UN2CLLMPayloadBuilder::UsesMaxCompletionTokens() const
{
    if (ProviderType != EN2CLLMProvider::OpenAI)
    {
        return false;
    }
    // Unregistered OpenAI IDs are treated as reasoning models, which reject max_tokens
    const FN2CModelMetadata* Meta = FindModelMetadata();
    return Meta ? Meta->bUsesMaxCompletionTokens : true;
}

void UN2CLLMPayloadBuilder::ApplyModelSamplingDefaults()
{
    if (!AcceptsSamplingParams())
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("No sampling parameters sent for model %s (provider default applies)"), *ModelName),
            EN2CLogSeverity::Debug);
        return;
    }
    if (const FN2CModelMetadata* Meta = FindModelMetadata())
    {
        SetTemperature(Meta->DefaultTemperature);
    }
}

void UN2CLLMPayloadBuilder::SetTemperature(float Value)
{
    if (!EnsureProviderConfigured(TEXT("SetTemperature")))
    {
        return;
    }
    if (!AcceptsSamplingParams())
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Temperature not sent: model %s does not accept sampling parameters"), *ModelName),
            EN2CLogSeverity::Debug);
        return;
    }

    switch (ProviderType)
    {
        case EN2CLLMProvider::Gemini:
            // Gemini only accepts temperature in generationConfig
            GetOrCreateRootObjectField(TEXT("generationConfig"))->SetNumberField(TEXT("temperature"), Value);
            break;
        case EN2CLLMProvider::Ollama:
            // Ollama's native API takes sampling in options
            GetOrCreateRootObjectField(TEXT("options"))->SetNumberField(TEXT("temperature"), Value);
            break;
        default:
            // OpenAI, Anthropic and DeepSeek use a root-level temperature
            RootObject->SetNumberField(TEXT("temperature"), Value);
            break;
    }
}

void UN2CLLMPayloadBuilder::SetMaxTokens(int32 Value)
{
    if (!EnsureProviderConfigured(TEXT("SetMaxTokens")))
    {
        return;
    }

    // Different providers use different field names for max tokens
    switch (ProviderType)
    {
        case EN2CLLMProvider::Gemini:
            // Gemini uses generationConfig.maxOutputTokens
            GetOrCreateRootObjectField(TEXT("generationConfig"))->SetNumberField(TEXT("maxOutputTokens"), Value);
            break;
        case EN2CLLMProvider::Ollama:
            // Ollama uses options.num_predict
            GetOrCreateRootObjectField(TEXT("options"))->SetNumberField(TEXT("num_predict"), Value);
            break;
        case EN2CLLMProvider::OpenAI:
            // OpenAI reasoning models take max_completion_tokens; the registry flag decides
            RootObject->SetNumberField(UsesMaxCompletionTokens() ? TEXT("max_completion_tokens") : TEXT("max_tokens"), Value);
            break;
        default:
            // Anthropic, DeepSeek and LM Studio (OpenAI-compatible) use max_tokens
            RootObject->SetNumberField(TEXT("max_tokens"), Value);
            break;
    }
}

void UN2CLLMPayloadBuilder::AddSystemMessage(const FString& Content)
{
    if (Content.IsEmpty() || !EnsureProviderConfigured(TEXT("AddMessage")))
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
    if (Content.IsEmpty() || !EnsureProviderConfigured(TEXT("AddMessage")))
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

void UN2CLLMPayloadBuilder::AddAnthropicOAuthSystemMessages(const FString& OAuthPrefix, const FString& ActualContent)
{
    // For OAuth, system must be an array of content blocks
    // Format: [{"type": "text", "text": "OAuth prefix"}, {"type": "text", "text": "actual content"}]
    TArray<TSharedPtr<FJsonValue>> SystemArray;

    // Always add the OAuth prefix as the first content block
    if (!OAuthPrefix.IsEmpty())
    {
        TSharedPtr<FJsonObject> PrefixBlock = MakeShared<FJsonObject>();
        PrefixBlock->SetStringField(TEXT("type"), TEXT("text"));
        PrefixBlock->SetStringField(TEXT("text"), OAuthPrefix);
        SystemArray.Add(MakeShared<FJsonValueObject>(PrefixBlock));
    }

    // Add the actual content as a separate content block
    if (!ActualContent.IsEmpty())
    {
        TSharedPtr<FJsonObject> ContentBlock = MakeShared<FJsonObject>();
        ContentBlock->SetStringField(TEXT("type"), TEXT("text"));
        ContentBlock->SetStringField(TEXT("text"), ActualContent);
        SystemArray.Add(MakeShared<FJsonValueObject>(ContentBlock));
    }

    // Set as array in root object
    RootObject->SetArrayField(TEXT("system"), SystemArray);
}

void UN2CLLMPayloadBuilder::SetJsonResponseFormat(const TSharedPtr<FJsonObject>& Schema)
{
    if (!Schema.IsValid() || !EnsureProviderConfigured(TEXT("SetJsonResponseFormat")))
    {
        return;
    }

    switch (ProviderType)
    {
        case EN2CLLMProvider::OpenAI:
            {
                // The registry decides: models flagged without structured output (o1-preview, o1-mini) take no
                // response_format at all; every other model, unregistered IDs included, gets strict json_schema.
                const FN2CModelMetadata* Meta = FindModelMetadata();
                if (Meta && !Meta->bSupportsStructuredOutput)
                {
                    FN2CLogger::Get().Log(FString::Printf(TEXT("Model %s takes no response_format, skipping"), *ModelName), EN2CLogSeverity::Debug);
                }
                else
                {
                    TSharedPtr<FJsonObject> ResponseFormatObject = MakeShared<FJsonObject>();
                    ResponseFormatObject->SetStringField(TEXT("type"), TEXT("json_schema"));

                    // strict makes OpenAI enforce the schema; GetN2CResponseSchema meets the strict-mode rules
                    // (every property required, additionalProperties false at every level)
                    TSharedPtr<FJsonObject> JsonSchemaWrapper = MakeShared<FJsonObject>();
                    JsonSchemaWrapper->SetStringField(TEXT("name"), TEXT("n2c_translation_schema"));
                    JsonSchemaWrapper->SetBoolField(TEXT("strict"), true);
                    JsonSchemaWrapper->SetObjectField(TEXT("schema"), Schema);

                    ResponseFormatObject->SetObjectField(TEXT("json_schema"), JsonSchemaWrapper);
                    RootObject->SetObjectField(TEXT("response_format"), ResponseFormatObject);
                }
            }
            break;

        case EN2CLLMProvider::Gemini:
            {
                // generationConfig.responseSchema takes an OpenAPI-subset Schema, which has no
                // additionalProperties field (sending it is a 400 "Unknown name"), so send a stripped copy.
                // The v1beta reference marks responseSchema deprecated in favour of responseFormat but still
                // lists it; moving to responseFormat needs a live call to confirm first.
                TSharedPtr<FJsonObject> GenConfigObj = GetOrCreateRootObjectField(TEXT("generationConfig"));
                GenConfigObj->SetStringField(TEXT("responseMimeType"), TEXT("application/json"));
                GenConfigObj->SetObjectField(TEXT("responseSchema"), StripAdditionalProperties(Schema));
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
                JsonSchemaWrapper->SetBoolField(TEXT("strict"), true);
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
    bProviderConfigured = true;

    // Clear messages array and recreate it
    MessagesArray.Empty();

    // Temperature only for models whose registry entry accepts it (not reasoning or unregistered IDs)
    ApplyModelSamplingDefaults();
}

void UN2CLLMPayloadBuilder::ConfigureForAnthropic()
{
    ProviderType = EN2CLLMProvider::Anthropic;
    bProviderConfigured = true;

    // Clear messages array and recreate it
    MessagesArray.Empty();

    // Claude Opus 4.7 and later reject sampling parameters; only registered older models get them
    ApplyModelSamplingDefaults();
}

void UN2CLLMPayloadBuilder::ConfigureForGemini()
{
    ProviderType = EN2CLLMProvider::Gemini;
    bProviderConfigured = true;

    // No topK/topP defaults: Gemini's own defaults apply. Temperature only where the registry allows it
    // (Google says to keep Gemini 3 at its default, since lower values can loop).
    ApplyModelSamplingDefaults();
}

void UN2CLLMPayloadBuilder::ConfigureForDeepSeek()
{
    ProviderType = EN2CLLMProvider::DeepSeek;
    bProviderConfigured = true;

    // Clear messages array and recreate it
    MessagesArray.Empty();

    ApplyModelSamplingDefaults();
}

void UN2CLLMPayloadBuilder::ConfigureForOllama(const FN2COllamaConfig& OllamaConfig)
{
    ProviderType = EN2CLLMProvider::Ollama;
    bProviderConfigured = true;

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
    bProviderConfigured = true;

    // Clear messages array and recreate it
    MessagesArray.Empty();

    // No temperature: LM Studio's UI configures sampling.
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

namespace N2CPayloadBuilderPrivate
{
    TSharedPtr<FJsonValue> CopyWithoutAdditionalProperties(const TSharedPtr<FJsonValue>& Value);

    TSharedPtr<FJsonObject> CopyObjectWithoutAdditionalProperties(const TSharedPtr<FJsonObject>& Object)
    {
        TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
        {
            if (Field.Key == TEXT("additionalProperties"))
            {
                continue;
            }
            Copy->SetField(Field.Key, CopyWithoutAdditionalProperties(Field.Value));
        }
        return Copy;
    }

    TSharedPtr<FJsonValue> CopyWithoutAdditionalProperties(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value.IsValid())
        {
            return Value;
        }
        if (Value->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            if (Value->TryGetObject(Object) && Object && Object->IsValid())
            {
                return MakeShared<FJsonValueObject>(CopyObjectWithoutAdditionalProperties(*Object));
            }
        }
        else if (Value->Type == EJson::Array)
        {
            const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
            if (Value->TryGetArray(Array) && Array)
            {
                TArray<TSharedPtr<FJsonValue>> CopyArray;
                CopyArray.Reserve(Array->Num());
                for (const TSharedPtr<FJsonValue>& Item : *Array)
                {
                    CopyArray.Add(CopyWithoutAdditionalProperties(Item));
                }
                return MakeShared<FJsonValueArray>(CopyArray);
            }
        }
        // Scalars are immutable, so sharing them is safe
        return Value;
    }
}

TSharedPtr<FJsonObject> UN2CLLMPayloadBuilder::StripAdditionalProperties(const TSharedPtr<FJsonObject>& Schema)
{
    if (!Schema.IsValid())
    {
        return Schema;
    }
    return N2CPayloadBuilderPrivate::CopyObjectWithoutAdditionalProperties(Schema);
}

TSharedPtr<FJsonObject> UN2CLLMPayloadBuilder::GetN2CResponseSchema()
{
    // Define the JSON schema for N2C translation responses
    // Note: additionalProperties: false is required at all object levels for OpenAI's strict JSON schema mode
    // Gemini rejects additionalProperties, so its path sends StripAdditionalProperties() of this schema
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
                    "graphImplementation",
                    "implementationNotes"
                  ],
                  "additionalProperties": false
                }
              },
              "required": [
                "graph_name",
                "graph_type",
                "graph_class",
                "code"
              ],
              "additionalProperties": false
            }
          }
        },
        "required": [
          "graphs"
        ],
        "additionalProperties": false
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
