// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Utils/N2CLogger.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CLoggerConcurrencyTest, "NodeToCode.Robustness.LoggerConcurrency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CLoggerConcurrencyTest::RunTest(const FString& Parameters)
{
	FN2CLogger& Logger = FN2CLogger::Get();
	Logger.ClearErrors();

	// 8 worker threads x 10,000 lines while the game thread logs too
	TArray<TFuture<void>> Workers;
	for (int32 Thread = 0; Thread < 8; ++Thread)
	{
		Workers.Add(Async(EAsyncExecution::Thread, [Thread]()
		{
			for (int32 i = 0; i < 10000; ++i)
			{
				FN2CLogger::Get().Log(FString::Printf(TEXT("concurrency probe %d/%d"), Thread, i), EN2CLogSeverity::Info);
			}
		}));
	}
	for (int32 i = 0; i < 2000; ++i)
	{
		Logger.Log(FString::Printf(TEXT("concurrency probe game thread %d"), i), EN2CLogSeverity::Info);
	}
	for (TFuture<void>& Worker : Workers)
	{
		Worker.Wait();
	}

	const int32 Retained = Logger.GetErrors().Num();
	TestTrue(FString::Printf(TEXT("retained %d records, cap 1000"), Retained), Retained > 0 && Retained <= 1000);
	Logger.ClearErrors();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
