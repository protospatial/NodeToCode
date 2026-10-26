// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Python/N2CPythonTranslation.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpTranslateBatchTool.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"

namespace N2CPythonTranslationPrivate
{
	FString ResultText(const FMcpToolCallResult& Result)
	{
		FString Text;
		for (const TSharedPtr<FJsonObject>& Item : Result.Content)
		{
			FString Part;
			if (Item.IsValid() && Item->TryGetStringField(TEXT("text"), Part))
			{
				Text += Part;
			}
		}
		return Text;
	}

	/** The translate-batch tool's arguments */
	TSharedRef<FJsonObject> MakeArguments(const TArray<FString>& BlueprintPaths, const TArray<FString>& GraphNames,
		const FString& Tag, const FString& Category, const FString& Language, const FString& Provider, const FString& Model)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		if (BlueprintPaths.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Targets;
			for (int32 Index = 0; Index < BlueprintPaths.Num(); ++Index)
			{
				const TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
				Target->SetStringField(TEXT("blueprintPath"), BlueprintPaths[Index].TrimStartAndEnd());
				if (GraphNames.IsValidIndex(Index) && !GraphNames[Index].TrimStartAndEnd().IsEmpty())
				{
					Target->SetStringField(TEXT("graphName"), GraphNames[Index].TrimStartAndEnd());
				}
				Targets.Add(MakeShared<FJsonValueObject>(Target));
			}
			Args->SetArrayField(TEXT("targets"), Targets);
		}
		const auto SetIfAny = [&Args](const TCHAR* Name, const FString& Value)
		{
			if (!Value.TrimStartAndEnd().IsEmpty())
			{
				Args->SetStringField(Name, Value.TrimStartAndEnd());
			}
		};
		SetIfAny(TEXT("tag"), Tag);
		SetIfAny(TEXT("category"), Category);
		SetIfAny(TEXT("language"), Language);
		SetIfAny(TEXT("provider"), Provider);
		SetIfAny(TEXT("model"), Model);
		return Args;
	}
}

UN2CPythonTranslation* UN2CPythonTranslation::GetInstance()
{
	static TWeakObjectPtr<UN2CPythonTranslation> Instance;
	if (!Instance.IsValid())
	{
		UN2CPythonTranslation* Created = NewObject<UN2CPythonTranslation>(GetTransientPackage(), NAME_None, RF_Transient);
		Created->AddToRoot();
		Instance = Created;
	}
	return Instance.Get();
}

void UN2CPythonTranslation::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	// A multicast delegate holds its listeners weakly; Python's add_callable listener has no other owner
	for (UObject* Listener : CastChecked<UN2CPythonTranslation>(InThis)->OnBatchFinished.GetAllObjects())
	{
		TObjectPtr<UObject> Reference = Listener;
		Collector.AddReferencedObject(Reference);
	}
}

FString UN2CPythonTranslation::StartBatch(const TArray<FString>& BlueprintPaths, const TArray<FString>& GraphNames,
	const FString& Tag, const FString& Category, const FString& Language, const FString& Provider, const FString& Model,
	FString& OutError)
{
	using namespace N2CPythonTranslationPrivate;
	check(IsInGameThread());
	OutError.Reset();

	UN2CPythonTranslation* Self = GetInstance();
	const TSharedRef<FBatch> Batch = MakeShared<FBatch>();
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Batch->Status.BatchId = Id;
	Batch->Status.State = TEXT("running");
	Batch->Status.Message = TEXT("Starting");
	Batch->StartSeconds = FPlatformTime::Seconds();

	// Progress arrives on the game thread; the batch may be dropped from the table (Prune) while its call runs
	const TWeakPtr<FBatch> WeakBatch = Batch;
	Batch->Context = FN2CToolCallContext::Create([WeakBatch](float Progress, const FString& Message)
	{
		if (const TSharedPtr<FBatch> Pinned = WeakBatch.Pin())
		{
			if (!Pinned->bFinished)
			{
				Pinned->Status.Progress = FMath::Clamp(Progress, Pinned->Status.Progress, 1.0f);
				Pinned->Status.Message = Message;
			}
		}
	});

	// In the table before the call starts: a batch can end inside the start
	Self->Batches.Add(Id, Batch);
	Self->Order.Add(Id);

	FN2CMcpTranslateBatchTool Tool;
	const TSharedRef<FJsonObject> Args = MakeArguments(BlueprintPaths, GraphNames, Tag, Category, Language, Provider, Model);
	TFuture<FMcpToolCallResult> Future = Tool.InvokeAsync(Batch->Context.ToSharedRef(), Args);
	const bool bEndedAtStart = Future.IsReady();

	// The call sets its result on the game thread; Then runs at once when it is already set
	const TWeakObjectPtr<UN2CPythonTranslation> WeakSelf = Self;
	Future.Then([WeakSelf, Batch](TFuture<FMcpToolCallResult> Done)
	{
		const FMcpToolCallResult Result = Done.Get();
		if (UN2CPythonTranslation* Owner = WeakSelf.Get())
		{
			Owner->Finish(Batch, Result);
		}
	});

	if (bEndedAtStart && Batch->Status.State != TEXT("succeeded"))
	{
		// Refused before a request was sent (bad arguments, nothing to translate, a batch already running)
		OutError = Batch->Status.Result.IsEmpty() ? FString(TEXT("The batch did not start")) : Batch->Status.Result;
		Self->Batches.Remove(Id);
		Self->Order.Remove(Id);
		return FString();
	}

	Self->Prune();
	FN2CLogger::Get().Log(FString::Printf(TEXT("NodeToCodeTools: batch %s started"), *Id), EN2CLogSeverity::Info);
	return Id;
}

