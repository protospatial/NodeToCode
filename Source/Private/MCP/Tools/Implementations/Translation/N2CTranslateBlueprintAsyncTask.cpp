// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CTranslateBlueprintAsyncTask.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"

FN2CTranslateBlueprintAsyncTask::FN2CTranslateBlueprintAsyncTask(const FGuid& TaskId, const FString& ProgressToken, const TSharedPtr<FJsonObject>& Arguments)
    : FN2CToolAsyncTaskBase(TaskId, ProgressToken, TEXT("translate-focused-blueprint"), Arguments)
    , CallbackState(MakeShared<FCallbackState, ESPMode::ThreadSafe>())
{
}

FN2CTranslateBlueprintAsyncTask::~FN2CTranslateBlueprintAsyncTask()
{
    StopTicker();
}

void FN2CTranslateBlueprintAsyncTask::Execute()
{
    // Collecting the graph, creating the provider service and sending the request create UObjects and touch editor
    // state: all of it belongs on the game thread. The HTTP completion arrives on the game thread too.
    check(IsInGameThread());

    if (CallbackState->bDone.load())
    {
        // Cancelled before it started; the cancel already reported
        return;
    }
    if (CheckCancellationAndReport())
    {
        CallbackState->bDone.store(true);
        return;
    }
    ReportProgress(0.05f, TEXT("Preparing Blueprint data..."));

    FN2CMcpArgumentParser ArgParser(Arguments);
    const FString ProviderId = ArgParser.GetOptionalString(TEXT("provider"));
    const FString ModelId = ArgParser.GetOptionalString(TEXT("model"));
    const FString LanguageId = ArgParser.GetOptionalString(TEXT("language"));

    StartTime = FPlatformTime::Seconds();
    WaitProgress = 0.1f;
    ReportProgress(WaitProgress, TEXT("Sending translation request to LLM..."));

    // The callback holds the shared state by value and the task weakly: a callback that arrives after the task gave up
    // or was destroyed touches nothing but the shared state.
    const TWeakPtr<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe> WeakTask = SharedThis(this);
    const TSharedRef<FCallbackState, ESPMode::ThreadSafe> State = CallbackState;
    FOnLLMResponseReceived OnLLMResponse = FOnLLMResponseReceived::CreateLambda(
        [WeakTask, State](const FString& LLMResponse)
        {
            if (State->bCancelled.load() || State->bDone.exchange(true))
            {
                return;
            }
            if (const TSharedPtr<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe> Task = WeakTask.Pin())
            {
                Task->HandleLLMResponse(LLMResponse);
            }
        });

    if (RequestStarter)
    {
        RequestHandle = RequestStarter(ProviderId, ModelId, LanguageId, OnLLMResponse);
    }
    else
    {
        RequestHandle = FN2CEditorIntegration::Get().TranslateFocusedBlueprintAsync(ProviderId, ModelId, LanguageId, OnLLMResponse);
    }

    if (CallbackState->bDone.load())
    {
        // An error found before sending completed the request inside the call
        return;
    }

    // Progress and timeout from the core ticker; nothing waits on a thread
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [WeakTask](float DeltaTime)
        {
            const TSharedPtr<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe> Task = WeakTask.Pin();
            return Task.IsValid() && Task->Tick(DeltaTime);
        }), ProgressIntervalSeconds);
}

void FN2CTranslateBlueprintAsyncTask::RequestCancel()
{
    FN2CToolAsyncTaskBase::RequestCancel();

    if (IsInGameThread())
    {
        AbortRequest(TEXT("Task was cancelled"));
        return;
    }

    // The request handle and the completion live on the game thread
    const TWeakPtr<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe> WeakTask = SharedThis(this);
    AsyncTask(ENamedThreads::GameThread, [WeakTask]()
    {
        if (const TSharedPtr<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe> Task = WeakTask.Pin())
        {
            Task->AbortRequest(TEXT("Task was cancelled"));
        }
    });
}

bool FN2CTranslateBlueprintAsyncTask::Tick(float DeltaTime)
{
    check(IsInGameThread());

    if (CallbackState->bDone.load())
    {
        TickerHandle.Reset();
        return false;
    }

    if (IsCancellationRequested())
    {
        AbortRequest(TEXT("Task was cancelled"));
        return false;
    }

    const double Elapsed = FPlatformTime::Seconds() - StartTime;
    if (Elapsed > TimeoutSeconds)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Translate task %s timed out after %.1f s; cancelling the LLM request"),
            *GetTaskId().ToString(), Elapsed));
        AbortRequest(TEXT("LLM request timed out."));
        return false;
    }

    // Progress creeps from 0.1 towards 0.95 over the timeout so clients see the task is alive
    const double Cycles = FMath::Max(TimeoutSeconds / ProgressIntervalSeconds, 1.0);
    const float IncrementPerCycle = static_cast<float>(0.85 / Cycles);
    WaitProgress = FMath::Min(WaitProgress + IncrementPerCycle, 0.95f);
    ReportProgress(WaitProgress, TEXT("Waiting for LLM response..."));
    return true;
}

void FN2CTranslateBlueprintAsyncTask::HandleLLMResponse(const FString& LLMResponse)
{
    StopTicker();

    const FMcpToolCallResult Result = LLMResponse.StartsWith(TEXT("{\"error\""))
        ? FMcpToolCallResult::CreateErrorResult(LLMResponse)
        : FMcpToolCallResult::CreateTextResult(LLMResponse);

    ReportProgress(1.0f, TEXT("Translation received."));
    ReportComplete(Result);
}

void FN2CTranslateBlueprintAsyncTask::AbortRequest(const FString& Message)
{
    // Whoever sets bDone first reports: the LLM callback or this abort, never both
    if (CallbackState->bDone.exchange(true))
    {
        return;
    }
    CallbackState->bCancelled.store(true);
    StopTicker();

    if (RequestHandle.IsValid() && !RequestHandle->Cancel())
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Translate task %s: the LLM request had already completed when it was cancelled"),
            *GetTaskId().ToString()));
    }

    ReportComplete(FMcpToolCallResult::CreateErrorResult(Message));
}

void FN2CTranslateBlueprintAsyncTask::StopTicker()
{
    if (TickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
}
