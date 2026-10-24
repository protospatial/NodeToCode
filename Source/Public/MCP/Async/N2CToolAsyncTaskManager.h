// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "HAL/CriticalSection.h"
#include <atomic>

/**
 * Where a call's MCP messages go: the HTTP response of the tools/call that started it. The MCP server binds these to
 * that response (a text/event-stream on the same POST, or one application/json response held until the end); tests
 * bind their own. Both are called on the game thread.
 */
struct FN2CAsyncTaskResponder
{
	/** Writes one serialized notifications/progress message. Unbound when the client asked for no progress. */
	TFunction<void(const FString& /*NotificationJson*/)> SendProgress;

	/** Writes the serialized JSON-RPC response that carries the call's CallToolResult. Called once per call. */
	TFunction<void(const FString& /*ResponseJson*/)> SendResponse;
};

/**
 * The manager's record of one running long-running tool call: who asked (session, request id, progress token), where
 * its messages go, and the call context the tool was given.
 */
struct FN2CToolCallRecord
{
	/** Unique ID of this call (the "task" in log lines and nodetocode/cancelTask) */
	FGuid TaskId;

	/** Progress token as a lookup key (nodetocode/cancelTask): the client's token as a string, or a generated one */
	FString ProgressToken;

	/** The client's _meta.progressToken exactly as sent (string or number), echoed in progress notifications. Null
	 *  when the client sent none: then no progress notifications are written. */
	TSharedPtr<FJsonValue> ClientProgressToken;

	/** Where progress and the final response are written */
	FN2CAsyncTaskResponder Responder;

	/** Last progress value written; each notification's value is larger, as MCP requires */
	double LastProgressSent = -1.0;

	/** MCP Session ID */
	FString SessionId;

	/** The tools/call's JSON-RPC id (what notifications/cancelled names) */
	TSharedPtr<FJsonValue> OriginalRequestId;

	/** Tool name */
	FString ToolName;

	/** Tool arguments */
	TSharedPtr<FJsonObject> Arguments;

	/** The tool's progress channel and cancellation signal */
	FN2CToolCallContextPtr CallContext;

	/** Set once the call's result has been delivered */
	std::atomic<bool> bCompleted{false};
};

/**
 * Runs long-running MCP tool calls. A call is the tool's IN2CMcpTool::InvokeAsync with an FN2CToolCallContext: the
 * manager binds the context's progress to the call's responder, cancels it on request (nodetocode/cancelTask, editor
 * exit) and writes the one JSON-RPC response when the tool's future is set. It knows nothing about any one tool.
 */
class NODETOCODE_API FN2CToolAsyncTaskManager
{
public:
	/**
	 * Gets the singleton instance
	 */
	static FN2CToolAsyncTaskManager& Get();

	/**
	 * Starts a long-running tool call: finds the registered tool and invokes it on the game thread (at once when
	 * called there). A call that finishes inside InvokeAsync (no Blueprint focused, a bad argument) has already
	 * written its response through Responder when this returns.
	 * @param InTaskId The ID to track the call under
	 * @param ToolName The name of the tool to invoke
	 * @param Arguments The tool arguments
	 * @param ProgressToken The progress token as a lookup key (the client's token as a string, or a generated one)
	 * @param ClientProgressToken The client's _meta.progressToken as sent, or null when it sent none
	 * @param SessionId The MCP session ID
	 * @param OriginalRequestId The tools/call's JSON-RPC id
	 * @param Responder Where progress notifications and the final response are written
	 * @return The task ID, or an invalid GUID when no such tool is registered (nothing was written)
	 */
	FGuid LaunchTask(const FGuid& InTaskId, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
		const FString& ProgressToken, const TSharedPtr<FJsonValue>& ClientProgressToken, const FString& SessionId,
		const TSharedPtr<FJsonValue>& OriginalRequestId, const FN2CAsyncTaskResponder& Responder);

	/**
	 * Starts a call of the given tool instance: tracks it under InTaskId and its progress token, makes its call
	 * context and invokes it on the game thread. LaunchTask uses this after finding the tool; tests use it to run
	 * their own tools.
	 * @return The task ID
	 */
	FGuid LaunchToolCall(const TSharedRef<IN2CMcpTool>& Tool, const FGuid& InTaskId, const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments, const FString& ProgressToken, const FString& SessionId,
		const TSharedPtr<FJsonValue>& OriginalRequestId, const FN2CAsyncTaskResponder& Responder = FN2CAsyncTaskResponder(),
		const TSharedPtr<FJsonValue>& ClientProgressToken = nullptr);

	/**
	 * Cancels a running call through its context
	 * @param TaskId The ID of the call to cancel
	 * @param Reason The cancelled result's text (empty: "Task was cancelled")
	 * @return true if cancellation was initiated, false if the call is unknown, finished or already cancelled
	 */
	bool CancelTask(const FGuid& TaskId, const FString& Reason = FString());