void UN2CPythonTranslation::Finish(const TSharedRef<FBatch>& Batch, const FMcpToolCallResult& Result)
{
	check(IsInGameThread());
	if (Batch->bFinished)
	{
		return;
	}
	Batch->bFinished = true;
	FN2CPythonBatchStatus& Status = Batch->Status;
	Status.Result = N2CPythonTranslationPrivate::ResultText(Result);
	Status.State = Batch->Context->IsCancelled() ? TEXT("cancelled") : (Result.bIsError ? TEXT("failed") : TEXT("succeeded"));
	if (Status.State == TEXT("succeeded"))
	{
		Status.Progress = 1.0f;
	}
	Status.Message = Status.State == TEXT("cancelled") ? Batch->Context->GetCancelledMessage()
		: Status.State == TEXT("succeeded") ? FString(TEXT("Batch translation complete")) : FString(TEXT("Batch translation failed"));
	Status.ElapsedSeconds = static_cast<float>(FPlatformTime::Seconds() - Batch->StartSeconds);
	Batch->Context->MarkCompleted();

	FN2CLogger::Get().Log(FString::Printf(TEXT("NodeToCodeTools: batch %s %s after %.1f s"), *Status.BatchId, *Status.State,
		Status.ElapsedSeconds), EN2CLogSeverity::Info);
	OnBatchFinished.Broadcast(Status.BatchId);
}

TSharedPtr<UN2CPythonTranslation::FBatch> UN2CPythonTranslation::Find(const FString& BatchId, bool bRunningOnly) const
{
	const FString Id = BatchId.TrimStartAndEnd();
	if (!Id.IsEmpty())
	{
		const TSharedRef<FBatch>* Found = Batches.Find(Id.ToLower());
		return Found ? TSharedPtr<FBatch>(*Found) : nullptr;
	}
	for (int32 Index = Order.Num() - 1; Index >= 0; --Index)
	{
		const TSharedRef<FBatch>& Batch = Batches.FindChecked(Order[Index]);
		if (!Batch->bFinished)
		{
			return Batch;
		}
	}
	if (bRunningOnly || Order.Num() == 0)
	{
		return nullptr;
	}
	return Batches.FindChecked(Order.Last());
}

FN2CPythonBatchStatus UN2CPythonTranslation::GetBatchStatus(const FString& BatchId)
{
	check(IsInGameThread());
	const TSharedPtr<FBatch> Batch = GetInstance()->Find(BatchId, /*bRunningOnly*/ false);
	if (!Batch.IsValid())
	{
		return FN2CPythonBatchStatus();
	}
	FN2CPythonBatchStatus Status = Batch->Status;
	if (!Batch->bFinished)
	{
		Status.ElapsedSeconds = static_cast<float>(FPlatformTime::Seconds() - Batch->StartSeconds);
	}
	return Status;
}

bool UN2CPythonTranslation::CancelBatch(const FString& BatchId)
{
	check(IsInGameThread());
	const TSharedPtr<FBatch> Batch = GetInstance()->Find(BatchId, /*bRunningOnly*/ true);
	if (!Batch.IsValid())
	{
		return false;
	}
	if (!Batch->bFinished)
	{
		// The call's cancel callback cancels the orchestrator's batch; its result then ends this batch (Finish)
		Batch->Context->Cancel(CancelMessage());
	}
	return true;
}

void UN2CPythonTranslation::Prune()
{
	for (int32 Index = 0; Index < Order.Num() && Order.Num() > MaxKeptBatches;)
	{
		if (Batches.FindChecked(Order[Index])->bFinished)
		{
			Batches.Remove(Order[Index]);
			Order.RemoveAt(Index);
		}
		else
		{
			++Index;
		}
	}
}
