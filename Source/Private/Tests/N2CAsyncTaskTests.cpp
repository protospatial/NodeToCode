// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Async/N2CToolAsyncTaskBase.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Server/N2CSseServer.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace N2CAsyncTaskTestsPrivate
{
	/** What a stub task reported through its delegates */
	struct FObserver
	{
		int32 Completions = 0;
		int32 ProgressReports = 0;
		int32 ErrorCompletions = 0;
	};

	/**
	 * Minimal async task. With bCompleteInExecute it reports progress and its result inside Execute(), like a task whose
	 * work fails before it is sent; otherwise it waits until cancelled, like a translation waiting for the LLM.
	 */
	class FStubAsyncTask : public FN2CToolAsyncTaskBase
	{
	public:
		FStubAsyncTask(bool bInCompleteInExecute)
			: FN2CToolAsyncTaskBase(FGuid::NewGuid(), FGuid::NewGuid().ToString(), TEXT("n2c-test-stub"), MakeShared<FJsonObject>())
			, bCompleteInExecute(bInCompleteInExecute)
		{
		}

		virtual void Execute() override
		{
			ReportProgress(0.5f, TEXT("stub working"));
			if (bCompleteInExecute)
			{
				ReportComplete(FMcpToolCallResult::CreateTextResult(TEXT("stub done")));
			}
		}

		virtual void RequestCancel() override
		{
			FN2CToolAsyncTaskBase::RequestCancel();
			if (!IsComplete())
			{
				ReportComplete(FMcpToolCallResult::CreateErrorResult(TEXT("Task was cancelled")));
			}
		}

		/** Reports from the calling thread (the protected reporters, exposed for the test) */
		void ReportFromAnyThread()
		{
			ReportProgress(0.75f, TEXT("off-thread progress"));
			ReportComplete(FMcpToolCallResult::CreateTextResult(TEXT("off-thread result")));
		}

	private:
		bool bCompleteInExecute;
	};

	using FStubRef = TSharedRef<FStubAsyncTask, ESPMode::ThreadSafe>;

	/** Binds counting delegates directly (no task manager) */
	static void Observe(const FStubRef& Task, const TSharedRef<FObserver>& Observer)
	{
		Task->SetProgressDelegate(FN2CAsyncTaskProgressDelegate::CreateLambda([Observer](float, const FString&)
		{
			++Observer->ProgressReports;
		}));
		Task->SetCompleteDelegate(FN2CAsyncTaskCompleteDelegate::CreateLambda([Observer](const FMcpToolCallResult& Result)
		{
			++Observer->Completions;
			Observer->ErrorCompletions += Result.bIsError ? 1 : 0;
		}));
	}

	/** Launches the task through the manager, the way the MCP server does (stream prepared first) */
	static void Launch(const FStubRef& Task)
	{
		NodeToCodeSseServer::PrepareSseStreamForTask(Task->GetTaskId());
		FN2CToolAsyncTaskManager::Get().LaunchTaskInstance(Task, TEXT("n2c-test-stub"), MakeShared<FJsonObject>(),
			TEXT("n2c-test-session"), MakeShared<FJsonValueNumber>(1));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CAsyncTaskCleanupTest, "NodeToCode.Robustness.AsyncTaskCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CAsyncTaskCleanupTest::RunTest(const FString& Parameters)
{
	using namespace N2CAsyncTaskTestsPrivate;

	if (!TestTrue(TEXT("test runs on the game thread"), IsInGameThread()))
	{
		return false;
	}

	FN2CToolAsyncTaskManager& Manager = FN2CToolAsyncTaskManager::Get();
	const int32 BaseTasks = Manager.GetTrackedTaskCount();
	const int32 BaseTokens = Manager.GetTrackedProgressTokenCount();

	// 1. Fifty tasks that complete: each is released by the manager once it has delivered its result
	constexpr int32 NumCompleted = 50;
	TArray<TWeakPtr<FStubAsyncTask, ESPMode::ThreadSafe>> CompletedTasks;
	TArray<FGuid> CompletedTaskIds;
	TArray<FString> CompletedTokens;
	for (int32 Index = 0; Index < NumCompleted; ++Index)
	{
		FStubRef Task = MakeShared<FStubAsyncTask, ESPMode::ThreadSafe>(true);
		CompletedTasks.Add(Task);
		CompletedTaskIds.Add(Task->GetTaskId());
		CompletedTokens.Add(Task->GetProgressToken());
		Launch(Task);
	}

	TestEqual(TEXT("no completed task is still tracked"), Manager.GetTrackedTaskCount(), BaseTasks);
	TestEqual(TEXT("no completed task's progress token is still mapped"), Manager.GetTrackedProgressTokenCount(), BaseTokens);

	int32 NumAlive = 0;
	int32 NumStreamsKept = 0;
	int32 NumRemembered = 0;
	int32 NumCancelAccepted = 0;
	for (int32 Index = 0; Index < NumCompleted; ++Index)
	{
		NumAlive += CompletedTasks[Index].IsValid() ? 1 : 0;
		NumStreamsKept += NodeToCodeSseServer::HasStreamForTask(CompletedTaskIds[Index]) ? 1 : 0;
		NumRemembered += Manager.WasProgressTokenCompleted(CompletedTokens[Index]) ? 1 : 0;
		NumCancelAccepted += Manager.CancelTaskByProgressToken(CompletedTokens[Index]) ? 1 : 0;
	}
	TestEqual(TEXT("every completed task object was freed"), NumAlive, 0);
	TestEqual(TEXT("each result waits on its SSE stream for a client that has not connected yet"), NumStreamsKept, NumCompleted);
	TestEqual(TEXT("each finished progress token is remembered (cancel answers task_already_completed)"), NumRemembered, NumCompleted);
	TestEqual(TEXT("cancel is refused for a finished task"), NumCancelAccepted, 0);

	// Streams nobody connected to expire after the TTL (here: at once)
	NodeToCodeSseServer::ExpireCompletedStreams(0.0);
	int32 NumStreamsLeft = 0;
	for (const FGuid& TaskId : CompletedTaskIds)
	{
		NumStreamsLeft += NodeToCodeSseServer::HasStreamForTask(TaskId) ? 1 : 0;
	}
	TestEqual(TEXT("expired streams are dropped"), NumStreamsLeft, 0);

	// 2. Running tasks are tracked until CancelAllTasks (the shutdown path) cancels and removes them
	constexpr int32 NumRunning = 5;
	TArray<TWeakPtr<FStubAsyncTask, ESPMode::ThreadSafe>> RunningTasks;
	TArray<FGuid> RunningTaskIds;
	for (int32 Index = 0; Index < NumRunning; ++Index)
	{
		FStubRef Task = MakeShared<FStubAsyncTask, ESPMode::ThreadSafe>(false);
		RunningTasks.Add(Task);
		RunningTaskIds.Add(Task->GetTaskId());
		Launch(Task);
	}

	TestEqual(TEXT("running tasks are tracked"), Manager.GetTrackedTaskCount(), BaseTasks + NumRunning);
	TestEqual(TEXT("running tasks' progress tokens are mapped"), Manager.GetTrackedProgressTokenCount(), BaseTokens + NumRunning);
	int32 NumRunningNow = 0;
	for (const FGuid& TaskId : RunningTaskIds)
	{
		NumRunningNow += Manager.IsTaskRunning(TaskId) ? 1 : 0;
	}
	TestEqual(TEXT("running tasks report running"), NumRunningNow, NumRunning);

	Manager.CancelAllTasks();

	TestEqual(TEXT("CancelAllTasks leaves no task tracked"), Manager.GetTrackedTaskCount(), BaseTasks);
	TestEqual(TEXT("CancelAllTasks leaves no progress token mapped"), Manager.GetTrackedProgressTokenCount(), BaseTokens);
	int32 NumRunningAlive = 0;
	for (const TWeakPtr<FStubAsyncTask, ESPMode::ThreadSafe>& Task : RunningTasks)
	{
		NumRunningAlive += Task.IsValid() ? 1 : 0;
	}
	TestEqual(TEXT("every cancelled task object was freed"), NumRunningAlive, 0);

	NodeToCodeSseServer::ExpireCompletedStreams(0.0);
	int32 NumRunningStreamsLeft = 0;
	for (const FGuid& TaskId : RunningTaskIds)
	{
		NumRunningStreamsLeft += NodeToCodeSseServer::HasStreamForTask(TaskId) ? 1 : 0;
	}
	TestEqual(TEXT("cancelled tasks' streams expire too"), NumRunningStreamsLeft, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CAsyncTaskQueuedReportTest, "NodeToCode.Robustness.AsyncTaskQueuedReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CAsyncTaskQueuedReportTest::RunTest(const FString& Parameters)
{
	using namespace N2CAsyncTaskTestsPrivate;

	if (!TestTrue(TEXT("test runs on the game thread"), IsInGameThread()))
	{
		return false;
	}

	// A report made off the game thread is queued there. If the task is released before the queue runs, the queued
	// report must find nothing (it holds the task weakly) instead of running on a freed task.
	{
		TSharedRef<FObserver> Observer = MakeShared<FObserver>();
		FStubRef Task = MakeShared<FStubAsyncTask, ESPMode::ThreadSafe>(false);
		Observe(Task, Observer);
		TWeakPtr<FStubAsyncTask, ESPMode::ThreadSafe> WeakTask = Task;

		// Raw pointer: only this scope owns the task, and it outlives the wait
		FStubAsyncTask* RawTask = &Task.Get();
		TFuture<void> Reported = Async(EAsyncExecution::Thread, [RawTask]()
		{
			RawTask->ReportFromAnyThread();
		});
		Reported.Wait();

		Task = MakeShared<FStubAsyncTask, ESPMode::ThreadSafe>(false); // drop the last reference to the reported task
		TestFalse(TEXT("the reported task was released before the game thread ran its queued reports"), WeakTask.IsValid());

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestEqual(TEXT("no queued progress ran for the released task"), Observer->ProgressReports, 0);
		TestEqual(TEXT("no queued completion ran for the released task"), Observer->Completions, 0);
	}

	// While the task is alive, the same off-thread reports arrive on the game thread
	{
		TSharedRef<FObserver> Observer = MakeShared<FObserver>();
		FStubRef Task = MakeShared<FStubAsyncTask, ESPMode::ThreadSafe>(false);
		Observe(Task, Observer);

		FStubAsyncTask* RawTask = &Task.Get();
		TFuture<void> Reported = Async(EAsyncExecution::Thread, [RawTask]()
		{
			RawTask->ReportFromAnyThread();
		});
		Reported.Wait();
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

		TestEqual(TEXT("queued progress ran for the live task"), Observer->ProgressReports, 1);
		TestEqual(TEXT("queued completion ran for the live task"), Observer->Completions, 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
