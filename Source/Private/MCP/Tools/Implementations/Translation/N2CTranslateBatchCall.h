// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include <atomic>

class FJsonObject;
class FJsonValue;

/**
 * One translate-batch call (card 0169): a batch of graphs translated through UN2CBatchTranslationOrchestrator with the
 * call's provider, model and language, answered in-band like translate-focused-blueprint (card 0107).
 *
 * Everything runs on the game thread. Start() starts the batch and returns the call's future at once; the batch's
 * completion (OnBatchCompleteNative for this call's batch serial) sets the result: every item's status, saved files,
 * code (inline while the whole result stays under N2CMcpBlueprintExport::MaxInlineTokens) and typed error. Each item
 * the batch starts and an FTSTicker heartbeat report progress through the call context. Cancelling the context, or the
 * timeout, cancels the batch (its in-flight HTTP request at once; the rest skipped) and sets one cancelled result.
 *
 * The call keeps itself alive until its result is set; the context, the ticker and the orchestrator's delegates hold
 * it weakly.
 */
class FN2CTranslateBatchCall : public TSharedFromThis<FN2CTranslateBatchCall, ESPMode::ThreadSafe>
{
public:
	/** What the tool resolved before the call starts */
	struct FSetup
	{
		/** One per graph, in order (BlueprintPath is the Blueprint's object path) */
		TArray<FN2CTagInfo> TagInfos;
		/** bUseRequestConfig with the provider, model and language; Graphs parallel to TagInfos */
		FN2CBatchTranslationOptions Options;
		/** The kind of each graph (EventGraph, Function, ...), parallel to TagInfos */
		TArray<FString> Kinds;
		/** Targets that resolved to nothing, and empty graphs left out (N2CMcpBatchTargets), passed through to the result */
		TArray<TSharedPtr<FJsonValue>> TargetFailures;
		TArray<TSharedPtr<FJsonValue>> Skipped;
		/** Provider, model and language IDs as the result reports them */
		FString ProviderId;
		FString ModelId;
		FString LanguageId;
		/** Added to the result (an unrecognised model ID) */
		TArray<FString> Warnings;
	};

	FN2CTranslateBatchCall(const FN2CToolCallContextRef& InContext, FSetup&& InSetup);

	/** Sets a result if none was set (a call dropped before it started), so the promise is never broken */
	~FN2CTranslateBatchCall();

	/** Game thread only, once. Starts the batch and returns the call's result; it is set later, on the game thread. */
	TFuture<FMcpToolCallResult> Start();

	/** Progress and timeout step, run by the call's FTSTicker. Returns true while the batch runs. Public for tests. */
	bool Tick(float DeltaTime);

	/** Overrides the 1-hour timeout (tests) */
	void SetTimeoutSeconds(double InSeconds) { TimeoutSeconds = InSeconds; }

	/** The orchestrator's serial of this call's batch; INDEX_NONE before it started */
	int32 GetBatchSerial() const { return Serial; }

	bool IsComplete() const { return bFinished.load(); }

	/** Default time a batch may take before the call cancels it */
	static constexpr double DefaultTimeoutSeconds = 3600.0;

	/** How often the ticker reports progress and checks the timeout */
	static constexpr float ProgressIntervalSeconds = 0.5f;

	/**
	 * The result JSON for a finished batch: the batch's figures, the items (status, outputDirectory, files, graphs with
	 * their code, usage, error) and the target failures. Code is left out (codeInline false) when the whole result
	 * would be over MaxInlineTokens. Public for tests.
	 */
	static TSharedRef<FJsonObject> BuildResultJson(const FSetup& Setup, const UN2CBatchTranslationOrchestrator& Orchestrator,
		const FN2CBatchTranslationResult& Result, int32 MaxInlineTokens);

private:
	void HandleProgress(int32 Index, int32 Total, const FString& GraphName);
	void HandleBatchComplete(const FN2CBatchTranslationResult& Result);

	/** Cancels this call's batch and sets Message as the call's error result, once */
	void AbortBatch(const FString& Message);

	/** True while this call's batch is the orchestrator's running batch */
	bool IsOurBatchRunning() const;

	/** Reports Progress (kept rising) with Message */
	void Report(float Progress, const FString& Message);

	/** Sets the call's result once, unsubscribes, stops the ticker and lets the call go */
	void Finish(const FMcpToolCallResult& Result);

	void Unsubscribe();
	void StopTicker();

	FN2CToolCallContextRef Context;
	FSetup Setup;

	TPromise<FMcpToolCallResult> Promise;
	std::atomic<bool> bFinished{false};
	bool bStarted = false;

	/** Set once the call is being cancelled (by the client or the timeout): the batch's completion then sets this text */
	bool bAborting = false;
	FString AbortMessage;

	TSharedPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> SelfWhileRunning;

	int32 Serial = INDEX_NONE;
	FDelegateHandle ProgressHandle;
	FDelegateHandle CompleteHandle;
	FTSTicker::FDelegateHandle TickerHandle;

	double TimeoutSeconds = DefaultTimeoutSeconds;
	double StartTime = 0.0;
	double ItemStartTime = 0.0;
	int32 CurrentIndex = 0;
	FString CurrentGraphName;
	float LastProgress = 0.0f;
};
