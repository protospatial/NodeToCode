// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Async/TaskGraphInterfaces.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include <atomic>

namespace N2CAsyncTaskTestsPrivate
{
	/** One stub call: its promise is set once, and it is never broken */
	struct FStubCall
	{
		TPromise<FMcpToolCallResult> Promise;
		std::atomic<bool> bSet{false};

		void Set(const FMcpToolCallResult& Result)
		{
			if (!bSet.exchange(true))
			{
				Promise.SetValue(Result);
			}
		}
		~FStubCall()
		{
			Set(FMcpToolCallResult::CreateErrorResult(TEXT("stub call dropped")));
		}
	};

	/**
	 * Minimal long-running tool. With bCompleteInInvoke each call reports progress and its result inside InvokeAsync,
	 * like a call whose work fails before it is sent; otherwise it waits until cancelled, like a translation waiting
	 * for the LLM. The tool keeps only weak references to its calls, so a test can see them freed.
	 */
	class FStubTool : public IN2CMcpTool
	{
	public:
		explicit FStubTool(bool bInCompleteInInvoke) : bCompleteInInvoke(bInCompleteInInvoke) {}

		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(TEXT("n2c-test-stub"));
			Definition.bIsLongRunning = true;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>&) override
		{
			const TSharedRef<FStubCall, ESPMode::ThreadSafe> Call = MakeShared<FStubCall, ESPMode::ThreadSafe>();
			Calls.Add(Call);
			TFuture<FMcpToolCallResult> Future = Call->Promise.GetFuture();
			Context->ReportProgress(0.5f, TEXT("stub working"));
			if (bCompleteInInvoke)
			{
				Call->Set(FMcpToolCallResult::CreateTextResult(TEXT("stub done")));
			}
			else
			{
				// The cancel callback is the only owner of a running call
				const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;
				Context->OnCancelled([Call, WeakContext]()
				{
					const FN2CToolCallContextPtr Pinned = WeakContext.Pin();
					Call->Set(FMcpToolCallResult::CreateErrorResult(Pinned.IsValid() ? Pinned->GetCancelledMessage() : TEXT("Task was cancelled")));
				});
			}
			return Future;
		}

		TArray<TWeakPtr<FStubCall, ESPMode::ThreadSafe>> Calls;

