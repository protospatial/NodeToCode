// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Tools/Implementations/Translation/N2CTranslateBlueprintAsyncTask.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "Utils/N2CLogger.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FN2CToolAsyncTaskManager& FN2CToolAsyncTaskManager::Get()
{
	static FN2CToolAsyncTaskManager Instance;
	return Instance;
}

FGuid FN2CToolAsyncTaskManager::LaunchTask(const FGuid& InTaskId, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
	const FString& ProgressToken, const TSharedPtr<FJsonValue>& ClientProgressToken, const FString& SessionId,
	const TSharedPtr<FJsonValue>& OriginalRequestId, const FN2CAsyncTaskResponder& Responder)
{
	// Create the async task under the caller's task ID
	TSharedPtr<IN2CToolAsyncTask> AsyncTask = TaskFactoryForTesting
		? TaskFactoryForTesting(ToolName, InTaskId, ProgressToken, Arguments)
		: CreateAsyncTask(ToolName, InTaskId, ProgressToken, Arguments);
	if (!AsyncTask.IsValid())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to create async task for tool: %s"), *ToolName));
		return FGuid();
	}

	return LaunchTaskInstance(AsyncTask.ToSharedRef(), ToolName, Arguments, SessionId, OriginalRequestId, Responder, ClientProgressToken);
}

FGuid FN2CToolAsyncTaskManager::LaunchTaskInstance(const TSharedRef<IN2CToolAsyncTask, ESPMode::ThreadSafe>& AsyncTask,
	const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, const FString& SessionId, const TSharedPtr<FJsonValue>& OriginalRequestId,
	const FN2CAsyncTaskResponder& Responder, const TSharedPtr<FJsonValue>& ClientProgressToken)
{
	const FGuid TaskId = AsyncTask->GetTaskId();
	const FString ProgressToken = AsyncTask->GetProgressToken();

	// Create task context
	auto TaskContext = MakeShared<FN2CAsyncTaskContext>();
	TaskContext->TaskId = TaskId;
	TaskContext->ProgressToken = ProgressToken;
	TaskContext->SessionId = SessionId;
	TaskContext->OriginalRequestId = OriginalRequestId;
	TaskContext->ToolName = ToolName;
	TaskContext->Arguments = Arguments;
	TaskContext->Task = AsyncTask;
	TaskContext->ClientProgressToken = ClientProgressToken;
	TaskContext->Responder = Responder;

	// Set up progress callback with lambda to capture TaskId
	AsyncTask->SetProgressDelegate(FN2CAsyncTaskProgressDelegate::CreateLambda(
		[this, TaskId](float Progress, const FString& Message)
		{
			this->OnTaskProgress(TaskId, Progress, Message);
		}));

	// Set up completion callback with lambda to capture TaskId
	AsyncTask->SetCompleteDelegate(FN2CAsyncTaskCompleteDelegate::CreateLambda(
		[this, TaskId](const FMcpToolCallResult& Result)
		{
			this->OnTaskCompleted(TaskId, Result);
		}));

	// Store the task context before starting, so progress and a result reported during Execute() find it
	{
		FScopeLock Lock(&TaskMapLock);
		RunningTasks.Add(TaskId, TaskContext);
		ProgressTokenToTaskId.Add(ProgressToken, TaskId);
	}

	// Start the task on the game thread. Execute() only starts the work (an async LLM request) and returns, so there
	// is no worker thread: UObjects are created on the game thread and callbacks land there too.
	if (IsInGameThread())
	{
		AsyncTask->Execute();
	}
	else
	{
		::AsyncTask(ENamedThreads::GameThread, [AsyncTask]()
		{
			AsyncTask->Execute();
		});
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Launched async task %s for tool: %s (ProgressToken: %s)"), 
		*TaskId.ToString(), *ToolName, *ProgressToken), EN2CLogSeverity::Info);

	return TaskId;
}

