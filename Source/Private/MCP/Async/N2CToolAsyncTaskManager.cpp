// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "Utils/N2CLogger.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace N2CToolAsyncTaskManagerPrivate
{
	/** JSON-RPC ids match when they have the same type and value (the string "1" is not the number 1) */
	static bool SameRequestId(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		if (!A.IsValid() || !B.IsValid() || A->Type != B->Type)
		{
			return false;
		}
		switch (A->Type)
		{
			case EJson::String:
				return A->AsString() == B->AsString();
			case EJson::Number:
				return A->AsNumber() == B->AsNumber();
			default:
				return false;
		}
	}
}

FN2CToolAsyncTaskManager& FN2CToolAsyncTaskManager::Get()
{
	static FN2CToolAsyncTaskManager Instance;
	return Instance;
}

FGuid FN2CToolAsyncTaskManager::LaunchTask(const FGuid& InTaskId, const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments,
	const FString& ProgressToken, const TSharedPtr<FJsonValue>& ClientProgressToken, const FString& SessionId,
	const TSharedPtr<FJsonValue>& OriginalRequestId, const FN2CAsyncTaskResponder& Responder)
{
	const TSharedPtr<IN2CMcpTool> Tool = ToolResolverForTesting
		? ToolResolverForTesting(ToolName)
		: FN2CMcpToolManager::Get().FindTool(ToolName);
	if (!Tool.IsValid())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("No registered tool to launch: %s"), *ToolName));
		return FGuid();
	}

	return LaunchToolCall(Tool.ToSharedRef(), InTaskId, ToolName, Arguments, ProgressToken, SessionId, OriginalRequestId,
		Responder, ClientProgressToken);
}

FGuid FN2CToolAsyncTaskManager::LaunchToolCall(const TSharedRef<IN2CMcpTool>& Tool, const FGuid& InTaskId, const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments, const FString& ProgressToken, const FString& SessionId,
	const TSharedPtr<FJsonValue>& OriginalRequestId, const FN2CAsyncTaskResponder& Responder, const TSharedPtr<FJsonValue>& ClientProgressToken)
{
	const FGuid TaskId = InTaskId.IsValid() ? InTaskId : FGuid::NewGuid();
	const FString TokenKey = ProgressToken.IsEmpty() ? TaskId.ToString(EGuidFormats::DigitsWithHyphensLower) : ProgressToken;

	const TSharedRef<FN2CToolCallRecord> Record = MakeShared<FN2CToolCallRecord>();
	Record->TaskId = TaskId;
	Record->ProgressToken = TokenKey;
	Record->SessionId = SessionId;
	Record->OriginalRequestId = OriginalRequestId;
	Record->ToolName = ToolName;
	Record->Arguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
	Record->ClientProgressToken = ClientProgressToken;
	Record->Responder = Responder;
	Record->CallContext = FN2CToolCallContext::Create([this, TaskId](float Progress, const FString& Message)
	{
		OnTaskProgress(TaskId, Progress, Message);
	});

	// Track the call before starting it, so progress and a result reported during InvokeAsync find it
	{
		FScopeLock Lock(&TaskMapLock);
		RunningTasks.Add(TaskId, Record);
		ProgressTokenToTaskId.Add(TokenKey, TaskId);
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Launched async task %s for tool: %s (ProgressToken: %s)"),
		*TaskId.ToString(), *ToolName, *TokenKey), EN2CLogSeverity::Info);

	// The tool starts on the game thread. InvokeAsync only starts the work (an async LLM request) and returns, so
	// there is no worker thread: UObjects are created on the game thread and callbacks land there too.
	if (IsInGameThread())
	{
		InvokeTool(Tool, Record);
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [this, Tool, Record]()
		{
			InvokeTool(Tool, Record);
		});
	}

	return TaskId;
}