	private:
		bool bCompleteInInvoke;
	};

	/** Launches one call through the manager, the way the MCP server does, counting the responses it writes */
	static FGuid Launch(const TSharedRef<FStubTool>& Tool, const FString& Token, const TSharedRef<int32>& Responses)
	{
		FN2CAsyncTaskResponder Responder;
		Responder.SendResponse = [Responses](const FString&) { ++*Responses; };
		return FN2CToolAsyncTaskManager::Get().LaunchToolCall(Tool, FGuid::NewGuid(), TEXT("n2c-test-stub"), MakeShared<FJsonObject>(),
			Token, TEXT("n2c-test-session"), MakeShared<FJsonValueNumber>(1), Responder);
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

	// 1. Fifty calls that complete: each is released by the manager once it has delivered its result
	constexpr int32 NumCompleted = 50;
	const TSharedRef<FStubTool> CompletingTool = MakeShared<FStubTool>(true);
	const TSharedRef<int32> CompletedResponses = MakeShared<int32>(0);
	TArray<FString> CompletedTokens;
	for (int32 Index = 0; Index < NumCompleted; ++Index)
	{
		const FString Token = FGuid::NewGuid().ToString();
		CompletedTokens.Add(Token);
		Launch(CompletingTool, Token, CompletedResponses);
	}

	TestEqual(TEXT("no completed task is still tracked"), Manager.GetTrackedTaskCount(), BaseTasks);
	TestEqual(TEXT("no completed task's progress token is still mapped"), Manager.GetTrackedProgressTokenCount(), BaseTokens);

	TestEqual(TEXT("each completed task wrote exactly one response"), *CompletedResponses, NumCompleted);

	int32 NumAlive = 0;
	int32 NumRemembered = 0;
	int32 NumCancelAccepted = 0;
	for (int32 Index = 0; Index < NumCompleted; ++Index)
	{
		NumAlive += CompletingTool->Calls[Index].IsValid() ? 1 : 0;
		NumRemembered += Manager.WasProgressTokenCompleted(CompletedTokens[Index]) ? 1 : 0;
		NumCancelAccepted += Manager.CancelTaskByProgressToken(CompletedTokens[Index]) ? 1 : 0;
	}
	TestEqual(TEXT("every completed call was freed"), NumAlive, 0);
	TestEqual(TEXT("each finished progress token is remembered (cancel answers task_already_completed)"), NumRemembered, NumCompleted);
	TestEqual(TEXT("cancel is refused for a finished task"), NumCancelAccepted, 0);

	// 2. Running calls are tracked until CancelAllTasks (the shutdown path) cancels and removes them
	constexpr int32 NumRunning = 5;
	const TSharedRef<FStubTool> WaitingTool = MakeShared<FStubTool>(false);
	const TSharedRef<int32> RunningResponses = MakeShared<int32>(0);
	TArray<FGuid> RunningTaskIds;
	for (int32 Index = 0; Index < NumRunning; ++Index)
	{
		RunningTaskIds.Add(Launch(WaitingTool, FGuid::NewGuid().ToString(), RunningResponses));
	}

	TestEqual(TEXT("running tasks are tracked"), Manager.GetTrackedTaskCount(), BaseTasks + NumRunning);
	TestEqual(TEXT("running tasks' progress tokens are mapped"), Manager.GetTrackedProgressTokenCount(), BaseTokens + NumRunning);
	int32 NumRunningNow = 0;
	for (const FGuid& TaskId : RunningTaskIds)
	{
		NumRunningNow += Manager.IsTaskRunning(TaskId) ? 1 : 0;
	}
	TestEqual(TEXT("running tasks report running"), NumRunningNow, NumRunning);
	TestEqual(TEXT("no running task has written a response"), *RunningResponses, 0);

	Manager.CancelAllTasks();

	TestEqual(TEXT("CancelAllTasks leaves no task tracked"), Manager.GetTrackedTaskCount(), BaseTasks);
	TestEqual(TEXT("CancelAllTasks leaves no progress token mapped"), Manager.GetTrackedProgressTokenCount(), BaseTokens);
	int32 NumRunningAlive = 0;
	for (const TWeakPtr<FStubCall, ESPMode::ThreadSafe>& Call : WaitingTool->Calls)
	{
		NumRunningAlive += Call.IsValid() ? 1 : 0;
	}
	TestEqual(TEXT("every cancelled call was freed"), NumRunningAlive, 0);
	TestEqual(TEXT("each cancelled task wrote exactly one response (its cancelled result)"), *RunningResponses, NumRunning);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CAsyncTaskQueuedReportTest, "NodeToCode.Robustness.AsyncTaskQueuedReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CAsyncTaskQueuedReportTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("test runs on the game thread"), IsInGameThread()))
	{
		return false;
	}

	struct FObserver
	{
		int32 ProgressReports = 0;
		int32 CancelCallbacks = 0;
	};

	// Progress reported off the game thread is queued there. If the context is released before the queue runs, the
	// queued report must find nothing (it holds the context weakly) instead of running on a freed context.
	{
		const TSharedRef<FObserver> Observer = MakeShared<FObserver>();
		FN2CToolCallContextPtr Context = FN2CToolCallContext::Create([Observer](float, const FString&) { ++Observer->ProgressReports; });
		const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;

		// Raw pointer: only this scope owns the context, and it outlives the wait
		FN2CToolCallContext* RawContext = Context.Get();
		TFuture<void> Reported = Async(EAsyncExecution::Thread, [RawContext]()
		{
			RawContext->ReportProgress(0.75f, TEXT("off-thread progress"));
		});
		Reported.Wait();

		Context.Reset(); // drop the last reference
		TestFalse(TEXT("the context was released before the game thread ran its queued reports"), WeakContext.IsValid());

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestEqual(TEXT("no queued progress ran for the released context"), Observer->ProgressReports, 0);
	}

	// While the context is alive, off-thread progress and an off-thread cancel arrive on the game thread
	{
		const TSharedRef<FObserver> Observer = MakeShared<FObserver>();
		const FN2CToolCallContextRef Context = FN2CToolCallContext::Create([Observer](float, const FString&)
		{
			check(IsInGameThread());
			++Observer->ProgressReports;
		});
		Context->OnCancelled([Observer]()
		{
			check(IsInGameThread());
			++Observer->CancelCallbacks;
		});

		TFuture<void> Reported = Async(EAsyncExecution::Thread, [Context]()
		{
			Context->ReportProgress(0.75f, TEXT("off-thread progress"));
			Context->Cancel(TEXT("off-thread cancel"));
			Context->Cancel(TEXT("second cancel"));
		});
		Reported.Wait();
		TestEqual(TEXT("nothing ran off the game thread"), Observer->ProgressReports + Observer->CancelCallbacks, 0);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

		TestEqual(TEXT("queued progress ran for the live context"), Observer->ProgressReports, 1);
		TestEqual(TEXT("the cancel callback ran once"), Observer->CancelCallbacks, 1);
		TestEqual(TEXT("the first cancel's reason is kept"), Context->GetCancelledMessage(), FString(TEXT("off-thread cancel")));

		// Progress after the result is dropped
		Context->MarkCompleted();
		Context->ReportProgress(0.9f, TEXT("late"));
		TestEqual(TEXT("progress after completion is dropped"), Observer->ProgressReports, 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
