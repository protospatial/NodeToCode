// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpTasksSpec
{
	const TCHAR* TaskToolName = TEXT("n2c-test-tasks-long");
	const TCHAR* PlainToolName = TEXT("n2c-test-tasks-plain");

	/** A long-running tool the spec finishes; with bTaskSupport it declares taskSupport optional */
	class FLongTool : public IN2CMcpTool
	{
	public:
		FLongTool(const TCHAR* InName, bool bInTaskSupport) : Name(InName), bTaskSupport(bInTaskSupport) {}

		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(Name, TEXT("Long-running spec tool registered by NodeToCode.MCP.Tasks"));
			TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Definition.InputSchema = Schema;
			Definition.bIsLongRunning = true;
			Definition.bAllowedDuringPlaySession = true;
			if (bTaskSupport)
			{
				Definition.TaskSupport = TEXT("optional");
			}
			return Definition;
		}

		struct FPending
		{
			TPromise<FMcpToolCallResult> Promise;
			bool bSet = false;
			void Set(const FMcpToolCallResult& Result) { if (!bSet) { bSet = true; Promise.SetValue(Result); } }
		};

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>&) override
		{
			const TSharedRef<FPending> Pending = MakeShared<FPending>();
			Promises.Add(Pending);
			LastContext = Context;
			++Invocations;
			const TWeakPtr<FPending> WeakPending = Pending;
			const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;
			Context->OnCancelled([WeakPending, WeakContext]()
			{
				const TSharedPtr<FPending> Pinned = WeakPending.Pin();
				const FN2CToolCallContextPtr PinnedContext = WeakContext.Pin();
				if (Pinned.IsValid() && PinnedContext.IsValid())
				{
					Pinned->Set(FMcpToolCallResult::CreateErrorResult(PinnedContext->GetCancelledMessage()));
				}
			});
			return Pending->Promise.GetFuture();
		}

		void Progress(const FString& Message) { if (LastContext.IsValid()) { LastContext->ReportProgress(0.5f, Message); } }

		void Finish(const FMcpToolCallResult& Result)
		{
			TArray<TSharedPtr<FPending>> Pending = MoveTemp(Promises);
			Promises.Reset();
			for (const TSharedPtr<FPending>& Call : Pending)
			{
				Call->Set(Result);
			}
		}

		FString Name;
		bool bTaskSupport = false;
		TArray<TSharedPtr<FPending>> Promises;
		FN2CToolCallContextPtr LastContext;
		int32 Invocations = 0;
	};

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString Request(int32 Id, const FString& Method, const FString& ParamsJson)
	{
		return FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}"), Id, *Method, *ParamsJson);
	}

	FString TaskCall(int32 Id, const FString& Tool, const FString& TaskJson = TEXT(",\"task\":{\"ttl\":60000}"))
	{
		return Request(Id, TEXT("tools/call"), FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{}%s}"), *Tool, *TaskJson));
	}

	FString TaskRequest(int32 Id, const FString& Method, const FString& TaskId)
	{
		return Request(Id, Method, FString::Printf(TEXT("{\"taskId\":\"%s\"}"), *TaskId));
	}
}

/**
 * Card 0211: MCP 2025-11-25 tasks for task-augmented tools/call (basic/utilities/tasks). initialize advertises the
 * tasks capability; a tool with TaskSupport lists execution.taskSupport. A session's call with params.task answers a
 * CreateTaskResult at once; tasks/get polls it (statusMessage from progress), tasks/result is held until it is
 * terminal and then answers the CallToolResult with the related-task _meta, tasks/list lists the session's tasks,
 * tasks/cancel cancels the call. An isError result ends the task as failed. Tasks are per session; a tool without
 * taskSupport ignores params.task, and so does a 2026-07-28 call.
 */
