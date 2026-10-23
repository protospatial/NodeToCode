// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolBase.h"
#include "Utils/N2CLogger.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"
#include <atomic>

namespace N2CMcpToolBasePrivate
{
	/** Lifecycle of one game-thread dispatch. Pending -> Running (game thread won) or Pending -> Abandoned (caller timed out). */
	enum class EGameThreadWorkState : int32
	{
		Pending,
		Running,
		Abandoned
	};

	/**
	 * State shared by the waiting caller and the queued game-thread task. Both hold a thread-safe reference, so it
	 * stays alive until whichever side finishes last; nothing in it points into the caller's stack frame.
	 */
	struct FGameThreadWork
	{
		TFunction<FMcpToolCallResult()> Logic;
		std::atomic<int32> State{ static_cast<int32>(EGameThreadWorkState::Pending) };
	};
}

FMcpToolCallResult FN2CMcpToolBase::ExecuteOnGameThread(TFunction<FMcpToolCallResult()> Logic, float TimeoutSeconds)
{
	using namespace N2CMcpToolBasePrivate;

	// Check if we're already on the Game Thread
	if (IsInGameThread())
	{
		FN2CLogger::Get().Log(TEXT("MCP Tool: Already on Game Thread, executing directly"), EN2CLogSeverity::Debug);
		return Logic();
	}

	// We're on a worker thread, need to dispatch to Game Thread
	FN2CLogger::Get().Log(TEXT("MCP Tool: On worker thread, dispatching to Game Thread"), EN2CLogSeverity::Debug);

	// The logic and its result live in shared state and a promise owned by the task, never on this stack frame,
	// so a task that runs after we stop waiting cannot write into a dead frame or call a destroyed TFunction.
	TSharedRef<FGameThreadWork, ESPMode::ThreadSafe> Work = MakeShared<FGameThreadWork, ESPMode::ThreadSafe>();
	Work->Logic = MoveTemp(Logic);

	TPromise<FMcpToolCallResult> Promise;
	TFuture<FMcpToolCallResult> Future = Promise.GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Work, Promise = MoveTemp(Promise)]() mutable
	{
		int32 Expected = static_cast<int32>(EGameThreadWorkState::Pending);
		if (!Work->State.compare_exchange_strong(Expected, static_cast<int32>(EGameThreadWorkState::Running)))
		{
			// The caller timed out and abandoned this work: skip the logic entirely
			FN2CLogger::Get().Log(TEXT("MCP Tool: Game Thread task skipped, the caller timed out and abandoned it"), EN2CLogSeverity::Debug);
			Work->Logic.Reset();
			Promise.SetValue(FMcpToolCallResult::CreateErrorResult(TEXT("Game Thread work was abandoned after a timeout.")));
			return;
		}

		FN2CLogger::Get().Log(TEXT("MCP Tool: Game Thread task executing"), EN2CLogSeverity::Debug);

		FMcpToolCallResult TaskResult = Work->Logic();
		Work->Logic.Reset();

		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP Tool: Game Thread task completed. Success: %s"),
			TaskResult.bIsError ? TEXT("No") : TEXT("Yes")), EN2CLogSeverity::Debug);

		Promise.SetValue(MoveTemp(TaskResult));
	});

	// Wait for completion with timeout
	if (Future.WaitFor(FTimespan::FromSeconds(FMath::Max(0.0f, TimeoutSeconds))))
	{
		return Future.Get();
	}

	// Timed out. Abandon the work so a late task skips the logic. If the game thread already started it, it runs to
	// completion and its result is dropped with the shared state; work that has started cannot be cancelled.
	int32 Expected = static_cast<int32>(EGameThreadWorkState::Pending);
	if (Work->State.compare_exchange_strong(Expected, static_cast<int32>(EGameThreadWorkState::Abandoned)))
	{
		FN2CLogger::Get().LogError(TEXT("MCP Tool timed out waiting for Game Thread; the queued work was abandoned and will not run"));
	}
	else
	{
		FN2CLogger::Get().LogError(TEXT("MCP Tool timed out waiting for Game Thread; the work had already started and its result will be discarded"));
	}
	return FMcpToolCallResult::CreateErrorResult(TEXT("Timeout waiting for Game Thread execution."));
}

TSharedPtr<FJsonObject> FN2CMcpToolBase::BuildInputSchema(
	const TMap<FString, FString>& Properties,
	const TArray<FString>& Required)
{
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	// Build properties object
	TSharedPtr<FJsonObject> PropertiesObj = MakeShareable(new FJsonObject);
	for (const auto& Prop : Properties)
	{
		TSharedPtr<FJsonObject> PropSchema = MakeShareable(new FJsonObject);
		PropSchema->SetStringField(TEXT("type"), Prop.Value);
		PropertiesObj->SetObjectField(Prop.Key, PropSchema);
	}
	Schema->SetObjectField(TEXT("properties"), PropertiesObj);
	
	// Add required array if needed
	if (Required.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RequiredArray;
		for (const FString& ReqProp : Required)
		{
			RequiredArray.Add(MakeShareable(new FJsonValueString(ReqProp)));
		}
		Schema->SetArrayField(TEXT("required"), RequiredArray);
	}
	
	return Schema;
}

TSharedPtr<FJsonObject> FN2CMcpToolBase::BuildEmptyObjectSchema()
{
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	// Empty properties object
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Empty required array
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	return Schema;
}

void FN2CMcpToolBase::AddReadOnlyAnnotation(FMcpToolDefinition& Definition)
{
	if (!Definition.Annotations.IsValid())
	{
		Definition.Annotations = MakeShareable(new FJsonObject);
	}
	Definition.Annotations->SetBoolField(TEXT("readOnlyHint"), true);
}