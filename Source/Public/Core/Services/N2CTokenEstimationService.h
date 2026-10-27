// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"

class UBlueprint;
class UEdGraph;

/** One graph's estimate: what the Translate button would send (one capture at the configured Translation Depth) */
struct FN2CTokenEstimate
{
	/** ceil(characters of the condensed N2C JSON / 4) */
	int32 Tokens = 0;

	/** Nested graphs the capture took in (functions, macros, collapsed graphs within the depth) */
	int32 NestedGraphCount = 0;

	/** False when the graph could not be found or captured (unsupported graph type, only ghost nodes, deleted) */
	bool bValid = false;
};

/**
 * A cached estimate. It stays valid while its Blueprint's change counter and the Translation Depth it was taken at
 * are unchanged (and, when the capture took in graphs from other Blueprints, while no Blueprint was compiled since).
 */
struct FN2CTokenCacheEntry
{
	FN2CTokenEstimate Estimate;

	/** Size of serialized JSON in characters */
	int32 CachedJsonSize = 0;

	/** Blueprint path for invalidation tracking */
	FString BlueprintPath;

	/** Translation Depth the capture used */
	int32 Depth = 0;

	/** The Blueprint's change counter when captured */
	uint32 BlueprintRevision = 0;

	/** Set when nested graphs came from other Blueprints: the any-compile counter when captured */
	TOptional<uint32> ExternalRevision;

	/** When the cache was created (eviction order) */
	FDateTime CacheTimestamp;
};

/**
 * Centralized service for token estimation with caching and model info.
 * Provides notifications when model settings change or cache is invalidated.
 *
 * Key features:
 * - Caches one capture-based estimate per graph; a graph edit or compile of its Blueprint makes it stale
 * - Tracks current model info (context window, cost, etc.)
 * - Applies 65% rule: usable context = advertised context * 0.65
 * - Broadcasts notifications for UI updates
 */
class NODETOCODE_API FN2CTokenEstimationService
{
public:
	/**
	 * Get the singleton instance
	 */
	static FN2CTokenEstimationService& Get();

	/**
	 * Initialize the service (subscribe to delegates)
	 * Should be called during module startup
	 */
	void Initialize();

	/**
	 * Shutdown the service (unsubscribe from delegates)
	 * Should be called during module shutdown
	 */
	void Shutdown();

	// ==================== Token Estimation ====================

	/**
	 * The estimate for what the Translate button sends for this graph: one N2C::CaptureGraph at the configured
	 * Translation Depth, nested graphs included, as condensed N2C JSON, at 4 characters per token. Cached per
	 * {Blueprint, graph}; a cached estimate is reused until its Blueprint changes (a graph edit, a compile) or the
	 * depth changes. Game thread only.
	 */
	FN2CTokenEstimate GetEstimate(const FN2CTagInfo& GraphInfo);

	/** True when GetEstimate would answer from the cache without capturing */
	bool IsEstimateCurrent(const FN2CTagInfo& GraphInfo) const;

	/** GetEstimate's token count (0 when the graph cannot be captured) */
	int32 GetTokenEstimate(const FN2CTagInfo& GraphInfo);

	/** GetEstimate's token count and nested-graph count */
	int32 GetTokenEstimateWithNesting(const FN2CTagInfo& GraphInfo, int32& OutNestedGraphCount);

	/** How many captures the service has run (tests and the overlay's log) */
	int32 GetCaptureCount() const { return CaptureCount; }

	/**
	 * Get the current translation depth setting
	 * @return Translation depth (0 = no nesting, 1-5 = levels of nesting)
	 */
	int32 GetTranslationDepth() const;

	/**
	 * Check if nested translation is enabled
	 * @return true if TranslationDepth > 0
	 */
	bool IsNestedTranslationEnabled() const;

	/**
	 * Invalidate cache for a specific graph (manual refresh)
	 * @param GraphGuid The GUID of the graph to invalidate
	 */
	void InvalidateCache(const FString& GraphGuid);

	/**
	 * Invalidate all cached token estimates
	 */
	void InvalidateAllCache();

	// ==================== Model Info (65% rule applied) ====================

	/**
	 * Get the current model's display name
	 */
	FString GetCurrentModelName() const { return CachedModelName; }

	/**
	 * Get the usable context window size (ContextWindow * 0.65)
	 * The advertised context window is not fully usable for input
	 */
	int32 GetUsableContextWindow() const { return CachedUsableContextWindow; }

	/**
	 * Get the full advertised context window size
	 */
	int32 GetFullContextWindow() const { return CachedFullContextWindow; }

	/**
	 * Get the input cost per 1M tokens
	 */
	float GetInputCostPer1M() const { return CachedInputCostPer1M; }

	/**
	 * Check if the current provider is a local model (free)
	 */
	bool IsLocalProvider() const { return bCachedIsLocal; }

