// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "N2CPythonTranslation.generated.h"

struct FMcpToolCallResult;

/** The state of one batch started through UN2CPythonTranslation (card 0176) */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CPythonBatchStatus
{
	GENERATED_BODY()

	/** The batch's id (StartBatch's return value) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Python")
	FString BatchId;

	/** running, succeeded (at least one graph translated), failed or cancelled */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Python")
	FString State;

	/** 0 to 1; rises while the batch runs and is 1 once it succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Python")
	float Progress = 0.0f;

	/** The latest progress message, for example "Translating BP_Door.Open (2 of 5, 12 s)" */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Python")
	FString Message;

	/**
	 * Once the batch ended: the translate-batch MCP tool's result text. For a batch that succeeded, its result JSON
	 * marked untrusted (provider, model, language, items with status, files and code); for a failed one, its error
	 * JSON (error, code, failures, or items) or the error message; for a cancelled one, the cancel message.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Python")
	FString Result;

	/** Seconds since the batch started (to its end once it ended) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Python")
	float ElapsedSeconds = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FN2COnPythonBatchFinished, const FString&, BatchId);

/**
 * Batch translation for Python (the NodeToCodeTools toolset, card 0176): starts the translate-batch MCP tool's
 * batch (UN2CBatchTranslationOrchestrator with the call's provider, model and language), keeps each batch's
 * progress and result under an id, and cancels it through the call's context (the HTTP request in flight is aborted
 * at once, the rest skipped). Game thread only; OnBatchFinished is broadcast there when a batch ends.
 *
 * Python: unreal.N2CPythonTranslation.start_batch(...), get_batch_status(id), cancel_batch(id), and
 * unreal.N2CPythonTranslation.get_instance().on_batch_finished.add_callable(fn).
 */
UCLASS(BlueprintType)
class NODETOCODE_API UN2CPythonTranslation : public UObject
{
	GENERATED_BODY()

public:
	/** The one instance (rooted), which carries OnBatchFinished */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NodeToCode|Python")
	static UN2CPythonTranslation* GetInstance();

	/**
	 * Starts a batch translation. Give the graphs either as BlueprintPaths with GraphNames parallel to them (an empty
	 * or missing name translates every graph of that Blueprint that has nodes) or by Tag and/or Category.
	 * @param Language, Provider, Model IDs as the translate-batch tool takes them; empty uses the settings
	 * @param OutError Why nothing started (INVALID_ARGUMENT, NO_VALID_ITEMS, BATCH_IN_PROGRESS, LLM_BUSY, ...): the
	 *        tool's error text. Empty when the batch started.
	 * @return The batch's id, or empty when it did not start
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString StartBatch(const TArray<FString>& BlueprintPaths, const TArray<FString>& GraphNames,
		const FString& Tag, const FString& Category, const FString& Language, const FString& Provider,
		const FString& Model, FString& OutError);

	/**
	 * The status of a batch. An empty BatchId means the running batch, or else the one started last.
	 * @return The status; its BatchId is empty when there is no such batch (the last MaxKeptBatches are kept)
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FN2CPythonBatchStatus GetBatchStatus(const FString& BatchId);

	/**
	 * Cancels a running batch; it ends (OnBatchFinished, state cancelled) before this returns, unless an item's
	 * response is being handled at that moment, when it ends right after that item. A batch that already ended is
	 * left as it is. An empty BatchId means the running batch.
	 * @return False when there is no such batch (or, for an empty id, none is running)
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static bool CancelBatch(const FString& BatchId);

	/**
	 * Broadcast on the game thread when a batch ends (succeeded, failed or cancelled). Its listeners are kept alive
	 * (AddReferencedObjects): a Python add_callable binds an otherwise unreferenced object, which garbage collection
	 * would otherwise remove, silently dropping the listener.
	 */
	UPROPERTY(BlueprintAssignable, Category = "NodeToCode|Python")
	FN2COnPythonBatchFinished OnBatchFinished;

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	/** Batches whose status is kept, finished or not (the oldest finished ones are dropped first) */
	static constexpr int32 MaxKeptBatches = 32;

	/** The cancel reason CancelBatch gives the call: the cancelled batch's Result */
	static const TCHAR* CancelMessage() { return TEXT("Cancelled by cancel_batch"); }

private:
	struct FBatch
	{
		FN2CToolCallContextPtr Context;
		FN2CPythonBatchStatus Status;
		double StartSeconds = 0.0;
		bool bFinished = false;
	};

	/** Records the batch's end and broadcasts OnBatchFinished */
	void Finish(const TSharedRef<FBatch>& Batch, const FMcpToolCallResult& Result);

	/** The batch BatchId names (empty: the running one, else the latest), or null */
	TSharedPtr<FBatch> Find(const FString& BatchId, bool bRunningOnly) const;

	/** Drops the oldest finished batches beyond MaxKeptBatches */
	void Prune();

	TMap<FString, TSharedRef<FBatch>> Batches;

	/** Batch ids, oldest first */
	TArray<FString> Order;
};
