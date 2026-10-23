// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Async/N2CToolAsyncTaskBase.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "Containers/Ticker.h"
#include <atomic>

/**
 * translate-focused-blueprint as an async MCP task.
 *
 * Everything runs on the game thread: Execute() collects the Blueprint JSON, builds the request and sends it, then
 * returns at once. The LLM completion (an HTTP completion, also on the game thread) reports the result. An FTSTicker
 * reports progress and enforces the timeout. Cancel and timeout cancel the in-flight request through its
 * FN2CLLMRequestHandle, so no callback arrives after the task has reported.
 */
class FN2CTranslateBlueprintAsyncTask : public FN2CToolAsyncTaskBase, public TSharedFromThis<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe>
{
public:
    /** Starts the LLM request for the given provider, model and language overrides and returns its handle */
    using FRequestStarter = TFunction<FN2CLLMRequestHandleRef(const FString& /*ProviderId*/, const FString& /*ModelId*/,
        const FString& /*LanguageId*/, const FOnLLMResponseReceived& /*OnComplete*/)>;

    FN2CTranslateBlueprintAsyncTask(const FGuid& TaskId, const FString& ProgressToken, const TSharedPtr<FJsonObject>& Arguments);
    virtual ~FN2CTranslateBlueprintAsyncTask() override;

    /** Game thread only. Sends the request and returns; the result arrives later through the completion delegate. */
    virtual void Execute() override;

    /** Cancels the in-flight LLM request and reports the task as cancelled (on the game thread) */
    virtual void RequestCancel() override;

    /**
     * Progress and timeout step, run by the task's FTSTicker every ProgressIntervalSeconds. Returns true while the
     * task is still waiting for the LLM. Public so tests can drive it without ticking the core ticker.
     */
    bool Tick(float DeltaTime);

    /** Replaces the default request (translate the focused Blueprint). For tests that cannot focus a Blueprint headless. */
    void SetRequestStarter(FRequestStarter InStarter) { RequestStarter = MoveTemp(InStarter); }

    /** Overrides the 1-hour timeout (tests) */
    void SetTimeoutSeconds(double InSeconds) { TimeoutSeconds = InSeconds; }

    /** The in-flight request's handle, or null before Execute() */
    FN2CLLMRequestHandlePtr GetRequestHandle() const { return RequestHandle; }

    /** Default time a translation may take before the task cancels it */
    static constexpr double DefaultTimeoutSeconds = 3600.0;

    /** How often the ticker reports progress and checks the timeout */
    static constexpr float ProgressIntervalSeconds = 0.5f;

private:
    /** State shared with the LLM callback, captured by value so a late callback never touches a dead task */
    struct FCallbackState
    {
        /** Set once the LLM callback has delivered a result */
        std::atomic<bool> bDone{false};
        /** Set once the task has given up on the request (cancel or timeout) */
        std::atomic<bool> bCancelled{false};
    };

    /** Reports the LLM response as the task's result (game thread) */
    void HandleLLMResponse(const FString& LLMResponse);

    /** Cancels the request and reports Message as the task's error result, once */
    void AbortRequest(const FString& Message);

    /** Removes the progress ticker */
    void StopTicker();

    FRequestStarter RequestStarter;
    TSharedRef<FCallbackState, ESPMode::ThreadSafe> CallbackState;
    FN2CLLMRequestHandlePtr RequestHandle;
    FTSTicker::FDelegateHandle TickerHandle;

    double TimeoutSeconds = DefaultTimeoutSeconds;
    double StartTime = 0.0;
    float WaitProgress = 0.1f;
};
