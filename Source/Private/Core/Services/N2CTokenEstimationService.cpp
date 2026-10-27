// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Services/N2CTokenEstimationService.h"
#include "Core/N2CSettings.h"
#include "Core/N2CNodeCollector.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CLLMTypes.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CGraphUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"
#include "Core/N2CGraphStateTypes.h"
#include "Editor.h"

FN2CTokenEstimationService& FN2CTokenEstimationService::Get()
{
	static FN2CTokenEstimationService Instance;
	return Instance;
}

FN2CTokenEstimationService::FN2CTokenEstimationService()
{
}

FN2CTokenEstimationService::~FN2CTokenEstimationService()
{
	Shutdown();
}

void FN2CTokenEstimationService::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	// Subscribe to settings changes
	SettingsChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
		this, &FN2CTokenEstimationService::OnSettingsPropertyChanged);

	// Each compile names its Blueprint here (OnBlueprintCompiled does not), so only that Blueprint's estimates go stale
	if (GEditor)
	{
		BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddRaw(this, &FN2CTokenEstimationService::OnBlueprintPreCompile);
	}

	// Initial refresh of model info
	RefreshModelInfo();

	bIsInitialized = true;
}

void FN2CTokenEstimationService::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	// Unsubscribe from settings changes
	if (SettingsChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(SettingsChangedHandle);
		SettingsChangedHandle.Reset();
	}

	if (BlueprintPreCompileHandle.IsValid() && GEditor)
	{
		GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
		BlueprintPreCompileHandle.Reset();
	}

	UnwatchAll();
	TokenCache.Empty();
	bIsInitialized = false;
}

FString FN2CTokenEstimationService::MakeCacheKey(const FN2CTagInfo& GraphInfo)
{
	return FN2CGraphKey::NormalizeBlueprintPath(GraphInfo.BlueprintPath).ToString() + TEXT("|") + GraphInfo.GraphGuid;
}

uint32 FN2CTokenEstimationService::GetBlueprintRevision(const FString& BlueprintPath) const
{
	const uint32* Revision = BlueprintRevisions.Find(BlueprintPath);
	return Revision ? *Revision : 0;
}

bool FN2CTokenEstimationService::IsEntryCurrent(const FN2CTokenCacheEntry& Entry) const
{
	return Entry.Depth == GetTranslationDepth()
		&& Entry.BlueprintRevision == GetBlueprintRevision(Entry.BlueprintPath)
		&& (!Entry.ExternalRevision.IsSet() || Entry.ExternalRevision.GetValue() == AnyCompileRevision);
}

bool FN2CTokenEstimationService::IsEstimateCurrent(const FN2CTagInfo& GraphInfo) const
{
	const FN2CTokenCacheEntry* Entry = TokenCache.Find(MakeCacheKey(GraphInfo));
	return Entry && IsEntryCurrent(*Entry);
}

FN2CTokenEstimate FN2CTokenEstimationService::GetEstimate(const FN2CTagInfo& GraphInfo)
{
	const FString Key = MakeCacheKey(GraphInfo);
	if (FN2CTokenCacheEntry* Cached = TokenCache.Find(Key))
	{
		if (IsEntryCurrent(*Cached))
		{
			return Cached->Estimate;
		}
		TokenCache.Remove(Key);
	}

	const int32 Depth = GetTranslationDepth();
	UBlueprint* Blueprint = nullptr;
	TArray<FGuid> GraphGuids;
	const FString JsonContent = SerializeGraphToJson(GraphInfo, Depth, Blueprint, GraphGuids);

	FN2CTokenCacheEntry Entry;
	Entry.BlueprintPath = FN2CGraphKey::NormalizeBlueprintPath(GraphInfo.BlueprintPath).ToString();
	Entry.Depth = Depth;
	Entry.CacheTimestamp = FDateTime::Now();
	if (!JsonContent.IsEmpty())
	{
		Entry.Estimate.Tokens = EstimateTokensForChars(JsonContent.Len());
		Entry.Estimate.NestedGraphCount = FMath::Max(0, GraphGuids.Num() - 1);
		Entry.Estimate.bValid = true;
		Entry.CachedJsonSize = JsonContent.Len();
	}

	if (Blueprint)
	{
		// Cached against this Blueprint's change counter; a nested graph from another Blueprint (a macro or function
		// library) ties the entry to every compile as well
		Entry.BlueprintPath = FN2CGraphKey::NormalizeBlueprintPath(FSoftObjectPath(Blueprint)).ToString();
		WatchBlueprint(Blueprint);
		TSet<FGuid> OwnGraphs;
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (const UEdGraph* Graph : AllGraphs)
		{
			if (Graph)
			{
				OwnGraphs.Add(Graph->GraphGuid);
			}
		}
		for (const FGuid& Guid : GraphGuids)
		{
			if (!OwnGraphs.Contains(Guid))
			{
				Entry.ExternalRevision = AnyCompileRevision;
				break;
			}
		}
	}
	Entry.BlueprintRevision = GetBlueprintRevision(Entry.BlueprintPath);

	const FN2CTokenEstimate Result = Entry.Estimate;
	TokenCache.Add(Key, MoveTemp(Entry));
	EvictOldestCacheEntries();
	return Result;
}

