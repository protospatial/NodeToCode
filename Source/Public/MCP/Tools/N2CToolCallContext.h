// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include <atomic>

class FN2CToolCallContext;

/** Shared, thread-safe reference to a call context. Tools and transports both hold one. */
using FN2CToolCallContextRef = TSharedRef<FN2CToolCallContext, ESPMode::ThreadSafe>;
using FN2CToolCallContextPtr = TSharedPtr<FN2CToolCallContext, ESPMode::ThreadSafe>;

/**
 * Where a call's progress goes. The caller (the MCP transport, or a test) supplies it; it is called on the game thread
 * with the tool's raw progress and message. Turning those into notifications/progress (and keeping the value rising)
 * is the caller's business.
 */
using FN2CToolProgressSink = TFunction<void(float /*Progress*/, const FString& /*Message*/)>;

/**
 * One tools/call as the tool sees it: a progress channel and a cancellation signal. Every tool is invoked with one
 * (IN2CMcpTool::InvokeAsync); a quick tool ignores it, a long-running tool reports progress and stops its work when
 * cancelled.
 *
 * Tool side: ReportProgress(Progress, Message) from any thread, IsCancelled(), and OnCancelled(Callback) to stop
 * in-flight work (for example cancel the provider's HTTP request) and complete the call with a cancelled result.
 *
 * Caller side: Cancel(Reason) from any thread (nodetocode/cancelTask today, notifications/cancelled next), and
 * MarkCompleted() once the call's result has been delivered, after which progress is dropped and cancel callbacks are
 * released.
 *
 * Progress and cancel callbacks are delivered on the game thread.
 */
class NODETOCODE_API FN2CToolCallContext : public TSharedFromThis<FN2CToolCallContext, ESPMode::ThreadSafe>
{
public:
	/** A context whose progress goes to ProgressSink (none: progress is dropped) */
	static FN2CToolCallContextRef Create(FN2CToolProgressSink ProgressSink = nullptr);

	/** A context for a call that nobody can cancel or watch (a tool invoked without a transport) */
	static FN2CToolCallContextRef MakeDetached() { return Create(); }

	// --- Tool side ---

	/** Reports progress (0..1) with an optional message. Any thread; delivered on the game thread; dropped once completed. */
	void ReportProgress(float Progress, const FString& Message = FString());

	/** True once Cancel() has been called */
	bool IsCancelled() const { return bCancelled.load(); }

	/** Why the call was cancelled (empty until it is) */
	FString GetCancelReason() const;

	/**
	 * Registers Callback to run once, on the game thread, when the call is cancelled. When the call is already
	 * cancelled it runs at once (on the game thread) or is queued there. Callbacks are released, unrun, when the call
	 * completes. Hold the tool's own state weakly in Callback: the context may outlive it.
	 */
	void OnCancelled(TFunction<void()> Callback);

	/** The text of a cancelled call's result: the cancel reason, or "Task was cancelled" */
	FString GetCancelledMessage() const;

	// --- Caller side ---

	/**
	 * Requests cancellation. Any thread. Returns true the first time (the call was neither cancelled nor completed);
	 * the OnCancelled callbacks then run on the game thread (before this returns when called there).
	 */
	bool Cancel(const FString& Reason = FString());

	/** Marks the call's result as delivered: later progress is dropped and pending cancel callbacks are released */
	void MarkCompleted();

	/** True once MarkCompleted() has run */
	bool IsCompleted() const { return bCompleted.load(); }

	/** Default text of a cancelled result */
	static const TCHAR* DefaultCancelledMessage() { return TEXT("Task was cancelled"); }

private:
	explicit FN2CToolCallContext(FN2CToolProgressSink InProgressSink);

	/** Runs (and releases) the registered cancel callbacks. Game thread. */
	void RunCancelCallbacks();

	FN2CToolProgressSink ProgressSink;

	std::atomic<bool> bCancelled{false};
	std::atomic<bool> bCompleted{false};

	mutable FCriticalSection Lock;
	FString CancelReason;
	TArray<TFunction<void()>> CancelCallbacks;
};
