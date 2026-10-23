// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IHttpRequest.h"
#include "LLM/N2CLLMTypes.h"
#include "Templates/SharedPointer.h"
#include <atomic>

/**
 * @class FN2CLLMRequestHandle
 * @brief Handle to one LLM request, returned by PostLLMRequest, SendRequest, ProcessN2CJson and
 * ProcessN2CJsonWithOverrides.
 *
 * Contract:
 * - The request's OnComplete runs at most once.
 * - Cancel() cancels the HTTP request (IHttpRequest::CancelRequest) and marks the handle cancelled. Once Cancel()
 *   returns true, OnComplete never runs. HTTP completions run on the game thread, so a Cancel() called on the game
 *   thread always wins unless the request already completed.
 * - Cancel() from another thread returns false if the completion is already running on the game thread; the caller
 *   must then treat the request as completed (its callback may still be executing).
 * - Callers may ignore the handle: the request still runs and completes as before.
 *
 * Layering: one handle travels up from the HTTP handler through the service and the LLM module, so cancelling at
 * any level stops the whole chain. A higher layer (a subsystem request id, a batch, an MCP task) holds the handle
 * and adds its own state through OnCancelled().
 */
class NODETOCODE_API FN2CLLMRequestHandle : public TSharedFromThis<FN2CLLMRequestHandle, ESPMode::ThreadSafe>
{
public:
    enum class EState : uint8
    {
        /** Request in flight (or not yet sent) */
        Pending,
        /** The completion won the race and is running OnComplete */
        Completing,
        /** OnComplete has returned */
        Completed,
        /** Cancel() won the race: OnComplete will never run */
        Cancelled
    };

    /** A new pending handle */
    static TSharedRef<FN2CLLMRequestHandle, ESPMode::ThreadSafe> Create();

    /**
     * Cancels the request. Returns true if this call (or an earlier Cancel) stopped the callback, false if the request
     * had already completed or its completion is running. Cancel hooks run once, on the thread that cancelled.
     */
    bool Cancel();

    /** True once Cancel() has won */
    bool IsCancelled() const { return State.load() == EState::Cancelled; }

    /** True once OnComplete has returned */
    bool IsCompleted() const { return State.load() == EState::Completed; }

    /** True when the request will not deliver anything more (completed or cancelled) */
    bool IsFinished() const
    {
        const EState Current = State.load();
        return Current == EState::Completed || Current == EState::Cancelled;
    }

    EState GetState() const { return State.load(); }

    /**
     * Registers a function to run when Cancel() wins (for example to reset a status flag). Runs at once if the handle
     * is already cancelled; never runs if the request completes.
     */
    void OnCancelled(TFunction<void()> Hook);

    /**
     * Runs OnComplete with Response unless the handle was cancelled or already completed. Returns true if it ran.
     * This is the only way the LLM layer delivers a result.
     */
    bool Complete(const FOnLLMResponseReceived& OnComplete, const FString& Response);

    /** Wraps OnComplete so it runs through Complete() on this handle */
    FOnLLMResponseReceived Wrap(const FOnLLMResponseReceived& OnComplete);

    /** Attaches the HTTP request Cancel() should cancel (held weakly: the HTTP manager owns it while in flight) */
    void SetHttpRequest(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request);

private:
    FN2CLLMRequestHandle() = default;

    std::atomic<EState> State{EState::Pending};

    /** Guards HttpRequest and CancelHooks */
    mutable FCriticalSection Lock;
    TWeakPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
    TArray<TFunction<void()>> CancelHooks;
};

using FN2CLLMRequestHandleRef = TSharedRef<FN2CLLMRequestHandle, ESPMode::ThreadSafe>;
using FN2CLLMRequestHandlePtr = TSharedPtr<FN2CLLMRequestHandle, ESPMode::ThreadSafe>;
