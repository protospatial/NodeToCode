// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CTranslation.h"
#include "Core/N2CTagTypes.h"
#include "N2CBatchTranslationTypes.generated.h"

/**
 * @enum EN2CBatchItemStatus
 * @brief Status of an individual item in a batch translation
 */
UENUM(BlueprintType)
enum class EN2CBatchItemStatus : uint8
{
	Pending		UMETA(DisplayName = "Pending"),
	Processing	UMETA(DisplayName = "Processing"),
	Completed	UMETA(DisplayName = "Completed"),
	Failed		UMETA(DisplayName = "Failed"),
	Skipped		UMETA(DisplayName = "Skipped")
};

/**
 * @struct FN2CBatchTranslationItem
 * @brief Internal tracking struct for a single item in a batch translation
 */
USTRUCT()
struct NODETOCODE_API FN2CBatchTranslationItem
{
	GENERATED_BODY()

	/** The tag info identifying this graph */
	UPROPERTY()
	FN2CTagInfo TagInfo;

	/** Cached reference to the loaded Blueprint */
	UPROPERTY()
	TWeakObjectPtr<UBlueprint> CachedBlueprint;

	/** Cached reference to the graph within the Blueprint */
	TWeakObjectPtr<UEdGraph> CachedGraph;

	/** Current processing status */
	UPROPERTY()
	EN2CBatchItemStatus Status = EN2CBatchItemStatus::Pending;

	/** Error message if processing failed */
	UPROPERTY()
	FString ErrorMessage;

	/** The translation response once completed */
	UPROPERTY()
	FN2CTranslationResponse TranslationResponse;

	/** Cached JSON payload from serialization (used for saving alongside translation) */
	FString CachedJsonPayload;

	/**
	 * The folder under the batch output folder this item's files are saved in. Set when the batch starts: the graph
	 * name's file stem, or <Blueprint>__<Graph> (then a number) when another item of the batch already has that stem,
	 * so two Blueprints' EventGraphs do not overwrite each other. Empty: the graph name's stem.
	 */
	FString OutputFolderName;

	FN2CBatchTranslationItem() = default;
};

/**
 * @struct FN2CBatchTranslationResult
 * @brief Summary result of a batch translation operation
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CBatchTranslationResult
{
	GENERATED_BODY()

	/** Total number of items in the batch */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 TotalCount = 0;

	/** Number of successfully translated items */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 SuccessCount = 0;

	/** Number of failed items */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 FailureCount = 0;

	/** Number of skipped items (e.g., due to cancellation) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 SkippedCount = 0;

	/** Path to the batch output directory */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	FString BatchOutputPath;

	/** Names of graphs that failed to translate */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	TArray<FString> FailedGraphNames;

	/** Total time taken for the batch in seconds */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	float TotalTimeSeconds = 0.0f;

	/** Total input tokens used across all translations, cached input included */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 TotalInputTokens = 0;

	/** Total output tokens used across all translations, thinking included */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 TotalOutputTokens = 0;

	/** Part of TotalInputTokens read from the provider's prompt cache */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 TotalCacheReadInputTokens = 0;

	/** Part of TotalInputTokens written to the provider's prompt cache */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 TotalCacheWriteInputTokens = 0;

	/** Part of TotalOutputTokens spent thinking, where the provider reports it */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch")
	int32 TotalReasoningTokens = 0;

	FN2CBatchTranslationResult() = default;
};

/**
 * Per-item completion delegate with batch context (Blueprint-compatible)
 * @param TagInfo The tag info for the completed item
 * @param Response The translation response
 * @param bSuccess Whether the translation was successful
 * @param ItemIndex The zero-based index of this item in the batch
 * @param TotalCount The total number of items in the batch
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnBatchItemTranslationComplete,
	const FN2CTagInfo&, TagInfo,
	const FN2CTranslationResponse&, Response,
	bool, bSuccess,
	int32, ItemIndex,
	int32, TotalCount
);

/**
 * Batch completion delegate (Blueprint-compatible)
 * @param Result The final batch result with statistics
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBatchTranslationComplete,
	const FN2CBatchTranslationResult&, Result
);

/**
 * Progress delegate for UI updates (Blueprint-compatible)
 * @param CurrentIndex The current item index being processed
 * @param TotalCount The total number of items
 * @param CurrentGraphName The name of the graph currently being processed
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnBatchTranslationProgress,
	int32, CurrentIndex,
	int32, TotalCount,
	const FString&, CurrentGraphName
);

// ==================== Native C++ Delegate Versions ====================
// These are for C++ code (like Slate widgets) that cannot use UFUNCTIONs

/**
 * Native per-item completion delegate (C++ only)
 */
DECLARE_MULTICAST_DELEGATE_FiveParams(
	FOnBatchItemTranslationCompleteNative,
	const FN2CTagInfo& /*TagInfo*/,
	const FN2CTranslationResponse& /*Response*/,
	bool /*bSuccess*/,
	int32 /*ItemIndex*/,
	int32 /*TotalCount*/
);

/**
 * Native batch completion delegate (C++ only)
 */
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnBatchTranslationCompleteNative,
	const FN2CBatchTranslationResult& /*Result*/
);

/**
 * Native progress delegate (C++ only)
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnBatchTranslationProgressNative,
	int32 /*CurrentIndex*/,
	int32 /*TotalCount*/,
	const FString& /*CurrentGraphName*/
);

/**
 * @struct FN2CBatchJsonExportResult
 * @brief Summary result of a batch JSON export operation (no LLM translation)
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CBatchJsonExportResult
{
	GENERATED_BODY()

	/** Total number of items in the export */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	int32 TotalCount = 0;

	/** Number of successfully exported items */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	int32 SuccessCount = 0;

	/** Number of failed items */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	int32 FailureCount = 0;

	/** Path to the batch output directory */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	FString OutputPath;

	/** Path to the combined markdown file */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	FString CombinedMarkdownPath;

	/** Names of graphs that failed to export */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	TArray<FString> FailedGraphNames;

	/** Total time taken for the export in seconds */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Batch Export")
	float TotalTimeSeconds = 0.0f;

	FN2CBatchJsonExportResult() = default;
};

/**
 * Batch JSON export completion delegate
 * @param Result The final export result with paths and statistics
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBatchJsonExportComplete,
	const FN2CBatchJsonExportResult&, Result
);
