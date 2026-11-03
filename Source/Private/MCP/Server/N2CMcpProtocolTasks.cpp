// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

// MCP 2025-11-25 tasks for task-augmented tools/call (card 0211): basic/utilities/tasks. A session's tools/call of a
// tool with TaskSupport, carrying params.task, answers a CreateTaskResult at once and runs as a long-running call;
// tasks/get, tasks/list, tasks/result and tasks/cancel act on the session's tasks.

#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpTasksPrivate
{
	const TCHAR* const RelatedTaskKey = TEXT("io.modelcontextprotocol/related-task");

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString TaskIdOf(const TSharedPtr<FJsonValue>& Params)
	{
		FString TaskId;
		if (Params.IsValid() && Params->Type == EJson::Object)
		{
			Params->AsObject()->TryGetStringField(TEXT("taskId"), TaskId);
		}
		return TaskId;
	}
}

TSharedRef<FJsonObject> FN2CMcpProtocol::FTask::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("taskId"), TaskId);
	Json->SetStringField(TEXT("status"), Status);
	if (!StatusMessage.IsEmpty())
	{
		Json->SetStringField(TEXT("statusMessage"), StatusMessage);
	}
	Json->SetStringField(TEXT("createdAt"), CreatedAt.ToIso8601());
	Json->SetStringField(TEXT("lastUpdatedAt"), LastUpdatedAt.ToIso8601());
	Json->SetNumberField(TEXT("ttl"), static_cast<double>(TtlMs));
	if (!IsTerminal())
	{
		Json->SetNumberField(TEXT("pollInterval"), static_cast<double>(TaskPollIntervalMs));
	}
	return Json;
}

FString FN2CMcpProtocol::FTask::ResultResponse(const TSharedPtr<FJsonValue>& Id) const
{
	using namespace N2CMcpTasksPrivate;
	if (Status == TEXT("cancelled") || !Result.IsValid())
	{
		return FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InternalError,
			FString::Printf(TEXT("Task %s was cancelled; it has no result"), *TaskId)));
	}
	// The call's CallToolResult, with the task it came from in _meta (basic/utilities/tasks, related-task)
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->Values = Result->Values;
	const TSharedPtr<FJsonObject>* ExistingMeta = nullptr;
	const TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
	if (Result->TryGetObjectField(TEXT("_meta"), ExistingMeta) && ExistingMeta && ExistingMeta->IsValid())
	{
		Meta->Values = (*ExistingMeta)->Values;
	}
	const TSharedRef<FJsonObject> Related = MakeShared<FJsonObject>();
	Related->SetStringField(TEXT("taskId"), TaskId);
	Meta->SetObjectField(RelatedTaskKey, Related);
	Out->SetObjectField(TEXT("_meta"), Meta);
	return FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateSuccessResponse(Id, MakeShared<FJsonValueObject>(Out)));
}

void FN2CMcpProtocol::FTask::AnswerWaiters()
{
	TArray<FWaiter> Pending = MoveTemp(Waiters);
	Waiters.Reset();
	for (const FWaiter& Waiter : Pending)
	{
		if (Waiter.Responder.IsValid())
		{
			FN2CMcpReply Reply;
			Reply.Body = ResultResponse(Waiter.Id);
			Reply.SessionId = SessionId;
			Waiter.Responder->Reply(Reply);
		}
	}
}

void FN2CMcpProtocol::PruneExpiredTasks()
{
	const FDateTime Now = FDateTime::UtcNow();
	for (auto It = Tasks.CreateIterator(); It; ++It)
	{
		const TSharedRef<FTask>& Task = It.Value();
		// Expired: ttl after creation, and finished (a running call keeps its task; nobody waits on an expired one)
		if (Task->IsTerminal() && Task->Waiters.Num() == 0 && (Now - Task->CreatedAt).GetTotalMilliseconds() > static_cast<double>(Task->TtlMs))
		{
			It.RemoveCurrent();
		}
	}
}