bool FN2CToolAsyncTaskManager::CancelTask(const FGuid& TaskId)
{
	TSharedPtr<IN2CToolAsyncTask> Task;
	{
		FScopeLock Lock(&TaskMapLock);
		const TSharedPtr<FN2CAsyncTaskContext>* TaskContext = RunningTasks.Find(TaskId);
		if (!TaskContext || !TaskContext->IsValid())
		{
			return false;
		}
		Task = (*TaskContext)->Task;
	}

	// Cancel outside the lock: the task reports its cancelled result, which looks the context up again
	if (!Task.IsValid() || Task->IsComplete())
	{
		return false;
	}

	Task->RequestCancel();
	FN2CLogger::Get().Log(FString::Printf(TEXT("Cancellation requested for task %s"), *TaskId.ToString()), EN2CLogSeverity::Info);
	return true;
}

bool FN2CToolAsyncTaskManager::CancelTaskByProgressToken(const FString& ProgressToken)
{
	FGuid TaskId;
	{
		FScopeLock Lock(&TaskMapLock);
		const FGuid* FoundTaskId = ProgressTokenToTaskId.Find(ProgressToken);
		if (!FoundTaskId)
		{
			return false;
		}
		TaskId = *FoundTaskId;
	}

	const bool bCancelled = CancelTask(TaskId);
	if (bCancelled)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Cancellation requested for task %s via progress token %s"),
			*TaskId.ToString(), *ProgressToken), EN2CLogSeverity::Info);
	}
	return bCancelled;
}

TSharedPtr<FN2CAsyncTaskContext> FN2CToolAsyncTaskManager::GetTaskContext(const FGuid& TaskId) const
{
	FScopeLock Lock(&TaskMapLock);
	
	auto TaskContext = RunningTasks.Find(TaskId);
	if (TaskContext && TaskContext->IsValid())
	{
		return *TaskContext;
	}
	
	return nullptr;
}

TSharedPtr<FN2CAsyncTaskContext> FN2CToolAsyncTaskManager::GetTaskContextByProgressToken(const FString& ProgressToken) const
{
	FScopeLock Lock(&TaskMapLock);
	
	const FGuid* TaskId = ProgressTokenToTaskId.Find(ProgressToken);
	if (!TaskId)
	{
		return nullptr;
	}

	auto TaskContext = RunningTasks.Find(*TaskId);
	if (TaskContext && TaskContext->IsValid())
	{
		return *TaskContext;
	}
	
	return nullptr;
}

bool FN2CToolAsyncTaskManager::IsTaskRunning(const FGuid& TaskId) const
{
	FScopeLock Lock(&TaskMapLock);

	auto TaskContext = RunningTasks.Find(TaskId);
	if (!TaskContext || !TaskContext->IsValid())
	{
		return false;
	}

	// Running until the task has reported its result
	return (*TaskContext)->Task.IsValid() && !(*TaskContext)->Task->IsComplete();
}

TArray<FGuid> FN2CToolAsyncTaskManager::GetRunningTaskIds() const
{
	FScopeLock Lock(&TaskMapLock);

	TArray<FGuid> RunningIds;
	for (const auto& Pair : RunningTasks)
	{
		if (Pair.Value.IsValid() && Pair.Value->Task.IsValid() && !Pair.Value->Task->IsComplete())
		{
			RunningIds.Add(Pair.Key);
		}
	}

	return RunningIds;
}

bool FN2CToolAsyncTaskManager::WasProgressTokenCompleted(const FString& ProgressToken) const
{
	FScopeLock Lock(&TaskMapLock);
	const double* CompletedAt = CompletedProgressTokens.Find(ProgressToken);
	return CompletedAt && FPlatformTime::Seconds() - *CompletedAt < CompletedTokenTtlSeconds;
}