void FN2CToolAsyncTaskManager::InvokeTool(const TSharedRef<IN2CMcpTool>& Tool, const TSharedRef<FN2CToolCallRecord>& Record)
{
	check(IsInGameThread());
	const FGuid TaskId = Record->TaskId;

	if (Record->CallContext->IsCancelled())
	{
		// Cancelled while queued for the game thread
		OnTaskCompleted(TaskId, FMcpToolCallResult::CreateErrorResult(Record->CallContext->GetCancelledMessage()));
		return;
	}

	// The call runs through the catalog's pipeline (the Play In Editor rule, card 0170, and the required arguments),
	// with the definition cached at registration when there is one. Whatever this manager is given runs as a
	// long-running call (InvokeAsync), so a spec's own tool instance runs the same way.
	const TSharedPtr<const FMcpToolDefinition> Cached = FN2CMcpToolManager::Get().GetToolDefinition(Record->ToolName);
	FN2CToolDescriptor Descriptor = FN2CToolDescriptor::FromDefinition(Cached.IsValid()
		? Cached : MakeShared<const FMcpToolDefinition>(Tool->GetDefinition()));
	Descriptor.ExecutionPolicy = EN2CToolExecutionPolicy::LongRunning;

	TFuture<FN2CToolOutcome> Future = FN2CToolCatalog::InvokeTool(Tool, Descriptor, Record->Arguments, Record->CallContext.ToSharedRef());

	// The continuation runs where the tool sets its result (at once when it already has): deliver on the game thread,
	// as MCP content (the outcome's MCP adapter)
	Future.Next([this, TaskId](FN2CToolOutcome Outcome)
	{
		FMcpToolCallResult Result = Outcome.ToCallToolResult();
		if (IsInGameThread())
		{
			OnTaskCompleted(TaskId, Result);
			return;
		}
		AsyncTask(ENamedThreads::GameThread, [this, TaskId, Result = MoveTemp(Result)]()
		{
			OnTaskCompleted(TaskId, Result);
		});
	});
}

bool FN2CToolAsyncTaskManager::CancelTask(const FGuid& TaskId, const FString& Reason)
{
	TSharedPtr<FN2CToolCallRecord> Record = GetTaskContext(TaskId);
	if (!Record.IsValid() || Record->bCompleted.load() || !Record->CallContext.IsValid())
	{
		return false;
	}

	// Cancel outside the lock: the tool reports its cancelled result, which looks the record up again
	const bool bCancelled = Record->CallContext->Cancel(Reason.IsEmpty() ? FString(FN2CToolCallContext::DefaultCancelledMessage()) : Reason);
	if (bCancelled)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Cancellation requested for async task %s (Tool: %s)"), *TaskId.ToString(), *Record->ToolName),
			EN2CLogSeverity::Info);
	}
	return bCancelled;
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

FGuid FN2CToolAsyncTaskManager::FindTaskByRequestId(const FString& SessionId, const TSharedPtr<FJsonValue>& RequestId) const
{
	FScopeLock Lock(&TaskMapLock);
	for (const TPair<FGuid, TSharedPtr<FN2CToolCallRecord>>& Pair : RunningTasks)
	{
		const TSharedPtr<FN2CToolCallRecord>& Record = Pair.Value;
		if (Record.IsValid() && !Record->bCompleted.load() && Record->SessionId == SessionId
			&& N2CToolAsyncTaskManagerPrivate::SameRequestId(Record->OriginalRequestId, RequestId))
		{
			return Pair.Key;
		}
	}
	return FGuid();
}

bool FN2CToolAsyncTaskManager::CancelTaskByRequestId(const FString& SessionId, const TSharedPtr<FJsonValue>& RequestId, const FString& Reason)
{
	const FGuid TaskId = FindTaskByRequestId(SessionId, RequestId);
	const TSharedPtr<FN2CToolCallRecord> Record = TaskId.IsValid() ? GetTaskContext(TaskId) : nullptr;
	if (!Record.IsValid() || Record->bCompleted.load())
	{
		return false;
	}

	// Mark first: a tool that stops at once reports its cancelled result inside Cancel, and that result is not sent
	Record->bResponseSuppressed.store(true);
	CancelTask(TaskId, Reason);
	FN2CLogger::Get().Log(FString::Printf(TEXT("notifications/cancelled: cancelling task %s (Tool: %s); no response will be sent for its request"),
		*TaskId.ToString(), *Record->ToolName), EN2CLogSeverity::Info);
	return true;
}

