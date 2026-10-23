// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMRequestHandle.h"
#include "Misc/ScopeLock.h"

TSharedRef<FN2CLLMRequestHandle, ESPMode::ThreadSafe> FN2CLLMRequestHandle::Create()
{
    return MakeShareable(new FN2CLLMRequestHandle());
}

bool FN2CLLMRequestHandle::Cancel()
{
    EState Expected = EState::Pending;
    if (!State.compare_exchange_strong(Expected, EState::Cancelled))
    {
        // Already cancelled (true), or completed / completing (false)
        return Expected == EState::Cancelled;
    }

    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> RequestToCancel;
    TArray<TFunction<void()>> Hooks;
    {
        FScopeLock ScopeLock(&Lock);
        RequestToCancel = HttpRequest.Pin();
        HttpRequest.Reset();
        Hooks = MoveTemp(CancelHooks);
        CancelHooks.Reset();
    }

    // The request's completion delegate still fires (as a failure); Complete() drops it because the state is Cancelled
    if (RequestToCancel.IsValid())
    {
        RequestToCancel->CancelRequest();
    }

    for (TFunction<void()>& Hook : Hooks)
    {
        if (Hook)
        {
            Hook();
        }
    }
    return true;
}

void FN2CLLMRequestHandle::OnCancelled(TFunction<void()> Hook)
{
    if (!Hook)
    {
        return;
    }
    {
        FScopeLock ScopeLock(&Lock);
        const EState Current = State.load();
        if (Current == EState::Pending)
        {
            CancelHooks.Add(MoveTemp(Hook));
            return;
        }
        if (Current != EState::Cancelled)
        {
            // Completed or completing: the request can no longer be cancelled
            return;
        }
    }
    Hook();
}

bool FN2CLLMRequestHandle::Complete(const FOnLLMResponseReceived& OnComplete, const FString& Response)
{
    EState Expected = EState::Pending;
    if (!State.compare_exchange_strong(Expected, EState::Completing))
    {
        // Cancelled, or a completion already ran: at most once, never after Cancel()
        return false;
    }

    {
        FScopeLock ScopeLock(&Lock);
        HttpRequest.Reset();
        CancelHooks.Reset();
    }

    OnComplete.ExecuteIfBound(Response);
    State.store(EState::Completed);
    return true;
}

FOnLLMResponseReceived FN2CLLMRequestHandle::Wrap(const FOnLLMResponseReceived& OnComplete)
{
    return FOnLLMResponseReceived::CreateLambda(
        [Handle = AsShared(), OnComplete](const FString& Response)
        {
            Handle->Complete(OnComplete, Response);
        });
}

void FN2CLLMRequestHandle::SetHttpRequest(const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request)
{
    FScopeLock ScopeLock(&Lock);
    if (State.load() == EState::Pending)
    {
        HttpRequest = Request;
    }
}