BEGIN_DEFINE_SPEC(FN2CMcpTasksSpec, "NodeToCode.MCP.Tasks", N2C_TEST_FLAGS)

	TSharedPtr<N2CMcpTasksSpec::FLongTool> TaskTool;
	TSharedPtr<N2CMcpTasksSpec::FLongTool> PlainTool;

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(FN2CMcpProtocol& Core, const FString& Body, const FString& SessionId = FString())
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
		Core.Handle(N2CTest::MakeMcpInbound(Body, TEXT("application/json, text/event-stream"), SessionId), Responder);
		return Responder;
	}

	FString Open(FN2CMcpProtocol& Core, TSharedPtr<FJsonObject>* OutInit = nullptr)
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Init = Send(Core, TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":")
			TEXT("{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-tasks-spec\",\"version\":\"1\"}}}"));
		if (OutInit)
		{
			*OutInit = N2CMcpTasksSpec::ParseObject(Init->LastReply().Body);
		}
		return Init->LastReply().SessionId;
	}

	TSharedPtr<FJsonObject> ResultOf(const N2CTest::FN2CMcpMemoryResponder& Responder)
	{
		const TSharedPtr<FJsonObject> Message = N2CMcpTasksSpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		return Message.IsValid() && Message->TryGetObjectField(TEXT("result"), Result) ? *Result : nullptr;
	}

	int32 ErrorCodeOf(const N2CTest::FN2CMcpMemoryResponder& Responder)
	{
		const TSharedPtr<FJsonObject> Message = N2CMcpTasksSpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		return Message.IsValid() && Message->TryGetObjectField(TEXT("error"), Error) ? static_cast<int32>((*Error)->GetNumberField(TEXT("code"))) : 0;
	}

	/** Starts a task call and returns its taskId (empty with a test error when no CreateTaskResult came back) */
	FString StartTask(FN2CMcpProtocol& Core, const FString& Session, int32 Id)
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Created = Send(Core, N2CMcpTasksSpec::TaskCall(Id, N2CMcpTasksSpec::TaskToolName), Session);
		const TSharedPtr<FJsonObject> Result = ResultOf(*Created);
		const TSharedPtr<FJsonObject>* Task = nullptr;
		if (!TestTrue(TEXT("CreateTaskResult: ") + Created->LastReply().Body.Left(300), Result.IsValid() && Result->TryGetObjectField(TEXT("task"), Task)))
		{
			return FString();
		}
		TestEqual(TEXT("answered at once, once"), Created->ReplyCount, 1);
		TestEqual(TEXT("working"), (*Task)->GetStringField(TEXT("status")), FString(TEXT("working")));
		TestEqual(TEXT("ttl as asked"), static_cast<int32>((*Task)->GetNumberField(TEXT("ttl"))), 60000);
		TestTrue(TEXT("pollInterval"), (*Task)->HasField(TEXT("pollInterval")));
		TestFalse(TEXT("createdAt"), (*Task)->GetStringField(TEXT("createdAt")).IsEmpty());
		return (*Task)->GetStringField(TEXT("taskId"));
	}

END_DEFINE_SPEC(FN2CMcpTasksSpec)

