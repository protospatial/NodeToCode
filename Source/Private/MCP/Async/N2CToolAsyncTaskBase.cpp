// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Async/N2CToolAsyncTaskBase.h"
#include "Utils/N2CLogger.h"
#include "Async/Async.h"

FN2CToolAsyncTaskBase::FN2CToolAsyncTaskBase(const FGuid& InTaskId, const FString& InProgressToken, const FString& InToolName, const TSharedPtr<FJsonObject>& InArguments)
	: Arguments(InArguments)
	, TaskId(InTaskId)
	, ProgressToken(InProgressToken)
	, ToolName(InToolName)
	, bCancellationRequested(false)
	, bHasCompleted(false)
{
}

void FN2CToolAsyncTaskBase::RequestCancel()
{
	bCancellationRequested = true;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Cancellation requested for async task %s (Tool: %s)"), *TaskId.ToString(), *ToolName), EN2CLogSeverity::Info);
}

bool FN2CToolAsyncTaskBase::IsCancellationRequested() const
{
	return bCancellationRequested;
}

void FN2CToolAsyncTaskBase::SetProgressDelegate(const FN2CAsyncTaskProgressDelegate& InDelegate)
{
	OnProgress = InDelegate;
}

void FN2CToolAsyncTaskBase::SetCompleteDelegate(const FN2CAsyncTaskCompleteDelegate& InDelegate)
{
	OnComplete = InDelegate;
}

void FN2CToolAsyncTaskBase::ReportProgress(float Progress, const FString& Message)
{
	if (!OnProgress.IsBound())
	{
		return;
	}

	if (IsInGameThread())
	{
		OnProgress.Execute(Progress, Message);
		return;
	}

	// Queued for the game thread: hold the task weakly, since the manager may release it before the queue runs. A task
	// that is not owned by a shared pointer cannot be held at all, so its off-thread progress is dropped.
	if (!DoesSharedInstanceExist())
	{
		return;
	}
	const TWeakPtr<FN2CToolAsyncTaskBase, ESPMode::ThreadSafe> WeakTask = AsWeak();
	AsyncTask(ENamedThreads::GameThread, [WeakTask, Progress, Message]()
	{
		if (const TSharedPtr<FN2CToolAsyncTaskBase, ESPMode::ThreadSafe> Task = WeakTask.Pin())
		{
			Task->OnProgress.ExecuteIfBound(Progress, Message);
		}
	});
}

void FN2CToolAsyncTaskBase::ReportComplete(const FMcpToolCallResult& Result)
{
	// Ensure we only complete once
	if (bHasCompleted.AtomicSet(true))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Async task %s attempted to complete multiple times"), *TaskId.ToString()));
		return;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Async task %s completed (Tool: %s, IsError: %s)"),
		*TaskId.ToString(), *ToolName, Result.bIsError ? TEXT("Yes") : TEXT("No")), EN2CLogSeverity::Info);

	if (!OnComplete.IsBound())
	{
		return;
	}

	if (IsInGameThread())
	{
		// The completion delegate lets the task manager drop its reference to this task. Keep the task (and the
		// delegate being executed) alive until the delegate returns.
		const TSharedPtr<FN2CToolAsyncTaskBase, ESPMode::ThreadSafe> KeepAlive =
			DoesSharedInstanceExist() ? AsShared().ToSharedPtr() : nullptr;
		OnComplete.Execute(Result);
		return;
	}

	// Queued for the game thread: hold the task weakly (see ReportProgress). A task released before the queue runs
	// has no one left to report to.
	if (!DoesSharedInstanceExist())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Async task %s reported its result off the game thread without a shared owner; dropping it"),
			*TaskId.ToString()));
		return;
	}
	const TWeakPtr<FN2CToolAsyncTaskBase, ESPMode::ThreadSafe> WeakTask = AsWeak();
	AsyncTask(ENamedThreads::GameThread, [WeakTask, Result]()
	{
		if (const TSharedPtr<FN2CToolAsyncTaskBase, ESPMode::ThreadSafe> Task = WeakTask.Pin())
		{
			Task->OnComplete.ExecuteIfBound(Result);
		}
	});
}

bool FN2CToolAsyncTaskBase::CheckCancellationAndReport()
{
	if (IsCancellationRequested())
	{
		FMcpToolCallResult CancelledResult = FMcpToolCallResult::CreateErrorResult(TEXT("Task was cancelled"));
		ReportComplete(CancelledResult);
		return true;
	}
	return false;
}