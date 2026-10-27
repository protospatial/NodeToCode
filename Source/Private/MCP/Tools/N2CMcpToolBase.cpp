// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolBase.h"
#include "Utils/N2CLogger.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
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

	/**
	 * Runs a tool's game-thread logic as one tool call (card 0151): the Blueprints its edits mark modified compile once,
	 * here, after the logic has returned and its transactions have closed, and the compile diagnostics are added to
	 * the result.
	 */
	FMcpToolCallResult RunAsToolCall(const TFunction<FMcpToolCallResult()>& Logic)
	{
		FN2CMcpToolCallScope CallScope;
		FMcpToolCallResult Result = Logic();
		FN2CMcpBlueprintUtils::AttachCompileDiagnostics(Result, CallScope.Flush());
		return Result;
	}
}

FMcpToolCallResult FN2CMcpToolBase::ExecuteOnGameThread(TFunction<FMcpToolCallResult()> Logic, float TimeoutSeconds)
{
	using namespace N2CMcpToolBasePrivate;

	// Check if we're already on the Game Thread
	if (IsInGameThread())
	{
		FN2CLogger::Get().Log(TEXT("MCP Tool: Already on Game Thread, executing directly"), EN2CLogSeverity::Debug);
		return RunAsToolCall(Logic);
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

		FMcpToolCallResult TaskResult = RunAsToolCall(Work->Logic);
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
	const TArray<FString>& Required,
	const TMap<FString, FString>& Descriptions)
{
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	// Build properties object
	TSharedPtr<FJsonObject> PropertiesObj = MakeShareable(new FJsonObject);
	for (const auto& Prop : Properties)
	{
		TSharedPtr<FJsonObject> PropSchema = MakeShareable(new FJsonObject);
		PropSchema->SetStringField(TEXT("type"), Prop.Value);
		if (const FString* Description = Descriptions.Find(Prop.Key))
		{
			if (!Description->IsEmpty())
			{
				PropSchema->SetStringField(TEXT("description"), *Description);
			}
		}
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

	// A tool without arguments says so: a client then sends none
	if (Properties.Num() == 0)
	{
		Schema->SetBoolField(TEXT("additionalProperties"), false);
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

	// No arguments at all
	Schema->SetBoolField(TEXT("additionalProperties"), false);
	
	return Schema;
}

void FN2CMcpToolBase::SetStrictArguments(FMcpToolDefinition& Definition)
{
	Definition.bStrictArguments = true;
	if (Definition.InputSchema.IsValid())
	{
		Definition.InputSchema->SetBoolField(TEXT("additionalProperties"), false);
	}
}

void FN2CMcpToolBase::SetAnnotations(FMcpToolDefinition& Definition, bool bReadOnly, bool bDestructive, bool bIdempotent, bool bOpenWorld)
{
	Definition.Annotations = FMcpToolAnnotations(bReadOnly, bDestructive, bIdempotent, bOpenWorld);
}

void FN2CMcpToolBase::SetAnnotations(FMcpToolDefinition& Definition, const FMcpToolAnnotations& Annotations)
{
	Definition.Annotations = Annotations;
}
