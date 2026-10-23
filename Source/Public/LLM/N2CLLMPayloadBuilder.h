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
    /** Writes Value into the provider's max-token field as-is (see SetOutputBudget for the setting-driven budget) */
    void SetMaxTokens(int32 Value);

    /**
     * Writes the output budget for the configured provider and model: ResolveMaxOutputTokens(Requested,
     * EstimatedInputTokens), or no field when that is 0. Call after a ConfigureFor* call.
     * @param Requested the provider's Max Output Tokens setting (FN2CLLMConfig::MaxOutputTokens); 0 is Auto
     * @param EstimatedInputTokens estimate of everything the request sends (system and user messages)
     */
    void SetOutputBudget(int32 Requested, int32 EstimatedInputTokens);

    /** Kept free in the context window in Auto, to absorb the error of the 4-characters-per-token input estimate */
    static constexpr int32 AutoOutputMarginTokens = 4096;

    /** Smallest budget Auto sends when the prompt leaves less room (the request then fails on the provider's side) */
    static constexpr int32 MinAutoOutputTokens = 1024;

    /**
     * Auto budget for an Anthropic model ID the registry does not know. The Messages API requires max_tokens, and
     * every Claude model since Claude Opus 4 accepts at least 32000 output tokens.
     */
    static constexpr int32 AnthropicUnregisteredMaxTokens = 32000;

    /**
     * Ceiling on the Auto budget for cloud providers while requests are not streamed. A very large max_tokens on a
     * non-streaming call risks long-request timeouts (Anthropic steers requests that can run past about 10 minutes
     * to streaming, and UE's HTTP activity timeout applies too). Explicit values are not affected. Remove this cap
     * (and DeepSeekReasoningAutoCap) when SSE streaming lands.
     */
    static constexpr int32 NonStreamingAutoCap = 32000;

    /** Auto ceiling for DeepSeek reasoning entries: reasoning counts against max_tokens, and 65536 is the API default */
    static constexpr int32 DeepSeekReasoningAutoCap = 65536;

    /** The non-streaming Auto ceiling for this provider and registry entry */
    static int32 NonStreamingAutoCapFor(EN2CLLMProvider Provider, const struct FN2CModelMetadata* Meta);

    /**
     * Output budget a request sends; 0 means no max-token field at all.
     * - Requested > 0 (explicit): Requested, capped at Meta->MaxOutputTokens when the model is registered.
     * - Requested <= 0 (Auto), LM Studio or Ollama: 0, so a local server (LM Studio, or vLLM through the LM Studio
     *   provider) uses whatever context the prompt leaves; a fixed value would overflow a small max_model_len.
     * - Auto, registered cloud model: min(MaxOutputTokens, NonStreamingAutoCapFor(), ContextWindowSize -
     *   EstimatedInputTokens - AutoOutputMarginTokens), and at least MinAutoOutputTokens (or that ceiling if lower).
     * - Auto, unregistered cloud model: AnthropicUnregisteredMaxTokens for Anthropic (the field is required), else
     *   0 so the provider's own default applies.
     * @param Meta the model's registry entry; ignored unless it belongs to Provider
     */
    static int32 ResolveMaxOutputTokens(EN2CLLMProvider Provider, const struct FN2CModelMetadata* Meta, int32 Requested, int32 EstimatedInputTokens);

    /** Message building */
    void AddSystemMessage(const FString& Content);
    void AddUserMessage(const FString& Content);

    
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