bool FN2CMcpProtocol::TryStartTaskCall(const TSharedRef<IN2CMcpResponder>& Responder, const TSharedPtr<FJsonObject>& RequestObject,
	const TSharedPtr<FJsonObject>& Params, const FString& ToolName, const FString& SessionId)
{
	using namespace N2CMcpTasksPrivate;

	const TSharedPtr<FJsonObject>* TaskParams = nullptr;
	if (SessionId.IsEmpty() || !Params->TryGetObjectField(TEXT("task"), TaskParams) || !TaskParams || !TaskParams->IsValid())
	{
		return false;
	}
	// Only a tool that declares taskSupport runs as a task; for any other the task field is ignored
	const TOptional<FN2CToolDescriptor> Descriptor = FN2CToolCatalog(*Catalogs.Tools).Find(ToolName);
	if (!Descriptor.IsSet() || Descriptor->ToMcpDefinition().TaskSupport.IsEmpty())
	{
		return false;
	}
	PruneExpiredTasks();

	double RequestedTtl = static_cast<double>(DefaultTaskTtlMs);
	(*TaskParams)->TryGetNumberField(TEXT("ttl"), RequestedTtl);
	const TSharedRef<FTask> Task = MakeShared<FTask>();
	Task->TaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Task->SessionId = SessionId;
	Task->CallId = FGuid::NewGuid();
	Task->CreatedAt = FDateTime::UtcNow();
	Task->LastUpdatedAt = Task->CreatedAt;
	Task->TtlMs = FMath::Clamp(static_cast<int64>(RequestedTtl), static_cast<int64>(1000), MaxTaskTtlMs);
	Task->StatusMessage = TEXT("Started");

	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	const TSharedPtr<FJsonObject> Arguments = Params->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid()
		? *ArgsObj : MakeShared<FJsonObject>();
	const TSharedPtr<FJsonValue> RequestId = RequestObject->TryGetField(TEXT("id"));

	// The call reports to the task, never to this request (already answered with the CreateTaskResult)
	const TWeakPtr<FTask> Weak = Task;
	FN2CAsyncTaskResponder CallResponder;
	CallResponder.SendProgress = [Weak](const FString& NotificationJson)
	{
		const TSharedPtr<FTask> Pinned = Weak.Pin();
		const TSharedPtr<FJsonObject> Notification = ParseObject(NotificationJson);
		const TSharedPtr<FJsonObject>* NotificationParams = nullptr;
		FString Message;
		if (Pinned.IsValid() && !Pinned->IsTerminal() && Notification.IsValid() && Notification->TryGetObjectField(TEXT("params"), NotificationParams)
			&& (*NotificationParams)->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
		{
			Pinned->StatusMessage = Message;
			Pinned->LastUpdatedAt = FDateTime::UtcNow();
		}
	};
	CallResponder.SendResponse = [Weak](const FString& ResponseJson)
	{
		const TSharedPtr<FTask> Pinned = Weak.Pin();
		if (!Pinned.IsValid() || Pinned->IsTerminal())
		{
			return;
		}
		const TSharedPtr<FJsonObject> Response = ParseObject(ResponseJson);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		bool bIsError = true;
		if (Response.IsValid() && Response->TryGetObjectField(TEXT("result"), Result))
		{
			Pinned->Result = *Result;
			bIsError = false;
			(*Result)->TryGetBoolField(TEXT("isError"), bIsError);
		}
		else
		{
			// A JSON-RPC error instead of a result: kept as a CallToolResult with isError, so tasks/result has one
			FString Message = TEXT("The call failed");
			const TSharedPtr<FJsonObject>* Error = nullptr;
			if (Response.IsValid() && Response->TryGetObjectField(TEXT("error"), Error))
			{
				(*Error)->TryGetStringField(TEXT("message"), Message);
			}
			Pinned->Result = FMcpToolCallResult::CreateErrorResult(Message).ToJson();
		}
		// basic/utilities/tasks: a tool result with isError true ends the task as failed
		Pinned->Status = bIsError ? TEXT("failed") : TEXT("completed");
		Pinned->StatusMessage = bIsError ? TEXT("The call returned an error result") : TEXT("Completed");
		Pinned->LastUpdatedAt = FDateTime::UtcNow();
		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP task %s %s"), *Pinned->TaskId, *Pinned->Status), EN2CLogSeverity::Info);
		Pinned->AnswerWaiters();
	};
	CallResponder.EndWithoutResponse = [Weak]()
	{
		const TSharedPtr<FTask> Pinned = Weak.Pin();
		if (Pinned.IsValid() && !Pinned->IsTerminal())
		{
			Pinned->Status = TEXT("cancelled");
			Pinned->StatusMessage = TEXT("Cancelled");
			Pinned->LastUpdatedAt = FDateTime::UtcNow();
			Pinned->AnswerWaiters();
		}
	};

	Tasks.Add(Task->TaskId, Task);
	// The task id is the progress token, so the call's progress reaches SendProgress (statusMessage) and nothing else
	const FGuid Launched = Catalogs.Tasks->LaunchTask(Task->CallId, ToolName, Arguments, Task->TaskId,
		MakeShared<FJsonValueString>(Task->TaskId), SessionId, RequestId, CallResponder);
	FN2CMcpReply Reply;
	Reply.SessionId = SessionId;
	if (!Launched.IsValid())
	{
		Tasks.Remove(Task->TaskId);
		Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(RequestId, JsonRpcErrorCodes::InternalError,
			FString::Printf(TEXT("Failed to start tool '%s'"), *ToolName)));
		Responder->Reply(Reply);
		return true;
	}

	// CreateTaskResult
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("task"), Task->ToJson());
	Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateSuccessResponse(RequestId, MakeShared<FJsonValueObject>(Result)));
	Responder->Reply(Reply);
	FN2CLogger::Get().Log(FString::Printf(TEXT("Started task %s for tool %s (MCP task, ttl %lld ms)"), *Task->TaskId, *ToolName, Task->TtlMs),
		EN2CLogSeverity::Info);
	return true;
}