TSharedPtr<FN2CToolCallRecord> FN2CToolAsyncTaskManager::GetTaskContext(const FGuid& TaskId) const
{
	FScopeLock Lock(&TaskMapLock);
	const TSharedPtr<FN2CToolCallRecord>* Record = RunningTasks.Find(TaskId);
	return Record ? *Record : nullptr;
}

TSharedPtr<FN2CToolCallRecord> FN2CToolAsyncTaskManager::GetTaskContextByProgressToken(const FString& ProgressToken) const
{
	FScopeLock Lock(&TaskMapLock);
	const FGuid* TaskId = ProgressTokenToTaskId.Find(ProgressToken);
	if (!TaskId)
	{
		return nullptr;
	}
	const TSharedPtr<FN2CToolCallRecord>* Record = RunningTasks.Find(*TaskId);
	return Record ? *Record : nullptr;
}

bool FN2CToolAsyncTaskManager::IsTaskRunning(const FGuid& TaskId) const
{
	FScopeLock Lock(&TaskMapLock);
	const TSharedPtr<FN2CToolCallRecord>* Record = RunningTasks.Find(TaskId);
	return Record && Record->IsValid() && !(*Record)->bCompleted.load();
}

TArray<FGuid> FN2CToolAsyncTaskManager::GetRunningTaskIds() const
{
	FScopeLock Lock(&TaskMapLock);
	TArray<FGuid> RunningIds;
	for (const TPair<FGuid, TSharedPtr<FN2CToolCallRecord>>& Pair : RunningTasks)
	{
		if (Pair.Value.IsValid() && !Pair.Value->bCompleted.load())
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
			const TSharedPtr<FN2CToolCallRecord>& Record = It.Value();
			if (Record.IsValid() && !Record->bCompleted.load())
			{
				continue;
			}
			if (Record.IsValid())
			{
				const FGuid* MappedTaskId = ProgressTokenToTaskId.Find(Record->ProgressToken);
				if (MappedTaskId && *MappedTaskId == It.Key())
				{
					ProgressTokenToTaskId.Remove(Record->ProgressToken);
				}
				CompletedProgressTokens.Add(Record->ProgressToken, Now);
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
	TArray<FGuid> ToCancel = GetRunningTaskIds();

	// Cancel outside the lock: each tool reports its cancelled result through OnTaskCompleted, which removes it
	for (const FGuid& TaskId : ToCancel)
	{
		CancelTask(TaskId);
	}

	// Drop whatever has finished but is still tracked
	CleanupCompletedTasks();

	if (ToCancel.Num() > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Requested cancellation for all %d running tasks (%d still tracked)"),
			ToCancel.Num(), GetTrackedTaskCount()), EN2CLogSeverity::Info);
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

void FN2CToolAsyncTaskManager::OnTaskCompleted(const FGuid& TaskId, const FMcpToolCallResult& Result)
{
	check(IsInGameThread());

	TSharedPtr<FN2CToolCallRecord> Record = GetTaskContext(TaskId);
	if (!Record.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Task completed but context not found: %s"), *TaskId.ToString()));
		return;
	}

	// Exactly one result per call
	if (Record->bCompleted.exchange(true))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Async task %s attempted to complete multiple times"), *TaskId.ToString()));
		return;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Async task %s completed (Tool: %s, IsError: %s)"),
		*TaskId.ToString(), *Record->ToolName, Result.bIsError ? TEXT("Yes") : TEXT("No")), EN2CLogSeverity::Info);

	// No progress after the result, and the tool's cancel callbacks are released
	if (Record->CallContext.IsValid())
	{
		Record->CallContext->MarkCompleted();
	}

	// Exactly one end per call: the HTTP response is completed (or its stream closed) by this write
	FN2CAsyncTaskResponder Responder = MoveTemp(Record->Responder);
	Record->Responder = FN2CAsyncTaskResponder();

	if (Record->bResponseSuppressed.load())
	{
		// The client cancelled the request with notifications/cancelled: end the exchange with no response.
		// Tests/mcp_probe/live_translate_cancel.py parses this line.
		FN2CLogger::Get().Log(FString::Printf(TEXT("Async task %s: request cancelled by the client; no response sent"), *TaskId.ToString()),
			EN2CLogSeverity::Info);
		if (Responder.EndWithoutResponse)
		{
			Responder.EndWithoutResponse();
		}
	}
	else
	{
		SendTaskResponse(Responder, Record->OriginalRequestId, TaskId, Result);
	}

	// The call is finished: stop tracking it. The token is remembered so a late cancel answers
	// task_already_completed.
	{
		FScopeLock Lock(&TaskMapLock);
		RunningTasks.Remove(TaskId);
		const FGuid* MappedTaskId = ProgressTokenToTaskId.Find(Record->ProgressToken);
		if (MappedTaskId && *MappedTaskId == TaskId)
		{
			ProgressTokenToTaskId.Remove(Record->ProgressToken);
		}
		const double Now = FPlatformTime::Seconds();
		PruneCompletedTokens(Now);
		CompletedProgressTokens.Add(Record->ProgressToken, Now);
	}
}

