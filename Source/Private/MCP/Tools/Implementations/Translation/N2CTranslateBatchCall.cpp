// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CTranslateBatchCall.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CTranslateBatchCallPrivate
{
	FString ToText(const TSharedRef<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Json, Writer);
		return Text;
	}

	const TCHAR* StatusId(EN2CBatchItemStatus Status)
	{
		switch (Status)
		{
		case EN2CBatchItemStatus::Completed: return TEXT("completed");
		case EN2CBatchItemStatus::Failed: return TEXT("failed");
		case EN2CBatchItemStatus::Skipped: return TEXT("skipped");
		case EN2CBatchItemStatus::Processing: return TEXT("processing");
		default: return TEXT("pending");
		}
	}

	TSharedRef<FJsonObject> GraphJson(const FN2CGraphTranslation& Graph)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("graph_name"), Graph.GraphName);
		Json->SetStringField(TEXT("graph_type"), Graph.GraphType);
		Json->SetStringField(TEXT("graph_class"), Graph.GraphClass);
		const TSharedRef<FJsonObject> Code = MakeShared<FJsonObject>();
		Code->SetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
		Code->SetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
		Code->SetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
		Json->SetObjectField(TEXT("code"), Code);
		return Json;
	}
}

FN2CTranslateBatchCall::FN2CTranslateBatchCall(const FN2CToolCallContextRef& InContext, FSetup&& InSetup)
	: Context(InContext)
	, Setup(MoveTemp(InSetup))
{
}

FN2CTranslateBatchCall::~FN2CTranslateBatchCall()
{
	StopTicker();
	if (!bFinished.exchange(true))
	{
		// Only reachable for a call that was never started: a started call holds itself until its result is set
		Promise.SetValue(FMcpToolCallResult::CreateErrorResult(TEXT("The batch translation call was dropped before it started")));
	}
}

TFuture<FMcpToolCallResult> FN2CTranslateBatchCall::Start()
{
	// The batch loads Blueprints, collects graphs and sends requests: game thread, like its HTTP completions
	check(IsInGameThread());
	check(!bStarted);
	bStarted = true;

	TFuture<FMcpToolCallResult> Future = Promise.GetFuture();
	SelfWhileRunning = AsShared();

	if (Context->IsCancelled())
	{
		Finish(FMcpToolCallResult::CreateErrorResult(Context->GetCancelledMessage()));
		return Future;
	}

	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
	const TWeakPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> WeakCall = AsShared();

	// The batch this call starts gets the next serial. Subscribe first: a batch whose every item fails before a request
	// is sent completes inside StartBatchTranslationWithOptions. Events of any other batch are ignored.
	Serial = Orchestrator.GetBatchSerial() + 1;
	ProgressHandle = Orchestrator.OnProgressNative.AddLambda([WeakCall](int32 Index, int32 Total, const FString& GraphName)
	{
		if (const TSharedPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> Call = WeakCall.Pin())
		{
			if (UN2CBatchTranslationOrchestrator::Get().GetBatchSerial() == Call->Serial)
			{
				Call->HandleProgress(Index, Total, GraphName);
			}
		}
	});
	CompleteHandle = Orchestrator.OnBatchCompleteNative.AddLambda([WeakCall](const FN2CBatchTranslationResult& Result)
	{
		if (const TSharedPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> Call = WeakCall.Pin())
		{
			if (UN2CBatchTranslationOrchestrator::Get().GetBatchSerial() == Call->Serial)
			{
				Call->HandleBatchComplete(Result);
			}
		}
	});

	// Cancelling the context cancels the batch. The context may outlive the call: hold it weakly.
	Context->OnCancelled([WeakCall]()
	{
		if (const TSharedPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> Call = WeakCall.Pin())
		{
			Call->AbortBatch(Call->Context->GetCancelledMessage());
		}
	});

	StartTime = FPlatformTime::Seconds();
	ItemStartTime = StartTime;
	Report(0.01f, FString::Printf(TEXT("Starting a batch of %d graphs"), Setup.TagInfos.Num()));

	FString StartError;
	if (!Orchestrator.StartBatchTranslationWithOptions(Setup.TagInfos, Setup.Options, StartError))
	{
		if (!bFinished.load())
		{
			const TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
			Error->SetStringField(TEXT("error"), StartError);
			Error->SetStringField(TEXT("code"), StartError.Left(StartError.Find(TEXT(":"))));
			Error->SetArrayField(TEXT("failures"), Setup.TargetFailures);
			Finish(FMcpToolCallResult::CreateTextResult(N2CTranslateBatchCallPrivate::ToText(Error), /*bInIsError*/ true));
		}
		return Future;
	}
	if (bFinished.load())
	{
		// Every item ended before a request was sent: the batch completed inside the start
		return Future;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakCall](float DeltaTime)
	{
		const TSharedPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> Call = WeakCall.Pin();
		return Call.IsValid() && Call->Tick(DeltaTime);
	}), ProgressIntervalSeconds);

	return Future;
}

