// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CToolCallContext.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"

FN2CToolCallContext::FN2CToolCallContext(FN2CToolProgressSink InProgressSink)
	: ProgressSink(MoveTemp(InProgressSink))
{
}

FN2CToolCallContextRef FN2CToolCallContext::Create(FN2CToolProgressSink ProgressSink)
{
	return MakeShareable(new FN2CToolCallContext(MoveTemp(ProgressSink)));
}

void FN2CToolCallContext::ReportProgress(float Progress, const FString& Message)
{
	if (!ProgressSink || bCompleted.load())
	{
		return;
	}

	if (IsInGameThread())
	{
		ProgressSink(Progress, Message);
		return;
	}

	// Queued for the game thread: hold the context weakly, and drop the report if the call completed meanwhile
	const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = AsWeak();
	AsyncTask(ENamedThreads::GameThread, [WeakContext, Progress, Message]()
	{
		if (const FN2CToolCallContextPtr Context = WeakContext.Pin())
		{
			if (!Context->bCompleted.load() && Context->ProgressSink)
			{
				Context->ProgressSink(Progress, Message);
			}
		}
	});
}

FString FN2CToolCallContext::GetCancelReason() const
{
	FScopeLock ScopeLock(&Lock);
	return CancelReason;
}

FString FN2CToolCallContext::GetCancelledMessage() const
{
	const FString Reason = GetCancelReason();
	return Reason.IsEmpty() ? FString(DefaultCancelledMessage()) : Reason;
}

void FN2CToolCallContext::OnCancelled(TFunction<void()> Callback)
{
	if (!Callback || bCompleted.load())
	{
		return;
	}

	{
		FScopeLock ScopeLock(&Lock);
		if (!bCancelled.load())
		{
			CancelCallbacks.Add(MoveTemp(Callback));
			return;
		}
	}

	// Already cancelled: run it now, on the game thread
	if (IsInGameThread())
	{
		Callback();
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, MoveTemp(Callback));
	}
}

bool FN2CToolCallContext::Cancel(const FString& Reason)
{
	if (bCompleted.load())
	{
		return false;
	}

	{
		FScopeLock ScopeLock(&Lock);
		if (bCancelled.exchange(true))
		{
			return false;
		}
		CancelReason = Reason;
	}

	if (IsInGameThread())
	{
		RunCancelCallbacks();
	}
	else
	{
		// The context stays alive until its callbacks have run
		const FN2CToolCallContextRef Self = AsShared();
		AsyncTask(ENamedThreads::GameThread, [Self]()
		{
			Self->RunCancelCallbacks();
		});
	}
	return true;
}

void FN2CToolCallContext::RunCancelCallbacks()
{
	check(IsInGameThread());

	TArray<TFunction<void()>> Callbacks;
	{
		FScopeLock ScopeLock(&Lock);
		Callbacks = MoveTemp(CancelCallbacks);
		CancelCallbacks.Reset();
	}
	for (TFunction<void()>& Callback : Callbacks)
	{
		Callback();
	}
}

void FN2CToolCallContext::MarkCompleted()
{
	bCompleted.store(true);

	// Release the callbacks (and whatever they hold) outside the lock
	TArray<TFunction<void()>> Released;
	{
		FScopeLock ScopeLock(&Lock);
		Released = MoveTemp(CancelCallbacks);
		CancelCallbacks.Reset();
	}
}