	/**
	 * Calculate the estimated cost for a token count
	 * @param TokenCount The number of tokens
	 * @return Estimated cost in USD
	 */
	float CalculateCost(int32 TokenCount) const;

	/**
	 * Force refresh of model information from settings
	 */
	void RefreshModelInfo();

	/** Rough token count for a text of this many characters (4 characters per token, as for graph estimates) */
	static int32 EstimateTokensForChars(int32 CharCount);

	/**
	 * Logs a warning when the estimated prompt is larger than the context window the request sets, since a local
	 * server such as Ollama cuts an over-long prompt without returning an error.
	 * @param EstimatedPromptTokens estimate of everything the request sends (system and user messages)
	 * @param ContextWindow the window the request sets; 0 or less means the server default, which is unknown here
	 * @param ProviderLabel provider name for the log line
	 * @return true when the warning was logged
	 */
	static bool WarnIfPromptExceedsContext(int32 EstimatedPromptTokens, int32 ContextWindow, const TCHAR* ProviderLabel);

	// ==================== Notifications ====================

	/** Delegate fired when model settings change (provider or model) */
	DECLARE_MULTICAST_DELEGATE(FOnModelChanged);
	FOnModelChanged OnModelChanged;

	/** Every estimate may be out of date (the cache was cleared, or the Translation Depth changed) */
	DECLARE_MULTICAST_DELEGATE(FOnCacheInvalidated);
	FOnCacheInvalidated OnCacheInvalidated;

	/**
	 * A Blueprint changed (a graph edit or a compile): estimates of its graphs, and any estimate IsEstimateCurrent
	 * now reports out of date, should be taken again. Listeners debounce; nothing is captured here.
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnEstimatesStale, const FSoftObjectPath& /*Blueprint*/);
	FOnEstimatesStale OnEstimatesStale;

	/** Marks a Blueprint changed, as a graph edit or compile would (tests; the editor hooks call it) */
	void NotifyBlueprintChanged(const UBlueprint* Blueprint);

private:
	FN2CTokenEstimationService();
	~FN2CTokenEstimationService();

	// Disable copy
	FN2CTokenEstimationService(const FN2CTokenEstimationService&) = delete;
	FN2CTokenEstimationService& operator=(const FN2CTokenEstimationService&) = delete;

	/**
	 * Handle settings property changes
	 */
	void OnSettingsPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	/** A Blueprint is about to compile: its estimates and those that took in other Blueprints' graphs go stale */
	void OnBlueprintPreCompile(UBlueprint* Blueprint);

	/**
	 * Captures the graph at Depth and serializes it the way the Translate button does
	 * @param OutNestedGraphCount nested graphs in the capture
	 * @param OutGraphGuids every captured graph's GUID (the root first)
	 * @return condensed N2C JSON, empty on failure
	 */
	FString SerializeGraphToJson(const FN2CTagInfo& GraphInfo, int32 Depth, UBlueprint*& OutBlueprint,
		TArray<FGuid>& OutGraphGuids);

	/** Follows a Blueprint's graph edits (every graph, re-scanned when the Blueprint changes) */
	void WatchBlueprint(UBlueprint* Blueprint);
	void UnwatchAll();

	/** The cache key: the normalised Blueprint path and the graph GUID */
	static FString MakeCacheKey(const FN2CTagInfo& GraphInfo);

	/** The change counter of a Blueprint path (0 until it first changes) */
	uint32 GetBlueprintRevision(const FString& BlueprintPath) const;

	/** Whether an entry still describes the graph */
	bool IsEntryCurrent(const FN2CTokenCacheEntry& Entry) const;

	/**
	 * Evict oldest cache entries if over limit
	 */
	void EvictOldestCacheEntries();

	/**
	 * Format a number with comma separators
	 */
	static FString FormatNumber(int64 Number);

	// Token cache: MakeCacheKey -> cache entry
	TMap<FString, FN2CTokenCacheEntry> TokenCache;

	// Change counter per Blueprint path (graph edits and compiles), and one bumped by every compile
	TMap<FString, uint32> BlueprintRevisions;
	uint32 AnyCompileRevision = 0;

	// Watched Blueprints and graphs
	TMap<TWeakObjectPtr<UBlueprint>, FDelegateHandle> WatchedBlueprints;
	TMap<TWeakObjectPtr<UEdGraph>, FDelegateHandle> WatchedGraphs;

	int32 CaptureCount = 0;

	// Maximum cache entries before LRU eviction
	static constexpr int32 MaxCacheEntries = 100;

	// Context window factor - advertised context is not fully usable and prone to hallucination past a certain amount
	static constexpr float ContextWindowFactor = 0.45f;

	// Cached model info
	FString CachedModelName;
	int32 CachedFullContextWindow = 0;
	int32 CachedUsableContextWindow = 0;
	float CachedInputCostPer1M = 0.0f;
	bool bCachedIsLocal = false;

	// Delegate handles
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle BlueprintPreCompileHandle;

	// Initialization state
	bool bIsInitialized = false;
};