void FN2CToolAsyncTaskManager::SendTaskResponse(const FN2CAsyncTaskResponder& Responder, const TSharedPtr<FJsonValue>& RequestId,
	const FGuid& TaskId, const FMcpToolCallResult& Result)
{
	// Create the JSON-RPC response
	FJsonRpcResponse Response;
	Response.Id = RequestId;
	Response.Result = MakeShared<FJsonValueObject>(Result.ToJson());

	FString ResponseJson = FJsonRpcUtils::SerializeResponse(Response);
	if (ResponseJson.IsEmpty())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to serialize response for task %s"), *TaskId.ToString()));
		ResponseJson = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(RequestId,
			JsonRpcErrorCodes::InternalError, TEXT("Failed to serialize the tool result")));
	}

	if (Responder.SendResponse)
	{
		Responder.SendResponse(ResponseJson);
	}
}

void FN2CToolAsyncTaskManager::OnTaskProgress(const FGuid& TaskId, float Progress, const FString& Message)
{
	TSharedPtr<FN2CToolCallRecord> Record = GetTaskContext(TaskId);
	if (!Record.IsValid() || Record->bCompleted.load())
	{
		return;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Task %s progress: %.1f%% - %s"),
		*TaskId.ToString(), Progress * 100.0f, *Message), EN2CLogSeverity::Debug);

	// Progress goes only to a client that asked for it (sent a progressToken) on a response that can carry it
	if (!Record->ClientProgressToken.IsValid() || !Record->Responder.SendProgress)
	{
		return;
	}

	// MCP: the progress value must increase with every notification
	const double Value = FMath::Max(static_cast<double>(Progress), Record->LastProgressSent + 1e-6);
	Record->LastProgressSent = Value;

	// The token is echoed exactly as the client sent it (a number stays a number)
	FJsonRpcNotification ProgressNotification;
	ProgressNotification.Method = TEXT("notifications/progress");

	const TSharedRef<FJsonObject> ParamsObject = MakeShared<FJsonObject>();
	ParamsObject->SetField(TEXT("progressToken"), Record->ClientProgressToken);
	ParamsObject->SetNumberField(TEXT("progress"), Value);
	if (!Message.IsEmpty())
	{
		ParamsObject->SetStringField(TEXT("message"), Message);
	}
	ProgressNotification.Params = MakeShared<FJsonValueObject>(ParamsObject);

	const FString NotificationJson = FJsonRpcUtils::SerializeNotification(ProgressNotification);
	if (!NotificationJson.IsEmpty())
	{
		Record->Responder.SendProgress(NotificationJson);
	}
	else
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to serialize progress notification for task %s"), *TaskId.ToString()));
	}
}
