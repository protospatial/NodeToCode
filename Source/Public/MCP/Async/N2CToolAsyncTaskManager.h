// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Async/IN2CToolAsyncTask.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "HAL/CriticalSection.h"
#include "Async/Async.h"

/**
 * Task execution context containing all necessary information for async execution
 */
struct FN2CAsyncTaskContext
{
	/** Unique task ID */
	FGuid TaskId;

	/** Progress token from the connector */
	FString ProgressToken;

	/** MCP Session ID */
	FString SessionId;

	/** Original request ID from the connector */
	TSharedPtr<FJsonValue> OriginalRequestId;

	/** Tool name */
	FString ToolName;

	/** Tool arguments */
	TSharedPtr<FJsonObject> Arguments;

	/** The async task instance. It runs on the game thread; Task->IsComplete() says whether it has reported. */
	TSharedPtr<IN2CToolAsyncTask> Task;

	FN2CAsyncTaskContext()
	{
	}
};

/**
 * Manages asynchronous execution of MCP tools
 * Handles task lifecycle, progress reporting, and cancellation
 */
class NODETOCODE_API FN2CToolAsyncTaskManager
{
public:
	/**
	 * Gets the singleton instance
	 */
	static FN2CToolAsyncTaskManager& Get();

	/**
	 * Launches an async task for a long-running tool. The task starts on the game thread (at once when called there)
	 * and runs without a worker thread.
	 * @param ToolName The name of the tool to execute
	 * @param Arguments The tool arguments
	 * @param ProgressToken The progress token from the connector
	 * @param SessionId The MCP session ID
	 * @param OriginalRequestId The original request ID from the connector
	 * @return The unique task ID
	 */
	FGuid LaunchTask(const FGuid& InTaskId, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, 
		const FString& ProgressToken, const FString& SessionId, const TSharedPtr<FJsonValue>& OriginalRequestId);

	/**
	 * Launches an already created task: tracks it under Task->GetTaskId() and its progress token, binds its delegates
	 * and starts it on the game thread. LaunchTask uses this after creating the task for the tool; tests use it to run
	 * their own task types.
	 * @return The task ID
	 */
	FGuid LaunchTaskInstance(const TSharedRef<IN2CToolAsyncTask, ESPMode::ThreadSafe>& Task, const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments, const FString& SessionId, const TSharedPtr<FJsonValue>& OriginalRequestId);

	/**
	 * Attempts to cancel a running task
	 * @param TaskId The ID of the task to cancel
	 * @return true if cancellation was initiated, false if task not found or already completed
	 */
	bool CancelTask(const FGuid& TaskId);

	/**
	 * Attempts to cancel a task by progress token
	 * @param ProgressToken The progress token of the task to cancel
	 * @return true if cancellation was initiated, false if task not found or already completed
	 */
	bool CancelTaskByProgressToken(const FString& ProgressToken);

	/**
	 * Gets task context by task ID
	 * @param TaskId The task ID
	 * @return The task context, or nullptr if not found
	 */
	TSharedPtr<FN2CAsyncTaskContext> GetTaskContext(const FGuid& TaskId) const;

	/**
	 * Gets task context by progress token
	 * @param ProgressToken The progress token
	 * @return The task context, or nullptr if not found
	 */
	TSharedPtr<FN2CAsyncTaskContext> GetTaskContextByProgressToken(const FString& ProgressToken) const;

	/**
	 * Checks if a task is still running
	 * @param TaskId The task ID
	 * @return true if the task is running, false otherwise
	 */
	bool IsTaskRunning(const FGuid& TaskId) const;

	/**
	 * Gets all running task IDs
	 * @return Array of task IDs that are currently running
	 */
	TArray<FGuid> GetRunningTaskIds() const;

	/**
	 * Checks whether a task with this progress token finished recently (within CompletedTokenTtlSeconds). A finished
	 * task is no longer tracked, so this is how nodetocode/cancelTask tells "already completed" from "not found".
	 */
	bool WasProgressTokenCompleted(const FString& ProgressToken) const;

	/**
	 * Removes tracked tasks that have already reported, and drops SSE streams of finished tasks that no client read
	 * within the SSE TTL. OnTaskCompleted already removes each task as it reports, so this is a safety net.
	 */
	void CleanupCompletedTasks();

	/**
	 * Cancels all running tasks, then removes every task that has reported. Called on editor exit (OnPreExit, while
	 * UObjects still exist) and in ShutdownModule before the servers stop. On the game thread, tasks that cancel their
	 * work there (translate-focused-blueprint) report and are removed before this returns.
	 */
	void CancelAllTasks();

	/** Number of tracked (launched and not yet reported) tasks */
	int32 GetTrackedTaskCount() const;

	/** Number of tracked progress-token mappings */
	int32 GetTrackedProgressTokenCount() const;

	/** How long a finished task's progress token is remembered for WasProgressTokenCompleted */
	static constexpr double CompletedTokenTtlSeconds = 600.0;

	/** At most this many finished tokens are remembered; the oldest go first */
	static constexpr int32 MaxCompletedTokens = 1024;

private:
	// Private constructor for singleton
	FN2CToolAsyncTaskManager() = default;

	// Non-copyable
	FN2CToolAsyncTaskManager(const FN2CToolAsyncTaskManager&) = delete;
	FN2CToolAsyncTaskManager& operator=(const FN2CToolAsyncTaskManager&) = delete;

	/**
	 * Creates an async task instance for the given tool
	 * @param ToolName The name of the tool
	 * @param TaskId The task ID
	 * @param ProgressToken The progress token
	 * @param Arguments The tool arguments
	 * @return The created task instance, or nullptr if the tool doesn't support async
	 */
	TSharedPtr<IN2CToolAsyncTask> CreateAsyncTask(const FString& ToolName, const FGuid& TaskId, 
		const FString& ProgressToken, const TSharedPtr<FJsonObject>& Arguments);

	/**
	 * Handles task completion
	 * @param TaskId The ID of the completed task
	 * @param Result The task result
	 */
	void OnTaskCompleted(const FGuid& TaskId, const FMcpToolCallResult& Result);

	/**
	 * Handles task progress
	 * @param TaskId The ID of the task
	 * @param Progress The progress value
	 * @param Message The progress message
	 */
	void OnTaskProgress(const FGuid& TaskId, float Progress, const FString& Message);

	/** Map of task IDs to task contexts */
	TMap<FGuid, TSharedPtr<FN2CAsyncTaskContext>> RunningTasks;

	/** Map of progress tokens to task IDs for quick lookup */
	TMap<FString, FGuid> ProgressTokenToTaskId;

	/** Progress tokens of recently finished tasks, with FPlatformTime::Seconds() of completion */
	TMap<FString, double> CompletedProgressTokens;

	/** Drops expired and excess entries from CompletedProgressTokens. Caller holds TaskMapLock. */
	void PruneCompletedTokens(double Now);

	/** Thread safety for task map access */
	mutable FCriticalSection TaskMapLock;
};
