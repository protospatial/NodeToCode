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
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_Composite.h"
#include "K2Node_MacroInstance.h"
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

	// Subscribe to Blueprint compilation (void delegate - no Blueprint parameter)
	if (GEditor)
	{
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddLambda(
			[this]() { OnBlueprintCompiled(); });
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

	// Unsubscribe from Blueprint compilation
	if (BlueprintCompiledHandle.IsValid() && GEditor)
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
		BlueprintCompiledHandle.Reset();
	}

	TokenCache.Empty();
	bIsInitialized = false;
}

int32 FN2CTokenEstimationService::GetTokenEstimate(const FN2CTagInfo& GraphInfo)
{
	// Check cache first
	if (FN2CTokenCacheEntry* CacheEntry = TokenCache.Find(GraphInfo.GraphGuid))
	{
		// Validate cache - check if Blueprint is dirty
		UBlueprint* Blueprint = Cast<UBlueprint>(FSoftObjectPath(GraphInfo.BlueprintPath).TryLoad());
		if (Blueprint && Blueprint->GetPackage())
		{
			if (!Blueprint->GetPackage()->IsDirty())
			{
				// Cache is valid
				return CacheEntry->CachedTokenCount;
			}
		}
		// Blueprint is dirty, invalidate cache entry
		TokenCache.Remove(GraphInfo.GraphGuid);
	}

	// Cache miss - serialize and calculate
	FString JsonContent = SerializeGraphToJson(GraphInfo);
	if (JsonContent.IsEmpty())
	{
		return 0;
	}

	// Estimate tokens: ~4 characters per token
	int32 TokenEstimate = FMath::CeilToInt(JsonContent.Len() / 4.0f);

	// Store in cache
	TokenCache.Add(GraphInfo.GraphGuid, FN2CTokenCacheEntry(TokenEstimate, JsonContent.Len(), GraphInfo.BlueprintPath));

	// Evict old entries if cache is too large
	EvictOldestCacheEntries();

	return TokenEstimate;
}

void FN2CTokenEstimationService::InvalidateCache(const FString& GraphGuid)
{
	TokenCache.Remove(GraphGuid);
	OnCacheInvalidated.Broadcast();
}

void FN2CTokenEstimationService::InvalidateAllCache()
{
	TokenCache.Empty();
	OnCacheInvalidated.Broadcast();
}