bool FN2CMcpProtocol::TryHandleTasksResult(const TSharedRef<IN2CMcpResponder>& Responder, const TSharedPtr<FJsonObject>& RequestObject,
	const FString& SessionId)
{
	using namespace N2CMcpTasksPrivate;
	const TSharedPtr<FJsonValue> Id = RequestObject->TryGetField(TEXT("id"));
	const FString TaskId = TaskIdOf(RequestObject->TryGetField(TEXT("params")));
	PruneExpiredTasks();
	const TSharedRef<FTask>* Found = Tasks.Find(TaskId);
	FN2CMcpReply Reply;
	Reply.SessionId = SessionId;
	if (!Found || (*Found)->SessionId != SessionId)
	{
		Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams,
			FString::Printf(TEXT("Unknown taskId '%s'"), *TaskId.Left(80))));
		Responder->Reply(Reply);
		return true;
	}
	if ((*Found)->IsTerminal())
	{
		Reply.Body = (*Found)->ResultResponse(Id);
		Responder->Reply(Reply);
		return true;
	}
	// Held until the task ends (basic/utilities/tasks: tasks/result blocks until the task is terminal)
	(*Found)->Waiters.Add({ Responder, Id });
	return true;
}

bool FN2CMcpProtocol::HandleTasksGet(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	using namespace N2CMcpTasksPrivate;
	PruneExpiredTasks();
	const FString TaskId = TaskIdOf(Params);
	const TSharedRef<FTask>* Found = Tasks.Find(TaskId);
	if (!Found || (*Found)->SessionId != ActiveSessionId)
	{
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, FString::Printf(TEXT("Unknown taskId '%s'"), *TaskId.Left(80)));
		return true;
	}
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShared<FJsonValueObject>((*Found)->ToJson()));
	return true;
}

bool FN2CMcpProtocol::HandleTasksList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	PruneExpiredTasks();
	TArray<TSharedRef<FTask>> Mine;
	for (const TPair<FString, TSharedRef<FTask>>& Pair : Tasks)
	{
		if (Pair.Value->SessionId == ActiveSessionId)
		{
			Mine.Add(Pair.Value);
		}
	}
	Mine.Sort([](const TSharedRef<FTask>& A, const TSharedRef<FTask>& B) { return A->CreatedAt < B->CreatedAt; });
	TArray<TSharedPtr<FJsonValue>> List;
	for (const TSharedRef<FTask>& Task : Mine)
	{
		List.Add(MakeShared<FJsonValueObject>(Task->ToJson()));
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tasks"), List);
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShared<FJsonValueObject>(Result));
	return true;
}

bool FN2CMcpProtocol::HandleTasksCancel(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	using namespace N2CMcpTasksPrivate;
	PruneExpiredTasks();
	const FString TaskId = TaskIdOf(Params);
	const TSharedRef<FTask>* Found = Tasks.Find(TaskId);
	if (!Found || (*Found)->SessionId != ActiveSessionId)
	{
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, FString::Printf(TEXT("Unknown taskId '%s'"), *TaskId.Left(80)));
		return true;
	}
	const TSharedRef<FTask> Task = *Found;
	if (Task->IsTerminal())
	{
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams,
			FString::Printf(TEXT("Task %s is already %s and cannot be cancelled"), *TaskId, *Task->Status));
		return true;
	}
	// The task is cancelled now; the call stops through its context and whatever it still reports is ignored
	Task->Status = TEXT("cancelled");
	Task->StatusMessage = TEXT("Cancelled with tasks/cancel");
	Task->LastUpdatedAt = FDateTime::UtcNow();
	Catalogs.Tasks->CancelTask(Task->CallId, TEXT("The client cancelled the task (tasks/cancel)"));
	Task->AnswerWaiters();
	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP task %s cancelled with tasks/cancel"), *TaskId), EN2CLogSeverity::Info);
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShared<FJsonValueObject>(Task->ToJson()));
	return true;
}