void FN2CToolAsyncTaskManager::PruneCompletedTokens(double Now)
{
	for (auto It = CompletedProgressTokens.CreateIterator(); It; ++It)
	{
		if (Now - It.Value() >= CompletedTokenTtlSeconds)
		{
			It.RemoveCurrent();
		}
	}

	while (CompletedProgressTokens.Num() > MaxCompletedTokens)
	{
		const FString* Oldest = nullptr;
		double OldestTime = TNumericLimits<double>::Max();
		for (const TPair<FString, double>& Pair : CompletedProgressTokens)
		{
			if (Pair.Value < OldestTime)
			{
				OldestTime = Pair.Value;
				Oldest = &Pair.Key;
			}
		}
		if (!Oldest)
		{
			break;
		}
		const FString OldestToken = *Oldest;
		CompletedProgressTokens.Remove(OldestToken);
	}
}

void FN2CToolAsyncTaskManager::CleanupCompletedTasks()
{
	int32 NumRemoved = 0;
	{
		FScopeLock Lock(&TaskMapLock);
		const double Now = FPlatformTime::Seconds();
		for (auto It = RunningTasks.CreateIterator(); It; ++It)
		{
			const TSharedPtr<FN2CAsyncTaskContext>& Context = It.Value();
			if (Context.IsValid() && Context->Task.IsValid() && !Context->Task->IsComplete())
			{
				continue;
			}
			if (Context.IsValid())
			{
				const FGuid* MappedTaskId = ProgressTokenToTaskId.Find(Context->ProgressToken);
				if (MappedTaskId && *MappedTaskId == It.Key())
				{
					ProgressTokenToTaskId.Remove(Context->ProgressToken);
				}
				CompletedProgressTokens.Add(Context->ProgressToken, Now);
			}
			It.RemoveCurrent();
			++NumRemoved;
		}
		PruneCompletedTokens(Now);
	}

	if (NumRemoved > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Cleaned up %d completed async tasks"), NumRemoved), EN2CLogSeverity::Debug);
	}
}

void FN2CToolAsyncTaskManager::CancelAllTasks()
{
	TArray<TSharedPtr<IN2CToolAsyncTask>> TasksToCancel;
	{
		FScopeLock Lock(&TaskMapLock);
		for (auto& Pair : RunningTasks)
		{
			if (Pair.Value.IsValid() && Pair.Value->Task.IsValid() && !Pair.Value->Task->IsComplete())
			{
				TasksToCancel.Add(Pair.Value->Task);
			}
		}
	}

	// Cancel outside the lock: each task reports its cancelled result through OnTaskCompleted, which removes it
	for (const TSharedPtr<IN2CToolAsyncTask>& Task : TasksToCancel)
	{
		Task->RequestCancel();
	}

	// Drop whatever has reported but is still tracked (for example a task whose report was queued)
	CleanupCompletedTasks();

	if (TasksToCancel.Num() > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Requested cancellation for all %d running tasks (%d still tracked)"),
			TasksToCancel.Num(), GetTrackedTaskCount()), EN2CLogSeverity::Info);
	}
}

int32 FN2CToolAsyncTaskManager::GetTrackedTaskCount() const
{
	FScopeLock Lock(&TaskMapLock);
	return RunningTasks.Num();
}

int32 FN2CToolAsyncTaskManager::GetTrackedProgressTokenCount() const
{
	FScopeLock Lock(&TaskMapLock);
	return ProgressTokenToTaskId.Num();
}

TSharedPtr<IN2CToolAsyncTask> FN2CToolAsyncTaskManager::CreateAsyncTask(const FString& ToolName, const FGuid& TaskId, 
	const FString& ProgressToken, const TSharedPtr<FJsonObject>& Arguments)
{
	
	if (ToolName == TEXT("translate-focused-blueprint"))
	{
		return MakeShared<FN2CTranslateBlueprintAsyncTask>(TaskId, ProgressToken, Arguments);
	}
	
	FN2CLogger::Get().LogWarning(FString::Printf(TEXT("No async task implementation found for tool: %s. TaskId: %s"), *ToolName, *TaskId.ToString()));
	return nullptr;
}

