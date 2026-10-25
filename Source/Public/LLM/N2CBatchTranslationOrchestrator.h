// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "N2CBatchTranslationOrchestrator.generated.h"

struct FN2CBlueprint;

class UBlueprint;
class UEdGraph;

/**
 * @class UN2CBatchTranslationOrchestrator
 * @brief Manages batch translation of multiple Blueprint graphs from FN2CTagInfo arrays
 *
 * This class orchestrates sequential translation of multiple graphs, managing:
 * - Blueprint loading and caching
 * - Graph resolution by GUID
 * - Sequential LLM translation requests
 * - Progress tracking and cancellation
 * - Batch output file generation
 */
UCLASS(BlueprintType)
class NODETOCODE_API UN2CBatchTranslationOrchestrator : public UObject
{
	GENERATED_BODY()

public:
	/** Get the singleton instance */
	static UN2CBatchTranslationOrchestrator& Get();

	/**
	 * Start a batch translation operation
	 * @param TagInfos Array of tagged graph information to translate
	 * @return True if batch started successfully, false if already in progress or invalid input
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Batch Translation")
	bool StartBatchTranslation(const TArray<FN2CTagInfo>& TagInfos);

	/**
	 * Cancel the current batch translation. The in-flight item's LLM request is cancelled at once (its HTTP request
	 * is aborted and nothing is saved for it); that item and the remaining ones are marked Skipped and the batch
	 * completes (OnBatchComplete) before this returns. If the in-flight item's response is already being handled,
	 * that item finishes and the batch completes right after it.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Batch Translation")
	void CancelBatch();

	/**
	 * Check if a batch translation is currently in progress
	 * @return True if batch is running
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Batch Translation")
	bool IsBatchInProgress() const { return bBatchInProgress; }

	/**
	 * Get the current batch progress as a percentage (0.0 - 1.0)
	 * @return Progress percentage
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Batch Translation")
	float GetBatchProgress() const;

	/**
	 * Get the current item index being processed
	 * @return Current item index, or -1 if not processing
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Batch Translation")
	int32 GetCurrentItemIndex() const { return CurrentItemIndex; }

	/**
	 * Get the total number of items in the current batch
	 * @return Total item count
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Batch Translation")
	int32 GetTotalItemCount() const { return BatchItems.Num(); }

	/**
	 * Get the current batch result (may be partial if in progress)
	 * @return Current batch result
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Batch Translation")
	const FN2CBatchTranslationResult& GetCurrentResult() const { return CurrentResult; }

	/** Delegate fired when each item completes translation (Blueprint-compatible) */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Batch Translation")
	FOnBatchItemTranslationComplete OnItemComplete;

	/** Delegate fired when the entire batch completes (Blueprint-compatible) */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Batch Translation")
	FOnBatchTranslationComplete OnBatchComplete;

	/** Delegate fired for progress updates (Blueprint-compatible) */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Batch Translation")
	FOnBatchTranslationProgress OnProgress;

	/** Native delegate for item completion (C++ only - use for Slate widgets) */
	FOnBatchItemTranslationCompleteNative OnItemCompleteNative;

	/** Native delegate for batch completion (C++ only - use for Slate widgets) */
	FOnBatchTranslationCompleteNative OnBatchCompleteNative;

	/** Native delegate for progress updates (C++ only - use for Slate widgets) */
	FOnBatchTranslationProgressNative OnProgressNative;

	// ==================== Batch JSON Export (No LLM) ====================

	/**
	 * Export multiple graphs to JSON files without LLM translation
	 * Creates individual JSON files and a combined markdown file for easy LLM chat upload
	 * @param TagInfos Array of tagged graph information to export
	 * @param Result Output result with paths and statistics
	 * @param bMinifyJson If true, outputs minified JSON; if false, outputs pretty-printed JSON
	 * @return True if export completed (check Result for individual failures)
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Batch Export")
	bool BatchExportJson(const TArray<FN2CTagInfo>& TagInfos, FN2CBatchJsonExportResult& Result, bool bMinifyJson = false);

	/** Delegate fired when batch JSON export completes */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Batch Export")
	FOnBatchJsonExportComplete OnJsonExportComplete;

private:
	/** Process the next pending item in the batch */
	void ProcessNextItem();