int32 FN2CTokenEstimationService::GetTokenEstimateWithNesting(const FN2CTagInfo& GraphInfo, int32& OutNestedGraphCount)
{
	OutNestedGraphCount = 0;

	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	int32 MaxDepth = Settings ? Settings->TranslationDepth : 0;

	if (MaxDepth <= 0)
	{
		// No nesting enabled, just return single graph estimate
		return GetTokenEstimate(GraphInfo);
	}

	// Use recursive estimation with cycle detection
	TSet<FString> VisitedGraphGuids;
	return GetTokenEstimateRecursive(GraphInfo, 0, MaxDepth, VisitedGraphGuids, OutNestedGraphCount);
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

int32 FN2CTokenEstimationService::GetTokenEstimateRecursive(
	const FN2CTagInfo& GraphInfo,
	int32 CurrentDepth,
	int32 MaxDepth,
	TSet<FString>& VisitedGraphGuids,
	int32& OutNestedGraphCount)
{
	// Prevent cycles
	if (VisitedGraphGuids.Contains(GraphInfo.GraphGuid))
	{
		return 0;
	}
	VisitedGraphGuids.Add(GraphInfo.GraphGuid);

	// Get token estimate for this graph
	int32 TotalTokens = GetTokenEstimate(GraphInfo);

	// If we've reached max depth, don't recurse further
	if (CurrentDepth >= MaxDepth)
	{
		return TotalTokens;
	}

	// Find referenced graphs and recurse
	TArray<FN2CTagInfo> ReferencedGraphs;
	FindReferencedGraphs(GraphInfo, ReferencedGraphs);

	for (const FN2CTagInfo& RefGraph : ReferencedGraphs)
	{
		if (!VisitedGraphGuids.Contains(RefGraph.GraphGuid))
		{
			OutNestedGraphCount++;
			TotalTokens += GetTokenEstimateRecursive(RefGraph, CurrentDepth + 1, MaxDepth, VisitedGraphGuids, OutNestedGraphCount);
		}
	}

	return TotalTokens;
}

void FN2CTokenEstimationService::FindReferencedGraphs(const FN2CTagInfo& GraphInfo, TArray<FN2CTagInfo>& OutReferencedGraphs)
{
	// The same resolver as SerializeGraphToJson (a failure is logged there)
	UBlueprint* Blueprint = nullptr;
	FString ResolveError;
	UEdGraph* FoundGraph = FN2CGraphUtils::ResolveTaggedGraph(GraphInfo.BlueprintPath, GraphInfo.GraphGuid,
		GraphInfo.GraphName, Blueprint, ResolveError);
	if (!FoundGraph)
	{
		return;
	}

	// Track already-added graphs to avoid duplicates
	TSet<FString> AddedGuids;

	// Iterate through nodes looking for function calls to user-created functions
	for (UEdGraphNode* Node : FoundGraph->Nodes)
	{
		// Check for function calls
		if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (UFunction* Function = FuncNode->GetTargetFunction())
			{
				if (UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(Function->GetOwnerClass()))
				{
					if (UBlueprint* FunctionBlueprint = Cast<UBlueprint>(BlueprintClass->ClassGeneratedBy))
					{
						// Check if it's a user-created Blueprint
						FString BPPath = FunctionBlueprint->GetPathName();
						if (BPPath.Contains(TEXT("/Game/")) || BPPath.Contains(TEXT("/Content/")))
						{
							// Find the function graph
							for (UEdGraph* FuncGraph : FunctionBlueprint->FunctionGraphs)
							{
								if (FuncGraph && FuncGraph->GetFName() == Function->GetFName())
								{
									FString FuncGraphGuid = FuncGraph->GraphGuid.ToString();
									if (!AddedGuids.Contains(FuncGraphGuid))
									{
										AddedGuids.Add(FuncGraphGuid);

										FN2CTagInfo RefInfo;
										RefInfo.GraphGuid = FuncGraphGuid;
										RefInfo.GraphName = FuncGraph->GetName();
										RefInfo.BlueprintPath = FunctionBlueprint->GetPathName();
										OutReferencedGraphs.Add(RefInfo);
									}
									break;
								}
							}
						}
					}
				}
			}
		}
		// Check for delegate creation nodes
		else if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
		{
			if (UClass* ScopeClass = CreateDelegateNode->GetScopeClass())
			{
				if (UBlueprint* BP = Cast<UBlueprint>(ScopeClass->ClassGeneratedBy))
				{
					FString BPPath = BP->GetPathName();
					if (BPPath.Contains(TEXT("/Game/")) || BPPath.Contains(TEXT("/Content/")))
					{
						for (UEdGraph* FuncGraph : BP->FunctionGraphs)
						{
							if (FuncGraph && FuncGraph->GetFName() == CreateDelegateNode->GetFunctionName())
							{
								FString FuncGraphGuid = FuncGraph->GraphGuid.ToString();
								if (!AddedGuids.Contains(FuncGraphGuid))
								{
									AddedGuids.Add(FuncGraphGuid);

									FN2CTagInfo RefInfo;
									RefInfo.GraphGuid = FuncGraphGuid;
									RefInfo.GraphName = FuncGraph->GetName();
									RefInfo.BlueprintPath = BP->GetPathName();
									OutReferencedGraphs.Add(RefInfo);
								}
								break;
							}
						}
					}
				}
			}
		}
		// Check for composite/collapsed graphs
		else if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
		{
			if (UEdGraph* BoundGraph = CompositeNode->BoundGraph)
			{
				FString CompositeGuid = BoundGraph->GraphGuid.ToString();
				if (!AddedGuids.Contains(CompositeGuid))
				{
					AddedGuids.Add(CompositeGuid);

					FN2CTagInfo RefInfo;
					RefInfo.GraphGuid = CompositeGuid;
					RefInfo.GraphName = BoundGraph->GetName();
					RefInfo.BlueprintPath = GraphInfo.BlueprintPath;
					OutReferencedGraphs.Add(RefInfo);
				}
			}
		}
		// Check for macro instances
		else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
		{
			if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
			{
				if (UBlueprint* MacroBP = Cast<UBlueprint>(MacroGraph->GetOuter()))
				{
					FString MacroPath = MacroBP->GetPathName();
					if (MacroPath.Contains(TEXT("/Game/")) || MacroPath.Contains(TEXT("/Content/")))
					{
						FString MacroGuid = MacroGraph->GraphGuid.ToString();
						if (!AddedGuids.Contains(MacroGuid))
						{
							AddedGuids.Add(MacroGuid);

							FN2CTagInfo RefInfo;
							RefInfo.GraphGuid = MacroGuid;
							RefInfo.GraphName = MacroGraph->GetName();
							RefInfo.BlueprintPath = MacroBP->GetPathName();
							OutReferencedGraphs.Add(RefInfo);
						}
					}
				}
			}
		}
	}
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
		RefreshModelInfo();
		// Model change invalidates cost calculations, so clear the cache
		InvalidateAllCache();
		OnModelChanged.Broadcast();
	}
}

void FN2CTokenEstimationService::OnBlueprintCompiled()
{
	// The OnBlueprintCompiled delegate doesn't tell us which Blueprint was compiled,
	// so we invalidate all cache entries. This is conservative but safe.
	// Individual cache entries also self-invalidate on access if their Blueprint is dirty.
	if (TokenCache.Num() > 0)
	{
		InvalidateAllCache();
	}
}

FString FN2CTokenEstimationService::SerializeGraphToJson(const FN2CTagInfo& GraphInfo)
{
	// The same resolver as translation: every graph of the Blueprint, interface implementations and collapsed graphs too
	UBlueprint* Blueprint = nullptr;
	FString ResolveError;
	UEdGraph* FoundGraph = FN2CGraphUtils::ResolveTaggedGraph(GraphInfo.BlueprintPath, GraphInfo.GraphGuid,
		GraphInfo.GraphName, Blueprint, ResolveError);
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

	// Collect nodes from the graph
	TArray<UK2Node*> CollectedNodes;
	if (!FN2CNodeCollector::Get().CollectNodesFromGraph(FoundGraph, CollectedNodes))
	{
		return FString();
	}

	if (CollectedNodes.IsEmpty())
	{
		return FString();
	}

	// Capture into this call's own result: an estimate never changes another capture (a translation in flight, MCP)
	const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(CollectedNodes);
	if (Capture.HasError())
	{
		return FString();
	}

	// Serialize to minified JSON for accurate size calculation
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