	/**
	 * Cancels a running call by progress token
	 * @param ProgressToken The progress token of the call to cancel
	 * @return true if cancellation was initiated, false if the call is unknown, finished or already cancelled
	 */
	bool CancelTaskByProgressToken(const FString& ProgressToken);

	/**
	 * Finds a running call by its session and tools/call request id (string and number ids are distinct, as in
	 * JSON-RPC). This is how notifications/cancelled names a call.
	 * @return The task ID, or an invalid GUID when no running call matches
	 */
	FGuid FindTaskByRequestId(const FString& SessionId, const TSharedPtr<FJsonValue>& RequestId) const;

	/**
	 * Gets a call's record by task ID
	 * @return The record, or nullptr if not found
	 */
	TSharedPtr<FN2CToolCallRecord> GetTaskContext(const FGuid& TaskId) const;

	/**
	 * Gets a call's record by progress token
	 * @return The record, or nullptr if not found
	 */
	TSharedPtr<FN2CToolCallRecord> GetTaskContextByProgressToken(const FString& ProgressToken) const;

	/**
	 * Checks if a call is still running (its result not yet delivered)
	 */
	bool IsTaskRunning(const FGuid& TaskId) const;

	/**
	 * Gets all running task IDs
	 */
	TArray<FGuid> GetRunningTaskIds() const;

	/**
	 * Checks whether a call with this progress token finished recently (within CompletedTokenTtlSeconds). A finished
	 * call is no longer tracked, so this is how nodetocode/cancelTask tells "already completed" from "not found".
	 */
	bool WasProgressTokenCompleted(const FString& ProgressToken) const;

	/**
	 * Removes tracked calls whose result was delivered. OnTaskCompleted already removes each call as it finishes, so
	 * this is a safety net.
	 */
	void CleanupCompletedTasks();

	/**
	 * Cancels all running calls, then removes every finished one. Called on editor exit (OnPreExit, while UObjects
	 * still exist) and in ShutdownModule before the servers stop. On the game thread, tools that cancel their work
	 * there (translate-focused-blueprint) finish and are removed before this returns.
	 */
	void CancelAllTasks();

	/** Number of tracked (launched and not yet finished) calls */
	int32 GetTrackedTaskCount() const;

	/** Number of tracked progress-token mappings */
	int32 GetTrackedProgressTokenCount() const;

	/** Finds the tool LaunchTask invokes for a tool name, or null */
	using FToolResolver = TFunction<TSharedPtr<IN2CMcpTool>(const FString& /*ToolName*/)>;

	/** Tests only: LaunchTask resolves tools with InResolver instead of the tool manager; an unset function restores it */
	void SetToolResolverForTesting(FToolResolver InResolver) { ToolResolverForTesting = MoveTemp(InResolver); }

	/** How long a finished call's progress token is remembered for WasProgressTokenCompleted */
	static constexpr double CompletedTokenTtlSeconds = 600.0;

	/** At most this many finished tokens are remembered; the oldest go first */
	static constexpr int32 MaxCompletedTokens = 1024;

private:
	// Private constructor for singleton
	FN2CToolAsyncTaskManager() = default;

	// Non-copyable
	FN2CToolAsyncTaskManager(const FN2CToolAsyncTaskManager&) = delete;
	FN2CToolAsyncTaskManager& operator=(const FN2CToolAsyncTaskManager&) = delete;

	/** Invokes the tool for a tracked call and waits (without blocking) for its future. Game thread. */
	void InvokeTool(const TSharedRef<IN2CMcpTool>& Tool, const TSharedRef<FN2CToolCallRecord>& Record);

	/** Delivers a call's result: writes the JSON-RPC response once and stops tracking the call. Game thread. */
	void OnTaskCompleted(const FGuid& TaskId, const FMcpToolCallResult& Result);

	/** Writes a notifications/progress for the call when its client asked for progress. Game thread. */
	void OnTaskProgress(const FGuid& TaskId, float Progress, const FString& Message);

	/** Map of task IDs to call records */
	TMap<FGuid, TSharedPtr<FN2CToolCallRecord>> RunningTasks;

	/** Map of progress tokens to task IDs for quick lookup */
	TMap<FString, FGuid> ProgressTokenToTaskId;

	/** Progress tokens of recently finished calls, with FPlatformTime::Seconds() of completion */
	TMap<FString, double> CompletedProgressTokens;

	/** Drops expired and excess entries from CompletedProgressTokens. Caller holds TaskMapLock. */
	void PruneCompletedTokens(double Now);

	/** Thread safety for task map access */
	mutable FCriticalSection TaskMapLock;

	/** Set by tests to replace the tool manager lookup */
	FToolResolver ToolResolverForTesting;
};