	/**
	 * Handles one item's own parsed result (the per-request OnParsed of UN2CLLMModule::ProcessN2CJson). Serial and
	 * ItemIndex identify the batch and item the request was sent for; a result for any other is ignored.
	 */
	void HandleItemResult(int32 Serial, int32 ItemIndex, const FN2CTranslationResponse& Response, bool bSuccess);

	/** Finalize the batch and broadcast completion */
	void FinalizeBatch();

	/** Clean up resources after batch completes */
	void CleanupBatch();

	/**
	 * Resolve all blueprints and graphs from TagInfos
	 * @return True if at least one item was successfully resolved
	 */
	bool ResolveBlueprintsAndGraphs();

	/**
	 * Load a blueprint from a soft object path
	 * @param BlueprintPath Path to the blueprint asset
	 * @return Loaded blueprint or nullptr
	 */
	UBlueprint* LoadBlueprintFromPath(const FString& BlueprintPath);

	/**
	 * Find a graph within a blueprint by its GUID
	 * @param Blueprint The blueprint to search
	 * @param GraphGuid The GUID of the graph to find
	 * @return The graph if found, nullptr otherwise
	 */
	UEdGraph* FindGraphByGuid(UBlueprint* Blueprint, const FGuid& GraphGuid);

	/**
	 * Collect nodes from a graph and serialize to JSON
	 * @param Graph The graph to process
	 * @param Blueprint The owning blueprint
	 * @param OutCapture The captured Blueprint the JSON was written from (the translation is saved with it)
	 * @return JSON string, empty on failure
	 */
	FString CollectAndSerializeGraph(UEdGraph* Graph, UBlueprint* Blueprint, FN2CBlueprint& OutCapture);

	/** Create the batch output directory */
	void CreateBatchOutputDirectory();

	/**
	 * Save translation files for a completed item
	 * @param Item The completed item to save
	 */
	void SaveItemTranslation(const FN2CBatchTranslationItem& Item);

	/** Generate the batch summary JSON file */
	void GenerateBatchSummary();

	/** Get the base path for translations */
	FString GetTranslationBasePath() const;

	/** Ensure a directory exists */
	bool EnsureDirectoryExists(const FString& DirectoryPath) const;

	/**
	 * Generate combined markdown file from all successfully exported JSON files
	 * @param Items The batch items that were processed
	 * @param OutputPath The directory containing the JSON files
	 */
	void GenerateCombinedMarkdown(const TArray<FN2CBatchTranslationItem>& Items, const FString& OutputPath);

	/** Items in the current batch */
	UPROPERTY()
	TArray<FN2CBatchTranslationItem> BatchItems;

	/** Cache of loaded blueprints by path */
	TMap<FString, TWeakObjectPtr<UBlueprint>> BlueprintCache;

	/** Current item index being processed */
	int32 CurrentItemIndex = -1;

	/** Whether a batch is currently in progress */
	bool bBatchInProgress = false;

	/** Whether cancellation has been requested */
	bool bCancellationRequested = false;

	/** Path to the batch output directory */
	FString BatchOutputPath;

	/** Time when the batch started */
	double BatchStartTime = 0.0;

	/** Current accumulated result */
	UPROPERTY()
	FN2CBatchTranslationResult CurrentResult;

	/** The in-flight item's LLM request (CancelBatch cancels it); reset when the item completes */
	FN2CLLMRequestHandlePtr CurrentRequest;

	/** Increases with every batch, so a late result from an earlier batch is never applied to this one */
	int32 BatchSerial = 0;
};
