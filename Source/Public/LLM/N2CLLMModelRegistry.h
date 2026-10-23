// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.h"
#include "N2CLLMModels.h"
#include "N2CLLMPricing.h"
#include "N2CLLMModelRegistry.generated.h"

/**
 * Unified model metadata structure - single source of truth for all model properties.
 * Contains all information about a specific LLM model including pricing, capabilities,
 * and default generation parameters.
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CModelMetadata
{
	GENERATED_BODY()

	/** API identifier sent to the provider (e.g., "claude-sonnet-4-5-20250929") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString ModelId;

	/** Human-readable display name (e.g., "Claude Sonnet 4.5") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString DisplayName;

	/** Provider this model belongs to */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

	// === Pricing (per 1M tokens in USD) ===

	/** Cost per 1M input tokens (USD) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pricing")
	float InputCostPer1M = 0.0f;

	/** Cost per 1M output tokens (USD) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pricing")
	float OutputCostPer1M = 0.0f;

	// === Capabilities ===

	/** Maximum context window size in tokens */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	int32 ContextWindowSize = 128000;

	/** Maximum output tokens this model can generate */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	int32 MaxOutputTokens = 16000;

	/** Whether this model supports system/developer prompts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bSupportsSystemPrompts = true;

	/** Whether this model supports structured JSON output */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bSupportsStructuredOutput = true;

	/** Whether this is a reasoning/thinking model (affects temperature handling) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bIsReasoningModel = false;

	/** Whether this model requires the OpenAI Responses API instead of Chat Completions */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bRequiresResponsesAPI = false;

	/**
	 * Whether the provider accepts sampling parameters (temperature, top_p, top_k) for this model.
	 * When false the payload builder sends none of them, so the provider's own default applies.
	 * Set from provider docs; false when unsure (current Claude, OpenAI reasoning and Gemini 3 models
	 * reject or discourage non-default sampling values).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bAcceptsSamplingParams = false;

	/** Whether this OpenAI model takes max_completion_tokens instead of max_tokens (reasoning models) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bUsesMaxCompletionTokens = false;

	// === Generation Defaults ===

	/** Default temperature for this model, sent only when bAcceptsSamplingParams is true */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	float DefaultTemperature = 0.0f;

	/** Default max tokens to request */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	int32 DefaultMaxTokens = 16000;

	// === Metadata ===

	/**
	 * Whether the provider has deprecated this model: it is retired already, or it has an announced shutdown date.
	 * A default model is never deprecated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	bool bIsDeprecated = false;

	/**
	 * Whether the provider has shut this model down, so every request to it fails. Retired models stay in their
	 * enum (UMETA(Hidden)) so saved settings still load; they are not offered in Project Settings, the context
	 * window model picker or get-available-llm-providers, and a saved retired selection moves to the default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	bool bIsRetired = false;

	/** The provider's shutdown date (YYYY-MM-DD), past or announced; empty when none is known */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	FString ShutdownDate;

	/** Default constructor */
	FN2CModelMetadata() = default;

	/** Full initialization constructor */
	FN2CModelMetadata(
		const FString& InModelId,
		const FString& InDisplayName,
		EN2CLLMProvider InProvider,
		float InInputCost,
		float InOutputCost,
		int32 InContextWindow = 128000,
		int32 InMaxOutput = 16000,
		bool bInSupportsSystem = true,
		bool bInSupportsStructured = true,
		bool bInIsReasoning = false,
		float InDefaultTemp = 0.0f,
		int32 InDefaultMaxTokens = 16000,
		bool bInRequiresResponsesAPI = false,
		bool bInAcceptsSamplingParams = false,
		bool bInUsesMaxCompletionTokens = false
	)
		: ModelId(InModelId)
		, DisplayName(InDisplayName)
		, Provider(InProvider)
		, InputCostPer1M(InInputCost)
		, OutputCostPer1M(InOutputCost)
		, ContextWindowSize(InContextWindow)
		, MaxOutputTokens(InMaxOutput)
		, bSupportsSystemPrompts(bInSupportsSystem)
		, bSupportsStructuredOutput(bInSupportsStructured)
		, bIsReasoningModel(bInIsReasoning)
		, bRequiresResponsesAPI(bInRequiresResponsesAPI)
		, bAcceptsSamplingParams(bInAcceptsSamplingParams)
		, bUsesMaxCompletionTokens(bInUsesMaxCompletionTokens)
		, DefaultTemperature(InDefaultTemp)
		, DefaultMaxTokens(InDefaultMaxTokens)
		, bIsDeprecated(false)
	{
	}

	/** Marks a model the provider has shut down (an empty date when the provider no longer lists one) */
	FN2CModelMetadata& Retired(const TCHAR* InShutdownDate)
	{
		bIsDeprecated = true;
		bIsRetired = true;
		ShutdownDate = InShutdownDate;
		return *this;
	}

	/** Marks a model the provider still serves but will shut down on the given date */
	FN2CModelMetadata& ShutsDown(const TCHAR* InShutdownDate)
	{
		bIsDeprecated = true;
		bIsRetired = false;
		ShutdownDate = InShutdownDate;
		return *this;
	}
};

