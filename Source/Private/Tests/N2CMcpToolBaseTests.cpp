// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Tools/N2CMcpToolBase.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include <atomic>

namespace N2CMcpToolBaseTestsPrivate
{
	/** Minimal tool that exposes the protected helper to the test. */
	class FGameThreadProbeTool : public FN2CMcpToolBase
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override { return FMcpToolDefinition(); }
		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			return FMcpToolCallResult::CreateTextResult(TEXT("unused"));
		}

		using FN2CMcpToolBase::ExecuteOnGameThread;
	};

	/** Runs queued game-thread tasks until Done() is true or the deadline passes. */
	template <typename PredicateType>
	bool PumpGameThreadUntil(PredicateType Done, double TimeoutSeconds)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (!Done())
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				return false;
			}
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.001f);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CExecuteOnGameThreadTimeoutTest, "NodeToCode.Robustness.ExecuteOnGameThreadTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CExecuteOnGameThreadTimeoutTest::RunTest(const FString& Parameters)
{
	using namespace N2CMcpToolBaseTestsPrivate;

	if (!TestTrue(TEXT("test runs on the game thread"), IsInGameThread()))
	{
		return false;
	}

	FGameThreadProbeTool Tool;

	// 1. Normal path: a worker call completes once the game thread runs the task
	{
		TFuture<FMcpToolCallResult> Worker = Async(EAsyncExecution::Thread, [&Tool]()
		{
			return Tool.ExecuteOnGameThread([]()
			{
				return FMcpToolCallResult::CreateTextResult(IsInGameThread() ? TEXT("game thread") : TEXT("wrong thread"));
			}, 10.0f);
		});

		const bool bFinished = PumpGameThreadUntil([&Worker]() { return Worker.IsReady(); }, 10.0);
		if (!TestTrue(TEXT("worker call finished while the game thread pumped tasks"), bFinished))
		{
			Worker.Wait();
			return false;
		}
		const FMcpToolCallResult& Result = Worker.Get();
		TestFalse(TEXT("normal call is not an error"), Result.bIsError);
		const FString Text = Result.Content.Num() > 0 && Result.Content[0].IsValid() ? Result.Content[0]->GetStringField(TEXT("text")) : FString();
		TestEqual(TEXT("logic ran on the game thread"), Text, FString(TEXT("game thread")));
	}

	// 2. Timeout path: the game thread is busy for 0.5 s while the worker waits only 0.1 s
	{
		AddExpectedError(TEXT("MCP Tool timed out waiting for Game Thread"), EAutomationExpectedErrorFlags::Contains, 1);

		TSharedRef<std::atomic<bool>, ESPMode::ThreadSafe> bLateLogicRan = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);

		TFuture<FMcpToolCallResult> Worker = Async(EAsyncExecution::Thread, [&Tool, bLateLogicRan]()
		{
			return Tool.ExecuteOnGameThread([bLateLogicRan]()
			{
				bLateLogicRan->store(true);
				return FMcpToolCallResult::CreateTextResult(TEXT("late logic"));
			}, 0.1f);
		});

		// Keep the game thread busy so the queued task cannot run before the worker gives up
		FPlatformProcess::Sleep(0.5f);

		// The worker's frame (and with the old helper, the Result and Logic it captured by reference) is gone after this
		const FMcpToolCallResult Result = Worker.Get();
		TestTrue(TEXT("timed-out call returns an error result"), Result.bIsError);

		// Now let the game thread run the stale task
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.05f);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

		TestFalse(TEXT("abandoned logic did not run after the timeout"), bLateLogicRan->load());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