int32 FN2CTokenEstimationService::GetTokenEstimate(const FN2CTagInfo& GraphInfo)
{
	return GetEstimate(GraphInfo).Tokens;
}

int32 FN2CTokenEstimationService::GetTokenEstimateWithNesting(const FN2CTagInfo& GraphInfo, int32& OutNestedGraphCount)
{
	const FN2CTokenEstimate Estimate = GetEstimate(GraphInfo);
	OutNestedGraphCount = Estimate.NestedGraphCount;
	return Estimate.Tokens;
}

void FN2CTokenEstimationService::InvalidateCache(const FString& GraphGuid)
{
	// Every Blueprint's entry for this GUID (a duplicated Blueprint's graph has the same GUID)
	const FString Suffix = TEXT("|") + GraphGuid;
	for (auto It = TokenCache.CreateIterator(); It; ++It)
	{
		if (It.Key().EndsWith(Suffix))
		{
			It.RemoveCurrent();
		}
	}
	OnCacheInvalidated.Broadcast();
}

void FN2CTokenEstimationService::InvalidateAllCache()
{
	TokenCache.Empty();
	OnCacheInvalidated.Broadcast();
}

void FN2CTokenEstimationService::NotifyBlueprintChanged(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}
	const FSoftObjectPath Path = FN2CGraphKey::NormalizeBlueprintPath(FSoftObjectPath(Blueprint));
	++BlueprintRevisions.FindOrAdd(Path.ToString());
	OnEstimatesStale.Broadcast(Path);
}

void FN2CTokenEstimationService::OnBlueprintPreCompile(UBlueprint* Blueprint)
{
	++AnyCompileRevision;
	NotifyBlueprintChanged(Blueprint);
}

void FN2CTokenEstimationService::WatchBlueprint(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return;
	}
	const TWeakObjectPtr<UBlueprint> WeakBlueprint(Blueprint);
	if (!WatchedBlueprints.Contains(WeakBlueprint))
	{
		// A structural change (a function added, removed or renamed) bumps the counter and picks up new graphs
		WatchedBlueprints.Add(WeakBlueprint, Blueprint->OnChanged().AddLambda([this, WeakBlueprint](UBlueprint* Changed)
		{
			NotifyBlueprintChanged(Changed);
			WatchBlueprint(WeakBlueprint.Get());
		}));
	}

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		const TWeakObjectPtr<UEdGraph> WeakGraph(Graph);
		if (Graph && !WatchedGraphs.Contains(WeakGraph))
		{
			// A node added, removed, linked or edited: this Blueprint's estimates go stale, compiled or not
			WatchedGraphs.Add(WeakGraph, Graph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateLambda(
				[this, WeakBlueprint](const FEdGraphEditAction&)
				{
					NotifyBlueprintChanged(WeakBlueprint.Get());
				})));
		}
	}
}