void FN2CToolAsyncTaskManager::OnTaskCompleted(const FGuid& TaskId, const FMcpToolCallResult& Result)
{
	TSharedPtr<FN2CAsyncTaskContext> TaskContext = GetTaskContext(TaskId);
	if (!TaskContext.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Task completed but context not found: %s"), *TaskId.ToString()));
		return;
	}

	// Write the final JSON-RPC response on the tools/call's own HTTP response
	FN2CLogger::Get().Log(FString::Printf(TEXT("Task %s completed, sending final response"), *TaskId.ToString()), EN2CLogSeverity::Info);

	// Create the JSON-RPC response
	FJsonRpcResponse Response;
	Response.Id = TaskContext->OriginalRequestId;
	// Convert the tool result to JSON Value
	Response.Result = MakeShared<FJsonValueObject>(Result.ToJson());

	// Serialize to JSON string
	FString ResponseJson = FJsonRpcUtils::SerializeResponse(Response);
	if (ResponseJson.IsEmpty())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to serialize response for task %s"), *TaskId.ToString()));
		ResponseJson = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(TaskContext->OriginalRequestId,
			JsonRpcErrorCodes::InternalError, TEXT("Failed to serialize the tool result")));
	}

	// Exactly one response per call: the HTTP response is completed (or its stream closed) by this write
	FN2CAsyncTaskResponder Responder = MoveTemp(TaskContext->Responder);
	TaskContext->Responder = FN2CAsyncTaskResponder();
	if (Responder.SendResponse)
	{
		Responder.SendResponse(ResponseJson);
	}

	// The result is delivered: stop tracking the task. The token is remembered so a late cancel answers
	// task_already_completed.
	// TaskContext (and the task's own ReportComplete) keep the task alive until this call returns.
	{
		FScopeLock Lock(&TaskMapLock);
		RunningTasks.Remove(TaskId);
		const FGuid* MappedTaskId = ProgressTokenToTaskId.Find(TaskContext->ProgressToken);
		if (MappedTaskId && *MappedTaskId == TaskId)
		{
			ProgressTokenToTaskId.Remove(TaskContext->ProgressToken);
		}
		const double Now = FPlatformTime::Seconds();
		PruneCompletedTokens(Now);
		CompletedProgressTokens.Add(TaskContext->ProgressToken, Now);
	}
}

void FN2CToolAsyncTaskManager::OnTaskProgress(const FGuid& TaskId, float Progress, const FString& Message)
{
	TSharedPtr<FN2CAsyncTaskContext> TaskContext = GetTaskContext(TaskId);
	if (!TaskContext.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Task progress reported but context not found: %s"), *TaskId.ToString()));
		return;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Task %s progress: %.1f%% - %s"),
		*TaskId.ToString(), Progress * 100.0f, *Message), EN2CLogSeverity::Debug);

	// Progress goes only to a client that asked for it (sent a progressToken) on a response that can carry it
	if (!TaskContext->ClientProgressToken.IsValid() || !TaskContext->Responder.SendProgress)
	{
		return;
	}

	// MCP: the progress value must increase with every notification
	const double Value = FMath::Max(static_cast<double>(Progress), TaskContext->LastProgressSent + 1e-6);
	TaskContext->LastProgressSent = Value;

	// Create MCP progress notification. The token is echoed exactly as the client sent it (a number stays a number).
	FJsonRpcNotification ProgressNotification;
	ProgressNotification.Method = TEXT("notifications/progress");

	auto ParamsObject = MakeShared<FJsonObject>();
	ParamsObject->SetField(TEXT("progressToken"), TaskContext->ClientProgressToken);
	ParamsObject->SetNumberField(TEXT("progress"), Value);
	if (!Message.IsEmpty())
	{
		ParamsObject->SetStringField(TEXT("message"), Message);
	}
	
	ProgressNotification.Params = MakeShared<FJsonValueObject>(ParamsObject);

	// Serialize the notification to JSON
	FString NotificationJson = FJsonRpcUtils::SerializeNotification(ProgressNotification);
	if (!NotificationJson.IsEmpty())
	{
		TaskContext->Responder.SendProgress(NotificationJson);
	}
	else
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to serialize progress notification for task %s"), *TaskId.ToString()));
	}
}