/**
 * Centralized registry for all LLM model metadata.
 * Provides single source of truth for model properties, pricing, and capabilities.
 * Use FN2CLLMModelRegistry::Get() to access the singleton instance.
 */
class NODETOCODE_API FN2CLLMModelRegistry
{
public:
	/** Get singleton instance */
	static FN2CLLMModelRegistry& Get();

	// === Model Metadata Accessors ===

	/** Get metadata for a specific OpenAI model */
	const FN2CModelMetadata* GetModelMetadata(EN2COpenAIModel Model) const;

	/** Get metadata for a specific Anthropic model */
	const FN2CModelMetadata* GetModelMetadata(EN2CAnthropicModel Model) const;

	/** Get metadata for a specific Gemini model */
	const FN2CModelMetadata* GetModelMetadata(EN2CGeminiModel Model) const;

	/** Get metadata for a specific DeepSeek model */
	const FN2CModelMetadata* GetModelMetadata(EN2CDeepSeekModel Model) const;

	/** Get metadata by model ID string (for any provider) */
	const FN2CModelMetadata* GetModelMetadataById(const FString& ModelId) const;

	// === Model ID Accessors (replaces FN2CLLMModelUtils::Get*ModelValue) ===

	FString GetModelId(EN2COpenAIModel Model) const;
	FString GetModelId(EN2CAnthropicModel Model) const;
	FString GetModelId(EN2CGeminiModel Model) const;
	FString GetModelId(EN2CDeepSeekModel Model) const;

	// === Capability Checks ===

	bool SupportsSystemPrompts(EN2COpenAIModel Model) const;
	bool SupportsSystemPrompts(EN2CAnthropicModel Model) const;
	bool SupportsSystemPrompts(EN2CGeminiModel Model) const;
	bool SupportsSystemPrompts(EN2CDeepSeekModel Model) const;

	bool IsReasoningModel(EN2COpenAIModel Model) const;
	bool IsReasoningModel(EN2CAnthropicModel Model) const;
	bool IsReasoningModel(EN2CGeminiModel Model) const;
	bool IsReasoningModel(EN2CDeepSeekModel Model) const;

	/** Check if a model requires the OpenAI Responses API */
	bool RequiresResponsesAPI(EN2COpenAIModel Model) const;
	bool RequiresResponsesAPI(const FString& ModelId) const;

	// === Pricing Accessors (for backward compatibility with FN2CLLMModelUtils) ===

	FN2COpenAIPricing GetOpenAIPricing(EN2COpenAIModel Model) const;
	FN2CAnthropicPricing GetAnthropicPricing(EN2CAnthropicModel Model) const;
	FN2CGeminiPricing GetGeminiPricing(EN2CGeminiModel Model) const;
	FN2CDeepSeekPricing GetDeepSeekPricing(EN2CDeepSeekModel Model) const;

	// === Generation Defaults ===

	float GetDefaultTemperature(EN2COpenAIModel Model) const;
	float GetDefaultTemperature(EN2CAnthropicModel Model) const;
	float GetDefaultTemperature(EN2CGeminiModel Model) const;
	float GetDefaultTemperature(EN2CDeepSeekModel Model) const;

	int32 GetDefaultMaxTokens(EN2COpenAIModel Model) const;
	int32 GetDefaultMaxTokens(EN2CAnthropicModel Model) const;
	int32 GetDefaultMaxTokens(EN2CGeminiModel Model) const;
	int32 GetDefaultMaxTokens(EN2CDeepSeekModel Model) const;

	// === Provider Enumeration ===

	/** Get all models for a specific provider */
	TArray<FN2CModelMetadata> GetAllModelsForProvider(EN2CLLMProvider Provider) const;

	/** Get all OpenAI models */
	const TMap<EN2COpenAIModel, FN2CModelMetadata>& GetAllOpenAIModels() const { return OpenAIModels; }

	/** Get all Anthropic models */
	const TMap<EN2CAnthropicModel, FN2CModelMetadata>& GetAllAnthropicModels() const { return AnthropicModels; }

	/** Get all Gemini models */
	const TMap<EN2CGeminiModel, FN2CModelMetadata>& GetAllGeminiModels() const { return GeminiModels; }

	/** Get all DeepSeek models */
	const TMap<EN2CDeepSeekModel, FN2CModelMetadata>& GetAllDeepSeekModels() const { return DeepSeekModels; }

private:
	FN2CLLMModelRegistry();
	~FN2CLLMModelRegistry() = default;

	/** Initialize all model metadata - called once on first Get() */
	void InitializeRegistry();

	/** Model metadata storage by provider */
	TMap<EN2COpenAIModel, FN2CModelMetadata> OpenAIModels;
	TMap<EN2CAnthropicModel, FN2CModelMetadata> AnthropicModels;
	TMap<EN2CGeminiModel, FN2CModelMetadata> GeminiModels;
	TMap<EN2CDeepSeekModel, FN2CModelMetadata> DeepSeekModels;

	/** Index by model ID for fast lookup across all providers */
	TMap<FString, const FN2CModelMetadata*> ModelIdIndex;

	/** Flag to track initialization state */
	bool bIsInitialized = false;
};
