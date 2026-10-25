// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.h"
#include "N2CLLMModels.h"
#include "N2CLLMPricing.h"
#include "N2CModelCatalog.h"

/**
 * Enum-keyed view of the model catalog (FN2CModelCatalog, loaded from Content/Data/N2CModelCatalog.json) for the
 * callers that still select models through EN2COpenAIModel, EN2CAnthropicModel, EN2CGeminiModel and
 * EN2CDeepSeekModel. It holds no model data of its own: each catalog row is filed under the enumerator its
 * legacy_enum names, and the view is rebuilt when the catalog reloads. Later cards move these callers to the
 * catalog's string IDs and remove the wrappers. Use FN2CLLMModelRegistry::Get() to access the singleton instance.
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

	/**
	 * Get metadata by model ID string (for any provider). When several entries share the ID (DeepSeek thinking
	 * modes), this is the first live one registered, which sends the provider's default mode for that ID.
	 */
	const FN2CModelMetadata* GetModelMetadataById(const FString& ModelId) const;

	/**
	 * Get metadata by model ID and thinking control: the entry with both, or GetModelMetadataById(ModelId) when
	 * ReasoningEffort is empty or no entry with that ID has it.
	 */
	const FN2CModelMetadata* GetModelMetadataById(const FString& ModelId, const FString& ReasoningEffort) const;

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

	/** Check if a model requires the OpenAI Responses API */
	bool RequiresResponsesAPI(EN2COpenAIModel Model) const;
	bool RequiresResponsesAPI(const FString& ModelId) const;

	// === Pricing Accessors (for backward compatibility with FN2CLLMModelUtils) ===

	FN2COpenAIPricing GetOpenAIPricing(EN2COpenAIModel Model) const;
	FN2CAnthropicPricing GetAnthropicPricing(EN2CAnthropicModel Model) const;
	FN2CGeminiPricing GetGeminiPricing(EN2CGeminiModel Model) const;
	FN2CDeepSeekPricing GetDeepSeekPricing(EN2CDeepSeekModel Model) const;

	// === Provider Enumeration ===

	/** Get all models for a specific provider, retired ones included, in catalog order */
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

	/** Files the catalog's rows under their enumerators; runs on first Get() and whenever the catalog reloads */
	void InitializeRegistry();

	/** Catalog generation the maps were built from */
	uint32 BuiltGeneration = 0;

	/** Model metadata storage by provider */
	TMap<EN2COpenAIModel, FN2CModelMetadata> OpenAIModels;
	TMap<EN2CAnthropicModel, FN2CModelMetadata> AnthropicModels;
	TMap<EN2CGeminiModel, FN2CModelMetadata> GeminiModels;
	TMap<EN2CDeepSeekModel, FN2CModelMetadata> DeepSeekModels;

	/** Catalog rows with no enumerator (a model added only in the catalog file), per provider in catalog order */
	TArray<FN2CModelMetadata> UnmappedModels;

	/** Index by model ID for fast lookup across all providers (first live entry per ID) */
	TMap<FString, const FN2CModelMetadata*> ModelIdIndex;

	/** Every entry per model ID, for IDs shared by several thinking modes */
	TMultiMap<FString, const FN2CModelMetadata*> ModelIdEntries;

};
