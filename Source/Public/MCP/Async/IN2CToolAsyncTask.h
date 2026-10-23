// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolTypes.h"

/**
 * Delegate for progress reporting from async tasks
 * @param Progress Progress value (0.0 to 1.0, or specific values for discrete progress)
 * @param Message Optional progress message
 */
DECLARE_DELEGATE_TwoParams(FN2CAsyncTaskProgressDelegate, float /* Progress */, const FString& /* Message */);

/**
 * Delegate for task completion
 * @param Result The tool call result
 */
DECLARE_DELEGATE_OneParam(FN2CAsyncTaskCompleteDelegate, const FMcpToolCallResult& /* Result */);

/**
 * Interface for asynchronous MCP tool tasks
 * Provides a standard way to execute long-running tools with progress reporting and cancellation support
 */
class NODETOCODE_API IN2CToolAsyncTask
{
public:
	virtual ~IN2CToolAsyncTask() = default;

	/**
	 * Starts the task. Called once, on the game thread. It must not block: start the work (for example an async HTTP
	 * request) and return, then report progress and completion from callbacks or a ticker on the game thread.
	 */
	virtual void Execute() = 0;

	/**
	 * Requests cancellation of the task. Tasks with in-flight work cancel it and report a cancelled result once.
	 */
	virtual void RequestCancel() = 0;

	/**
	 * Checks if the task supports cancellation
	 * @return true if the task can be cancelled, false otherwise
	 */
	virtual bool IsCancellable() const = 0;

	/**
	 * Checks if cancellation has been requested
	 * @return true if cancellation was requested, false otherwise
	 */
	virtual bool IsCancellationRequested() const = 0;

	/**
	 * Checks if the task has reported its result (success, error or cancelled)
	 * @return true once ReportComplete has run
	 */
	virtual bool IsComplete() const = 0;

	/**
	 * Gets the unique task identifier
	 * @return The task's unique ID
	 */
	virtual FGuid GetTaskId() const = 0;

	/**
	 * Gets the progress token for this task
	 * @return The progress token (connectorProgressToken from the MCP request)
	 */
	virtual FString GetProgressToken() const = 0;

	/**
	 * Sets the progress reporting delegate
	 * @param InDelegate The delegate to call when reporting progress
	 */
	virtual void SetProgressDelegate(const FN2CAsyncTaskProgressDelegate& InDelegate) = 0;

	/**
	 * Sets the completion delegate
	 * @param InDelegate The delegate to call when the task completes
	 */
	virtual void SetCompleteDelegate(const FN2CAsyncTaskCompleteDelegate& InDelegate) = 0;

protected:
	/**
	 * Reports progress from the task
	 * @param Progress Progress value (0.0 to 1.0)
	 * @param Message Optional progress message
	 */
	virtual void ReportProgress(float Progress, const FString& Message = TEXT("")) = 0;

	/**
	 * Reports task completion
	 * @param Result The result of the task execution
	 */
	virtual void ReportComplete(const FMcpToolCallResult& Result) = 0;
};