void FN2CMcpTasksSpec::Define()
{
	using namespace N2CMcpTasksSpec;

	BeforeEach([this]()
	{
		TaskTool = MakeShared<FLongTool>(TaskToolName, true);
		PlainTool = MakeShared<FLongTool>(PlainToolName, false);
		TestTrue(TEXT("registered"), FN2CMcpToolManager::Get().RegisterTool(TaskTool) && FN2CMcpToolManager::Get().RegisterTool(PlainTool));
	});

	AfterEach([this]()
	{
		TaskTool->Finish(FMcpToolCallResult::CreateTextResult(TEXT("cleanup")));
		PlainTool->Finish(FMcpToolCallResult::CreateTextResult(TEXT("cleanup")));
		FN2CMcpToolManager::Get().UnregisterTool(TaskToolName);
		FN2CMcpToolManager::Get().UnregisterTool(PlainToolName);
		TaskTool.Reset();
		PlainTool.Reset();
	});

	It(TEXT("advertises tasks on initialize and execution.taskSupport on the tool"), [this]()
	{
		FN2CMcpProtocol Core;
		TSharedPtr<FJsonObject> Init;
		const FString Session = Open(Core, &Init);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		const TSharedPtr<FJsonObject>* Tasks = nullptr;
		if (TestTrue(TEXT("tasks capability"), Init.IsValid() && Init->TryGetObjectField(TEXT("result"), Result)
			&& (*Result)->GetObjectField(TEXT("capabilities"))->TryGetObjectField(TEXT("tasks"), Tasks)))
		{
			TestTrue(TEXT("cancel and list"), (*Tasks)->HasField(TEXT("cancel")) && (*Tasks)->HasField(TEXT("list")));
			TestTrue(TEXT("tools/call can be a task"), (*Tasks)->GetObjectField(TEXT("requests"))->GetObjectField(TEXT("tools"))->HasField(TEXT("call")));
		}
		const TSharedPtr<FJsonObject> List = ResultOf(*Send(Core, Request(2, TEXT("tools/list"), TEXT("{}")), Session));
		FString TaskSupport, PlainSupport = TEXT("none");
		for (const TSharedPtr<FJsonValue>& Tool : List.IsValid() ? List->GetArrayField(TEXT("tools")) : TArray<TSharedPtr<FJsonValue>>())
		{
			const TSharedPtr<FJsonObject> Entry = Tool->AsObject();
			const TSharedPtr<FJsonObject>* Execution = nullptr;
			if (Entry->GetStringField(TEXT("name")) == TaskToolName && Entry->TryGetObjectField(TEXT("execution"), Execution))
			{
				TaskSupport = (*Execution)->GetStringField(TEXT("taskSupport"));
			}
			if (Entry->GetStringField(TEXT("name")) == PlainToolName)
			{
				PlainSupport = Entry->HasField(TEXT("execution")) ? TEXT("execution") : TEXT("");
			}
		}
		TestEqual(TEXT("the task tool: optional"), TaskSupport, FString(TEXT("optional")));
		TestEqual(TEXT("the plain tool: no execution (forbidden)"), PlainSupport, FString());
		const TSharedPtr<IN2CMcpTool> Translate = N2CTest::FindRegisteredTool(TEXT("translate-focused-blueprint"));
		TestTrue(TEXT("translate-focused-blueprint declares taskSupport optional"), Translate.IsValid() && Translate->GetDefinition().TaskSupport == TEXT("optional"));
	});

	It(TEXT("creates a task, polls it, holds tasks/result until it completes, then answers the result with related-task"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Session = Open(Core);
		const FString TaskId = StartTask(Core, Session, 3);
		if (TaskId.IsEmpty())
		{
			return;
		}
		TestEqual(TEXT("the tool runs"), TaskTool->Invocations, 1);

		TaskTool->Progress(TEXT("Waiting for the LLM response"));
		const TSharedPtr<FJsonObject> Polled = ResultOf(*Send(Core, TaskRequest(4, TEXT("tasks/get"), TaskId), Session));
		TestTrue(TEXT("tasks/get: working, with the progress message"), Polled.IsValid() && Polled->GetStringField(TEXT("status")) == TEXT("working")
			&& Polled->GetStringField(TEXT("statusMessage")) == TEXT("Waiting for the LLM response"));

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Held = Send(Core, TaskRequest(5, TEXT("tasks/result"), TaskId), Session);
		TestEqual(TEXT("tasks/result is held while the task works"), Held->ReplyCount, 0);

		TaskTool->Finish(FMcpToolCallResult::CreateTextResult(TEXT("translated")));
		TestEqual(TEXT("tasks/result answered once the task completed"), Held->ReplyCount, 1);
		const TSharedPtr<FJsonObject> Result = ResultOf(*Held);
		if (TestTrue(TEXT("a CallToolResult"), Result.IsValid()))
		{
			TestEqual(TEXT("its text"), Result->GetArrayField(TEXT("content"))[0]->AsObject()->GetStringField(TEXT("text")), FString(TEXT("translated")));
			TestEqual(TEXT("related-task"), Result->GetObjectField(TEXT("_meta"))->GetObjectField(TEXT("io.modelcontextprotocol/related-task"))
				->GetStringField(TEXT("taskId")), TaskId);
		}
		const TSharedPtr<FJsonObject> Done = ResultOf(*Send(Core, TaskRequest(6, TEXT("tasks/get"), TaskId), Session));
		TestTrue(TEXT("tasks/get: completed, no pollInterval"), Done.IsValid() && Done->GetStringField(TEXT("status")) == TEXT("completed")
			&& !Done->HasField(TEXT("pollInterval")));
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Again = Send(Core, TaskRequest(7, TEXT("tasks/result"), TaskId), Session);
		TestTrue(TEXT("tasks/result of a finished task answers at once"), Again->ReplyCount == 1 && ResultOf(*Again).IsValid());

		const TSharedPtr<FJsonObject> Listed = ResultOf(*Send(Core, Request(8, TEXT("tasks/list"), TEXT("{}")), Session));
		TestTrue(TEXT("tasks/list has it"), Listed.IsValid() && Listed->GetArrayField(TEXT("tasks")).Num() == 1
			&& Listed->GetArrayField(TEXT("tasks"))[0]->AsObject()->GetStringField(TEXT("taskId")) == TaskId);
		TestEqual(TEXT("cancelling a completed task is -32602"), ErrorCodeOf(*Send(Core, TaskRequest(9, TEXT("tasks/cancel"), TaskId), Session)),
			JsonRpcErrorCodes::InvalidParams);
	});

	It(TEXT("cancels a task with tasks/cancel: the call's context is cancelled and a held tasks/result gets an error"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Session = Open(Core);
		const FString TaskId = StartTask(Core, Session, 3);
		if (TaskId.IsEmpty())
		{
			return;
		}
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Held = Send(Core, TaskRequest(4, TEXT("tasks/result"), TaskId), Session);
		const TSharedPtr<FJsonObject> Cancelled = ResultOf(*Send(Core, TaskRequest(5, TEXT("tasks/cancel"), TaskId), Session));
		TestTrue(TEXT("tasks/cancel answers the task, cancelled"), Cancelled.IsValid() && Cancelled->GetStringField(TEXT("status")) == TEXT("cancelled"));
		TestTrue(TEXT("the call's context is cancelled"), TaskTool->LastContext.IsValid() && TaskTool->LastContext->IsCancelled());
		TestEqual(TEXT("the held tasks/result is answered"), Held->ReplyCount, 1);
		TestEqual(TEXT("with an error"), ErrorCodeOf(*Held), JsonRpcErrorCodes::InternalError);
		const TSharedPtr<FJsonObject> After = ResultOf(*Send(Core, TaskRequest(6, TEXT("tasks/get"), TaskId), Session));
		TestTrue(TEXT("still cancelled after the call reported"), After.IsValid() && After->GetStringField(TEXT("status")) == TEXT("cancelled"));
	});

	It(TEXT("ends a task whose call returned an error result as failed, keeps tasks per session, and ignores task where it does not apply"), [this]()
	{
		FN2CMcpProtocol Core;
		const FString Session = Open(Core);
		const FString TaskId = StartTask(Core, Session, 3);
		if (TaskId.IsEmpty())
		{
			return;
		}
		TaskTool->Finish(FMcpToolCallResult::CreateErrorResult(TEXT("No active Blueprint Editor.")));
		const TSharedPtr<FJsonObject> Failed = ResultOf(*Send(Core, TaskRequest(4, TEXT("tasks/get"), TaskId), Session));
		TestTrue(TEXT("failed"), Failed.IsValid() && Failed->GetStringField(TEXT("status")) == TEXT("failed"));
		const TSharedPtr<FJsonObject> FailedResult = ResultOf(*Send(Core, TaskRequest(5, TEXT("tasks/result"), TaskId), Session));
		bool bIsError = false;
		TestTrue(TEXT("tasks/result is the isError CallToolResult"), FailedResult.IsValid() && FailedResult->TryGetBoolField(TEXT("isError"), bIsError) && bIsError);

		const FString Other = Open(Core);
		TestEqual(TEXT("another session cannot see it"), ErrorCodeOf(*Send(Core, TaskRequest(6, TEXT("tasks/get"), TaskId), Other)), JsonRpcErrorCodes::InvalidParams);
		TestEqual(TEXT("an unknown taskId is -32602"), ErrorCodeOf(*Send(Core, TaskRequest(7, TEXT("tasks/get"), TEXT("nope")), Session)), JsonRpcErrorCodes::InvalidParams);
		const TSharedPtr<FJsonObject> OtherList = ResultOf(*Send(Core, Request(8, TEXT("tasks/list"), TEXT("{}")), Other));
		TestTrue(TEXT("the other session lists none"), OtherList.IsValid() && OtherList->GetArrayField(TEXT("tasks")).Num() == 0);

		// A tool without taskSupport runs its call as usual: held until it finishes, no CreateTaskResult
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Plain = Send(Core, TaskCall(9, PlainToolName), Session);
		TestEqual(TEXT("plain: held"), Plain->ReplyCount, 0);
		PlainTool->Finish(FMcpToolCallResult::CreateTextResult(TEXT("plain done")));
		const TSharedPtr<FJsonObject> PlainResult = ResultOf(*Plain);
		TestTrue(TEXT("plain: the CallToolResult itself"), PlainResult.IsValid() && PlainResult->HasField(TEXT("content")) && !PlainResult->HasField(TEXT("task")));

		// A call without params.task runs as usual too
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> NoTask = Send(Core, TaskCall(10, TaskToolName, FString()), Session);
		TestEqual(TEXT("no task field: held"), NoTask->ReplyCount, 0);
		TaskTool->Finish(FMcpToolCallResult::CreateTextResult(TEXT("direct")));
		TestTrue(TEXT("no task field: the CallToolResult"), ResultOf(*NoTask).IsValid() && ResultOf(*NoTask)->HasField(TEXT("content")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
