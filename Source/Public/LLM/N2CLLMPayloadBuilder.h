// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "N2CLLMTypes.h"
#include "N2COllamaConfig.h"
#include "N2CLLMPayloadBuilder.generated.h"

/**
 * @class UN2CLLMPayloadBuilder
 * @brief Utility class for building JSON payloads for LLM requests
 */
UCLASS()
class NODETOCODE_API UN2CLLMPayloadBuilder : public UObject
{
    GENERATED_BODY()

public:
    /** Initialize with model name */
    void Initialize(const FString& ModelName);
    
    /** Set the model name */
    void SetModel(const FString& ModelName);
    
    /** Common configuration */
    void SetTemperature(float Value);
    void SetMaxTokens(int32 Value);
    
    /** Message building */
    void AddSystemMessage(const FString& Content);
    void AddUserMessage(const FString& Content);

    /** OAuth-specific: Add system messages as array of content blocks for Anthropic OAuth */
    void AddAnthropicOAuthSystemMessages(const FString& OAuthPrefix, const FString& ActualContent);
    
    /** Response format */
    void SetJsonResponseFormat(const TSharedPtr<FJsonObject>& Schema);
    void SetStructuredOutput(const TSharedPtr<FJsonObject>& Schema) { SetJsonResponseFormat(Schema); }
    
    /** Provider-specific extensions */
    void ConfigureForOpenAI();
    void ConfigureForAnthropic();
    void ConfigureForGemini();
    /**
     * DeepSeek chat completions. ReasoningEffort is the request's thinking control (FN2CLLMConfig::ReasoningEffort);
     * empty uses the registry entry for the model. Writes thinking.type and, with thinking on, reasoning_effort.
     */
    void ConfigureForDeepSeek(const FString& ReasoningEffort = FString());
    void ConfigureForOllama(const struct FN2COllamaConfig& OllamaConfig);
    void ConfigureForLMStudio();
    
    /** Generate final payload */
    FString Build();
    
    /** Get the JSON schema for N2C translation responses */
    static TSharedPtr<FJsonObject> GetN2CResponseSchema();

    /**
     * Deep copy of a JSON schema with every "additionalProperties" key removed at every level.
     * Gemini's responseSchema is an OpenAPI subset that rejects the field with a 400.
     */
    static TSharedPtr<FJsonObject> StripAdditionalProperties(const TSharedPtr<FJsonObject>& Schema);

private:
    /** Returns the named sub-object of the root, creating and attaching it when missing */
    TSharedPtr<FJsonObject> GetOrCreateRootObjectField(const FString& FieldName);

    /** Logs and returns false when a provider-dependent field is written before a ConfigureFor* call */
    bool EnsureProviderConfigured(const TCHAR* Caller) const;

    /** Whether temperature/top_p/top_k may be sent for the current provider and model (registry flag) */
    bool AcceptsSamplingParams() const;

    /** Whether the current OpenAI model takes max_completion_tokens (unregistered IDs are treated as reasoning models) */
    bool UsesMaxCompletionTokens() const;

    /** Writes the registry's default temperature when the model accepts sampling parameters */
    void ApplyModelSamplingDefaults();

    /** Registry entry for ModelName when it belongs to the configured provider, else nullptr */
    const struct FN2CModelMetadata* FindModelMetadata() const;

    /** Root JSON object */
    TSharedPtr<FJsonObject> RootObject;
    
    /** Messages array for providers that use it */
    TArray<TSharedPtr<FJsonValue>> MessagesArray;
    
    /** Current provider type, valid once bProviderConfigured is true */
    EN2CLLMProvider ProviderType = EN2CLLMProvider::OpenAI;

    /** Set by the ConfigureFor* calls; fields that depend on the provider are written only after it */
    bool bProviderConfigured = false;
    
    /** Model name */
    FString ModelName;

    /** Thinking control the request asked for (DeepSeek); empty when the registry entry decides */
    FString RequestedReasoningEffort;

    /** Thinking control written to the payload (DeepSeek); empty when none was sent */
    FString SentReasoningEffort;
};