bool FN2CTranslateBatchCall::IsOurBatchRunning() const
{
	const UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
	return Orchestrator.IsBatchInProgress() && Orchestrator.GetBatchSerial() == Serial;
}

bool FN2CTranslateBatchCall::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (bFinished.load())
	{
		TickerHandle.Reset();
		return false;
	}

	if (Context->IsCancelled() && !bAborting)
	{
		AbortBatch(Context->GetCancelledMessage());
		return !bFinished.load();
	}

	const double Now = FPlatformTime::Seconds();
	if (!bAborting && Now - StartTime > TimeoutSeconds)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("translate-batch timed out after %.1f s; cancelling the batch"), Now - StartTime));
		AbortBatch(TEXT("Batch translation timed out."));
		return !bFinished.load();
	}

	if (!IsOurBatchRunning())
	{
		// The batch ended without its completion reaching this call (cancelled while an item's result was handled)
		Finish(FMcpToolCallResult::CreateErrorResult(bAborting ? AbortMessage : FString(TEXT("The batch translation ended without a result"))));
		return false;
	}

	// A heartbeat while an item's request is out: rises within the item's share, never reaching the next item's
	const int32 Total = FMath::Max(1, Setup.TagInfos.Num());
	const double Waited = Now - ItemStartTime;
	const float Slot = 0.96f / static_cast<float>(Total);
	const float Base = 0.02f + Slot * static_cast<float>(CurrentIndex);
	Report(Base + Slot * 0.9f * static_cast<float>(Waited / (Waited + 60.0)),
		FString::Printf(TEXT("Translating %s (%d of %d, %d s)"), *CurrentGraphName, CurrentIndex + 1, Total, FMath::FloorToInt32(Waited)));
	return true;
}

void FN2CTranslateBatchCall::HandleProgress(int32 Index, int32 Total, const FString& GraphName)
{
	if (bFinished.load())
	{
		return;
	}
	CurrentIndex = Index;
	// Blueprint and graph: several Blueprints' event graphs share the graph name
	CurrentGraphName = Setup.TagInfos.IsValidIndex(Index)
		? FString::Printf(TEXT("%s.%s"), *FPackageName::ObjectPathToObjectName(Setup.TagInfos[Index].BlueprintPath), *GraphName)
		: GraphName;
	ItemStartTime = FPlatformTime::Seconds();
	const float Slot = 0.96f / static_cast<float>(FMath::Max(1, Total));
	Report(0.02f + Slot * static_cast<float>(Index), FString::Printf(TEXT("Translating %s (%d of %d)"), *CurrentGraphName, Index + 1, Total));
}

void FN2CTranslateBatchCall::Report(float Progress, const FString& Message)
{
	// Each value sent is larger than the last, as MCP requires; 1 is kept for the end
	LastProgress = FMath::Min(FMath::Max(Progress, LastProgress + 0.0001f), 0.99f);
	Context->ReportProgress(LastProgress, Message);
}

void FN2CTranslateBatchCall::HandleBatchComplete(const FN2CBatchTranslationResult& Result)
{
	if (bFinished.load())
	{
		return;
	}
	if (bAborting)
	{
		Finish(FMcpToolCallResult::CreateErrorResult(AbortMessage));
		return;
	}

	const TSharedRef<FJsonObject> Json = BuildResultJson(Setup, UN2CBatchTranslationOrchestrator::Get(), Result,
		N2CMcpBlueprintExport::MaxInlineTokens);
	const FString Text = N2CTranslateBatchCallPrivate::ToText(Json);
	const bool bAnySucceeded = Result.SuccessCount > 0;
	Context->ReportProgress(1.0f, bAnySucceeded ? TEXT("Batch translation complete") : TEXT("Batch translation failed"));

	// The translations were written from the Blueprints' own text, so they are untrusted data (card 0124). A batch with
	// no success is an error result whose body still lists every item's reason.
	Finish(bAnySucceeded
		? FMcpToolCallResult::CreateTextResult(FN2CMcpUntrustedContent::MarkJson(Text))
		: FMcpToolCallResult::CreateTextResult(Text, /*bInIsError*/ true));
}

