// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Async/IN2CToolAsyncTask.h"
#include "HAL/ThreadSafeBool.h"

/**
 * Base implementation for asynchronous MCP tool tasks
 * Provides common functionality for progress reporting, cancellation, and completion handling
 */
class NODETOCODE_API FN2CToolAsyncTaskBase : public IN2CToolAsyncTask
{
public:
	/**
	 * Constructor
	 * @param InTaskId Unique identifier for this task
	 * @param InProgressToken Progress token from the MCP request
	 * @param InToolName Name of the tool being executed
	 * @param InArguments Tool arguments
	 */
	FN2CToolAsyncTaskBase(const FGuid& InTaskId, const FString& InProgressToken, const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments);

	virtual ~FN2CToolAsyncTaskBase() = default;

	// IN2CToolAsyncTask interface
	virtual void RequestCancel() override;
	virtual bool IsCancellable() const override { return true; }
	virtual bool IsCancellationRequested() const override;
	virtual bool IsComplete() const override { return bHasCompleted; }
	virtual FGuid GetTaskId() const override { return TaskId; }
	virtual FString GetProgressToken() const override { return ProgressToken; }
	virtual void SetProgressDelegate(const FN2CAsyncTaskProgressDelegate& InDelegate) override;
	virtual void SetCompleteDelegate(const FN2CAsyncTaskCompleteDelegate& InDelegate) override;

protected:
	// IN2CToolAsyncTask interface
	virtual void ReportProgress(float Progress, const FString& Message = TEXT("")) override;
	virtual void ReportComplete(const FMcpToolCallResult& Result) override;

	/**
	 * Gets the tool name
	 * @return The name of the tool being executed
	 */
	FString GetToolName() const { return ToolName; }

	/**
	 * Gets the tool arguments
	 * @return The arguments passed to the tool
	 */
	TSharedPtr<FJsonObject> GetArguments() const { return Arguments; }

	/**
	 * Utility method to check for cancellation and report an error if cancelled
	 * @return true if cancelled (and error was reported), false if not cancelled
	 */
	bool CheckCancellationAndReport();
	
	/** Tool arguments */
	TSharedPtr<FJsonObject> Arguments;

private:
	/** Unique task identifier */
	FGuid TaskId;

	/** Progress token for MCP notifications */
	FString ProgressToken;

	/** Name of the tool being executed */
	FString ToolName;

	/** Thread-safe cancellation flag */
	FThreadSafeBool bCancellationRequested;

	/** Progress reporting delegate */
	FN2CAsyncTaskProgressDelegate OnProgress;

	/** Completion delegate */
	FN2CAsyncTaskCompleteDelegate OnComplete;

	/** Thread-safe flag to ensure completion is only reported once */
	FThreadSafeBool bHasCompleted;
};