void FN2CTokenEstimationService::UnwatchAll()
{
	for (const auto& Pair : WatchedGraphs)
	{
		if (UEdGraph* Graph = Pair.Key.Get())
		{
			Graph->RemoveOnGraphChangedHandler(Pair.Value);
		}
	}
	WatchedGraphs.Empty();
	for (const auto& Pair : WatchedBlueprints)
	{
		if (UBlueprint* Blueprint = Pair.Key.Get())
		{
			Blueprint->OnChanged().Remove(Pair.Value);
		}
	}
	WatchedBlueprints.Empty();
}

int32 FN2CTokenEstimationService::GetTranslationDepth() const
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings ? Settings->TranslationDepth : 0;
}

bool FN2CTokenEstimationService::IsNestedTranslationEnabled() const
{
	return GetTranslationDepth() > 0;
}

int32 FN2CTokenEstimationService::EstimateTokensForChars(int32 CharCount)
{
	return FMath::CeilToInt(FMath::Max(CharCount, 0) / 4.0f);
}

bool FN2CTokenEstimationService::WarnIfPromptExceedsContext(int32 EstimatedPromptTokens, int32 ContextWindow, const TCHAR* ProviderLabel)
{
	if (ContextWindow <= 0 || EstimatedPromptTokens <= ContextWindow)
	{
		return false;
	}
	FN2CLogger::Get().LogWarning(FString::Printf(
		TEXT("Estimated prompt of about %d tokens is larger than the %s Context Window of %d tokens; the server may cut the prompt without an error, giving a wrong or failed translation. Raise Context Window (32K-64K recommended) or set it to 0 to use the server's context length."),
		EstimatedPromptTokens, ProviderLabel, ContextWindow), TEXT("TokenEstimation"));
	return true;
}

float FN2CTokenEstimationService::CalculateCost(int32 TokenCount) const
{
	if (bCachedIsLocal || CachedInputCostPer1M <= 0.0f)
	{
		return 0.0f;
	}

	// Calculate cost: (tokens / 1,000,000) * cost per 1M
	return (static_cast<float>(TokenCount) / 1000000.0f) * CachedInputCostPer1M;
}

void FN2CTokenEstimationService::RefreshModelInfo()
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	if (!Settings)
	{
		return;
	}

	switch (Settings->Provider)
	{
	case EN2CLLMProvider::Ollama:
		CachedModelName = Settings->OllamaModel;
		// The Context Window the requests send; unset (0), the server's own length applies, which is not known here
		CachedFullContextWindow = Settings->OllamaConfig.NumCtx > 0 ? Settings->OllamaConfig.NumCtx : 128000;
		CachedUsableContextWindow = FMath::RoundToInt(CachedFullContextWindow * ContextWindowFactor);
		CachedInputCostPer1M = 0.0f;
		bCachedIsLocal = true;
		return;
	case EN2CLLMProvider::LMStudio:
		CachedModelName = Settings->LMStudioModel;
		CachedFullContextWindow = 128000; // Default estimate for local models
		CachedUsableContextWindow = FMath::RoundToInt(CachedFullContextWindow * ContextWindowFactor);
		CachedInputCostPer1M = 0.0f;
		bCachedIsLocal = true;
		return;
	default:
		break;
	}

	// Cloud providers: the selected model ID's catalog row; the price is the user's override when there is one
	const FN2CModelMetadata* Metadata = Settings->GetActiveModelMetadataForProvider(Settings->Provider);
	CachedInputCostPer1M = Settings->GetActivePricingForProvider(Settings->Provider).InputCost;
	bCachedIsLocal = false;
	if (Metadata)
	{
		CachedModelName = Metadata->DisplayName;
		CachedFullContextWindow = Metadata->ContextWindowSize;
	}
	else
	{
		// A custom model ID the catalog does not list: its own name, and a common context size as the estimate
		const FString ModelId = Settings->GetActiveModelForProvider(Settings->Provider);
		CachedModelName = ModelId.IsEmpty() ? FString(TEXT("Unknown Model")) : ModelId;
		CachedFullContextWindow = 128000;
	}
	CachedUsableContextWindow = FMath::RoundToInt(CachedFullContextWindow * ContextWindowFactor);
}