TSharedRef<FJsonObject> FN2CTranslateBatchCall::BuildResultJson(const FSetup& Setup, const UN2CBatchTranslationOrchestrator& Orchestrator,
	const FN2CBatchTranslationResult& Result, int32 MaxInlineTokens)
{
	using namespace N2CTranslateBatchCallPrivate;

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("provider"), Setup.ProviderId);
	Json->SetStringField(TEXT("model"), Setup.ModelId);
	Json->SetStringField(TEXT("language"), Setup.LanguageId);
	Json->SetStringField(TEXT("batchOutputPath"), FPaths::ConvertRelativePathToFull(Result.BatchOutputPath));
	if (!Result.BatchOutputPath.IsEmpty())
	{
		Json->SetStringField(TEXT("summaryPath"), FPaths::ConvertRelativePathToFull(Result.BatchOutputPath / TEXT("BatchSummary.json")));
	}
	Json->SetNumberField(TEXT("total"), Result.TotalCount);
	Json->SetNumberField(TEXT("succeeded"), Result.SuccessCount);
	Json->SetNumberField(TEXT("failed"), Result.FailureCount);
	Json->SetNumberField(TEXT("skipped"), Result.SkippedCount);
	Json->SetNumberField(TEXT("durationSeconds"), Result.TotalTimeSeconds);
	const TSharedRef<FJsonObject> Usage = MakeShared<FJsonObject>();
	Usage->SetNumberField(TEXT("input_tokens"), Result.TotalInputTokens);
	Usage->SetNumberField(TEXT("output_tokens"), Result.TotalOutputTokens);
	Json->SetObjectField(TEXT("usage"), Usage);

	TArray<TSharedPtr<FJsonValue>> Items;
	TArray<TSharedPtr<FJsonObject>> ItemsWithCode;
	const TArray<FN2CBatchTranslationItem>& BatchItems = Orchestrator.GetBatchItems();
	for (int32 Index = 0; Index < BatchItems.Num(); ++Index)
	{
		const FN2CBatchTranslationItem& Item = BatchItems[Index];
		const TSharedRef<FJsonObject> ItemJson = MakeShared<FJsonObject>();
		ItemJson->SetStringField(TEXT("blueprintPath"), Item.TagInfo.BlueprintPath);
		ItemJson->SetStringField(TEXT("graphName"), Item.TagInfo.GraphName);
		ItemJson->SetStringField(TEXT("graphGuid"), Item.TagInfo.GraphGuid);
		if (Setup.Kinds.IsValidIndex(Index))
		{
			ItemJson->SetStringField(TEXT("kind"), Setup.Kinds[Index]);
		}
		if (!Item.TagInfo.Tag.IsEmpty())
		{
			ItemJson->SetStringField(TEXT("tag"), Item.TagInfo.Tag);
		}
		ItemJson->SetStringField(TEXT("status"), StatusId(Item.Status));

		if (Item.Status == EN2CBatchItemStatus::Completed)
		{
			// Where the item was saved (UN2CGraphStateManager::LoadTranslation reads it back) and what it holds
			const FString Directory = FPaths::ConvertRelativePathToFull(Orchestrator.GetItemOutputDirectory(Item));
			ItemJson->SetStringField(TEXT("outputDirectory"), Directory);
			TArray<FString> Found;
			IFileManager::Get().FindFilesRecursive(Found, *Directory, TEXT("*"), /*Files*/ true, /*Directories*/ false);
			Found.Sort();
			TArray<TSharedPtr<FJsonValue>> Files;
			for (const FString& File : Found)
			{
				Files.Add(MakeShared<FJsonValueString>(FPaths::ConvertRelativePathToFull(File)));
			}
			ItemJson->SetArrayField(TEXT("files"), Files);

			TArray<TSharedPtr<FJsonValue>> Graphs;
			for (const FN2CGraphTranslation& Graph : Item.TranslationResponse.Graphs)
			{
				Graphs.Add(MakeShared<FJsonValueObject>(GraphJson(Graph)));
			}
			ItemJson->SetArrayField(TEXT("graphs"), Graphs);
			ItemsWithCode.Add(ItemJson);

			const TSharedRef<FJsonObject> ItemUsage = MakeShared<FJsonObject>();
			ItemUsage->SetNumberField(TEXT("input_tokens"), Item.TranslationResponse.Usage.InputTokens);
			ItemUsage->SetNumberField(TEXT("output_tokens"), Item.TranslationResponse.Usage.OutputTokens);
			ItemJson->SetObjectField(TEXT("usage"), ItemUsage);
		}
		else if (Item.Status == EN2CBatchItemStatus::Failed)
		{
			// The typed error (kind, HTTP status, provider message, retryable) when the request failed
			const TSharedRef<FJsonObject> Error = Item.TranslationResponse.Error.IsError()
				? Item.TranslationResponse.Error.ToJsonObject()
				: FN2CTranslationError::Make(Item.ErrorMessage.IsEmpty() ? FString(TEXT("Translation failed")) : Item.ErrorMessage).ToJsonObject();
			ItemJson->SetObjectField(TEXT("error"), Error);
		}
		else if (Item.Status == EN2CBatchItemStatus::Skipped)
		{
			ItemJson->SetStringField(TEXT("reason"), Item.ErrorMessage.IsEmpty() ? FString(TEXT("The batch was cancelled before this item")) : Item.ErrorMessage);
		}
		Items.Add(MakeShared<FJsonValueObject>(ItemJson));
	}
	Json->SetArrayField(TEXT("items"), Items);
	Json->SetArrayField(TEXT("failures"), Setup.TargetFailures);
	if (Setup.Skipped.Num() > 0)
	{
		Json->SetArrayField(TEXT("skippedGraphs"), Setup.Skipped);
	}
	if (Setup.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Warning : Setup.Warnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Json->SetArrayField(TEXT("warnings"), Warnings);
	}

	// The code stays in the result while the whole of it fits a client's tool output; otherwise the files hold it
	Json->SetBoolField(TEXT("codeInline"), true);
	const int32 EstimatedTokens = N2CMcpBlueprintExport::EstimateTokens(ToText(Json));
	if (EstimatedTokens > MaxInlineTokens)
	{
		for (const TSharedPtr<FJsonObject>& ItemJson : ItemsWithCode)
		{
			ItemJson->RemoveField(TEXT("graphs"));
		}
		Json->SetBoolField(TEXT("codeInline"), false);
		Json->SetStringField(TEXT("codeOmitted"), FString::Printf(
			TEXT("The result with the code would be about %d tokens, over the %d inline limit: read each item's files (read-file)."),
			EstimatedTokens, MaxInlineTokens));
	}
	return Json;
}

