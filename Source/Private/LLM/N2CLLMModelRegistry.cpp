// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModelRegistry.h"

namespace
{
	/** Files Row under the enumerator its LegacyEnumName names; false when TEnum has no such enumerator */
	template <typename TEnum>
	bool FileUnderEnum(const FN2CModelMetadata& Row, TMap<TEnum, FN2CModelMetadata>& Map)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		const int64 Value = (Enum && !Row.LegacyEnumName.IsEmpty()) ? Enum->GetValueByNameString(Row.LegacyEnumName) : INDEX_NONE;
		if (Value == INDEX_NONE)
		{
			return false;
		}
		Map.Add(static_cast<TEnum>(Value), Row);
		return true;
	}

	/** The provider default's model ID: the catalog's default row, or the compiled-in ID when it has none */
	FString DefaultModelId(EN2CLLMProvider Provider, const TCHAR* CompiledDefault)
	{
		const FN2CModelMetadata* Default = FN2CModelCatalog::Get().FindDefault(FN2CModelCatalog::ProviderIdFor(Provider));
		return Default ? Default->ModelId : FString(CompiledDefault);
	}
}

FN2CLLMModelRegistry& FN2CLLMModelRegistry::Get()
{
	static FN2CLLMModelRegistry Instance;
	const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
	if (Instance.BuiltGeneration != Catalog.GetGeneration())
	{
		Instance.InitializeRegistry();
	}
	return Instance;
}

FN2CLLMModelRegistry::FN2CLLMModelRegistry()
{
}

void FN2CLLMModelRegistry::InitializeRegistry()
{
	const FN2CModelCatalog& Catalog = FN2CModelCatalog::Get();
	OpenAIModels.Reset();
	AnthropicModels.Reset();
	GeminiModels.Reset();
	DeepSeekModels.Reset();
	UnmappedModels.Reset();
	ModelIdIndex.Reset();
	ModelIdEntries.Reset();

	// Catalog order, so maps iterate in the file's order (live rows, then the compiled-in retired ones)
	for (const FN2CModelMetadata& Row : Catalog.AllModels())
	{
		bool bFiled = false;
		switch (Row.Provider)
		{
		case EN2CLLMProvider::OpenAI: bFiled = FileUnderEnum(Row, OpenAIModels); break;
		case EN2CLLMProvider::Anthropic: bFiled = FileUnderEnum(Row, AnthropicModels); break;
		case EN2CLLMProvider::Gemini: bFiled = FileUnderEnum(Row, GeminiModels); break;
		case EN2CLLMProvider::DeepSeek: bFiled = FileUnderEnum(Row, DeepSeekModels); break;
		default: break;
		}
		if (!bFiled)
		{
			UnmappedModels.Add(Row);
		}
	}

	// For an ID shared by several entries the index keeps the first live one (maps iterate in insertion order);
	// a retired entry only fills an ID no live entry has. Built after every Add, so the pointers stay valid.
	auto IndexEntry = [this](const FN2CModelMetadata& Entry)
	{
		ModelIdEntries.Add(Entry.ModelId, &Entry);
		const FN2CModelMetadata*& Slot = ModelIdIndex.FindOrAdd(Entry.ModelId, nullptr);
		if (!Slot || (Slot->bIsRetired && !Entry.bIsRetired))
		{
			Slot = &Entry;
		}
	};
	for (const auto& Pair : OpenAIModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const auto& Pair : AnthropicModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const auto& Pair : GeminiModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const auto& Pair : DeepSeekModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const FN2CModelMetadata& Entry : UnmappedModels)
	{
		IndexEntry(Entry);
	}

	BuiltGeneration = Catalog.GetGeneration();
}

// ============================================================================
// Model Metadata Accessors
// ============================================================================

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2COpenAIModel Model) const
{
	return OpenAIModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2CAnthropicModel Model) const
{
	return AnthropicModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2CGeminiModel Model) const
{
	return GeminiModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2CDeepSeekModel Model) const
{
	return DeepSeekModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadataById(const FString& ModelId) const
{
	const FN2CModelMetadata* const* Found = ModelIdIndex.Find(ModelId);
	return Found ? *Found : nullptr;
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadataById(const FString& ModelId, const FString& ReasoningEffort) const
{
	if (!ReasoningEffort.IsEmpty())
	{
		TArray<const FN2CModelMetadata*, TInlineAllocator<4>> Entries;
		ModelIdEntries.MultiFind(ModelId, Entries, /*bMaintainOrder*/ true);
		for (const FN2CModelMetadata* Entry : Entries)
		{
			if (Entry->ReasoningEffort == ReasoningEffort && !Entry->bIsRetired)
			{
				return Entry;
			}
		}
	}
	return GetModelMetadataById(ModelId);
}

// ============================================================================
// Model ID Accessors
// ============================================================================

FString FN2CLLMModelRegistry::GetModelId(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return DefaultModelId(EN2CLLMProvider::OpenAI, TEXT("gpt-6.1-sol"));
}

FString FN2CLLMModelRegistry::GetModelId(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return DefaultModelId(EN2CLLMProvider::Anthropic, TEXT("claude-sonnet-5-5"));
}

FString FN2CLLMModelRegistry::GetModelId(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return DefaultModelId(EN2CLLMProvider::Gemini, TEXT("gemini-3.8-flash"));
}

FString FN2CLLMModelRegistry::GetModelId(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return DefaultModelId(EN2CLLMProvider::DeepSeek, TEXT("deepseek-flash"));
}

// ============================================================================
// Capability Checks
// ============================================================================

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true; // Default to true
}

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true;
}

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true;
}

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true;
}

bool FN2CLLMModelRegistry::RequiresResponsesAPI(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bRequiresResponsesAPI;
	}
	return false;
}

bool FN2CLLMModelRegistry::RequiresResponsesAPI(const FString& ModelId) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadataById(ModelId))
	{
		return Meta->bRequiresResponsesAPI;
	}
	return false;
}

// ============================================================================
// Pricing Accessors (Backward Compatibility)
// ============================================================================

FN2COpenAIPricing FN2CLLMModelRegistry::GetOpenAIPricing(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2COpenAIPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2COpenAIPricing();
}

FN2CAnthropicPricing FN2CLLMModelRegistry::GetAnthropicPricing(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2CAnthropicPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2CAnthropicPricing();
}

FN2CGeminiPricing FN2CLLMModelRegistry::GetGeminiPricing(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2CGeminiPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2CGeminiPricing();
}

FN2CDeepSeekPricing FN2CLLMModelRegistry::GetDeepSeekPricing(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2CDeepSeekPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2CDeepSeekPricing();
}

// ============================================================================
// Provider Enumeration
// ============================================================================

TArray<FN2CModelMetadata> FN2CLLMModelRegistry::GetAllModelsForProvider(EN2CLLMProvider Provider) const
{
	// Ollama and LM Studio have no catalog models: they serve whatever the user installed
	TArray<FN2CModelMetadata> Result;
	for (const FN2CModelMetadata* Row : FN2CModelCatalog::Get().List(FN2CModelCatalog::ProviderIdFor(Provider), /*bIncludeRetired*/ true))
	{
		Result.Add(*Row);
	}
	return Result;
}