void FN2CTokenEstimationService::OnSettingsPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	// Check if this is the settings object
	if (!Object || !Object->IsA<UN2CSettings>())
	{
		return;
	}

	// Check if relevant property changed
	FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, TranslationDepth))
	{
		// Estimates are cached per depth: the overlays and lists take theirs again at the new one
		OnCacheInvalidated.Broadcast();
		return;
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAIModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAICustomModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicCustomModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, GeminiModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, GeminiCustomModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekCustomModelId) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekReasoningEffort) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, ModelPricing) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, OllamaModel) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, LMStudioModel) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FN2COllamaConfig, NumCtx))
	{
		// Token counts do not depend on the model; its context window and price do, and listeners re-read those
		RefreshModelInfo();
		OnModelChanged.Broadcast();
	}
}

FString FN2CTokenEstimationService::SerializeGraphToJson(const FN2CTagInfo& GraphInfo, int32 Depth, UBlueprint*& OutBlueprint,
	TArray<FGuid>& OutGraphGuids)
{
	OutBlueprint = nullptr;
	OutGraphGuids.Reset();

	// The same resolver as translation: every graph of the Blueprint, interface implementations and collapsed graphs too
	UBlueprint* Blueprint = nullptr;
	FString ResolveError;
	UEdGraph* FoundGraph = FN2CGraphUtils::ResolveTaggedGraph(GraphInfo.BlueprintPath, GraphInfo.GraphGuid,
		GraphInfo.GraphName, Blueprint, ResolveError);
	OutBlueprint = Blueprint;
	if (FoundGraph && !FN2CGraphUtils::CanTranslateGraph(FoundGraph, &ResolveError))
	{
		FoundGraph = nullptr;
	}
	if (!FoundGraph)
	{
		// No notification: the estimate refreshes on its own, and the overlay of such a graph has its actions disabled
		FN2CLogger::Get().LogWarning(ResolveError, TEXT("TokenEstimationService"));
		return FString();
	}

	// One capture at the configured depth, as the Translate button sends it: each nested graph is taken in once,
	// shallowest first, so a graph reachable along several call paths counts once, at its shortest one
	FN2CCaptureOptions Options;
	Options.Depth = Depth;
	++CaptureCount;
	const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*FoundGraph, Options);
	// One line per capture: the estimate is cached, so this shows when (and for which graph) it was taken again
	FN2CLogger::Get().Log(FString::Printf(TEXT("Token estimate: captured %s in %s at depth %d"), *GraphInfo.GraphName,
		*GraphInfo.BlueprintPath, Depth), EN2CLogSeverity::Info);
	if (Capture.HasError())
	{
		// A graph of only the editor's placeholder events, or nothing capture can read
		return FString();
	}

	OutGraphGuids = Capture.GetValue().GraphGuids;
	return FN2CSerializer::ToJson(Capture.GetValue().Blueprint, EN2CJsonStyle::Condensed);
}

void FN2CTokenEstimationService::EvictOldestCacheEntries()
{
	if (TokenCache.Num() <= MaxCacheEntries)
	{
		return;
	}

	// Find and remove oldest entries
	while (TokenCache.Num() > MaxCacheEntries)
	{
		FString OldestKey;
		FDateTime OldestTime = FDateTime::MaxValue();

		for (const auto& Pair : TokenCache)
		{
			if (Pair.Value.CacheTimestamp < OldestTime)
			{
				OldestTime = Pair.Value.CacheTimestamp;
				OldestKey = Pair.Key;
			}
		}

		if (!OldestKey.IsEmpty())
		{
			TokenCache.Remove(OldestKey);
		}
		else
		{
			break;
		}
	}
}

FString FN2CTokenEstimationService::FormatNumber(int64 Number)
{
	FString Result = FString::Printf(TEXT("%lld"), Number);
	int32 InsertPosition = Result.Len() - 3;

	while (InsertPosition > 0)
	{
		Result.InsertAt(InsertPosition, TEXT(","));
		InsertPosition -= 3;
	}

	return Result;
}