void FN2CTranslateBatchCall::AbortBatch(const FString& Message)
{
	if (bFinished.load() || bAborting)
	{
		return;
	}
	bAborting = true;
	AbortMessage = Message;

	if (IsOurBatchRunning())
	{
		// Cancels the in-flight request at once and completes the batch (HandleBatchComplete sets the result), unless
		// the item's response is being handled; the ticker then finishes the call once the batch has ended
		UN2CBatchTranslationOrchestrator::Get().CancelBatch();
	}
	if (!bFinished.load() && !IsOurBatchRunning())
	{
		Finish(FMcpToolCallResult::CreateErrorResult(AbortMessage));
	}
}

void FN2CTranslateBatchCall::Finish(const FMcpToolCallResult& Result)
{
	if (bFinished.exchange(true))
	{
		return;
	}
	StopTicker();
	Unsubscribe();

	// Setting the value runs the caller's continuation, which may drop the last outside reference to this call: keep
	// it alive until this function returns
	const TSharedPtr<FN2CTranslateBatchCall, ESPMode::ThreadSafe> KeepAlive = MoveTemp(SelfWhileRunning);
	SelfWhileRunning.Reset();
	Promise.SetValue(Result);
}

void FN2CTranslateBatchCall::Unsubscribe()
{
	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
	if (ProgressHandle.IsValid())
	{
		Orchestrator.OnProgressNative.Remove(ProgressHandle);
		ProgressHandle.Reset();
	}
	if (CompleteHandle.IsValid())
	{
		Orchestrator.OnBatchCompleteNative.Remove(CompleteHandle);
		CompleteHandle.Reset();
	}
}

void FN2CTranslateBatchCall::StopTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}
