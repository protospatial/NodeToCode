// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "Core/N2CNodeCollector.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"
#include "Misc/Paths.h"
#include "Utils/N2CPathUtils.h"
#include "LLM/N2CTranslationArchive.h"
#include "Utils/N2CGraphUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"

UN2CBatchTranslationOrchestrator& UN2CBatchTranslationOrchestrator::Get()
{
	static UN2CBatchTranslationOrchestrator* Instance = nullptr;
	if (!Instance)
	{
		Instance = NewObject<UN2CBatchTranslationOrchestrator>();
		Instance->AddToRoot(); // Prevent garbage collection
	}
	return *Instance;
}

bool UN2CBatchTranslationOrchestrator::StartBatchTranslation(const TArray<FN2CTagInfo>& TagInfos)
{
	FString Error;
	return StartBatchTranslationWithOptions(TagInfos, FN2CBatchTranslationOptions(), Error);
}

bool UN2CBatchTranslationOrchestrator::StartBatchTranslationWithOptions(const TArray<FN2CTagInfo>& TagInfos,
	const FN2CBatchTranslationOptions& Options, FString& OutError)
{
	OutError.Reset();

	// Validate input
	if (TagInfos.IsEmpty())
	{
		OutError = TEXT("INVALID_ARGUMENT: the batch has no items");
		FN2CLogger::Get().LogError(TEXT("Cannot start batch translation with empty TagInfos array"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Check if already in progress
	if (bBatchInProgress)
	{
		OutError = FString::Printf(TEXT("BATCH_IN_PROGRESS: a batch translation is already running (item %d of %d); wait for it or cancel it"),
			CurrentItemIndex + 1, BatchItems.Num());
		FN2CLogger::Get().LogWarning(TEXT("Batch translation already in progress"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Check if LLM module is already processing
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
	{
		OutError = TEXT("LLM_BUSY: another translation is in flight; start the batch when it has finished");
		FN2CLogger::Get().LogWarning(TEXT("LLM module is already processing a translation"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Initialize the LLM module, or pick up settings changed since it was (each item's request does this again)
	if (LLMModule && !LLMModule->RefreshFromSettings())
	{
		OutError = TEXT("LLM_UNAVAILABLE: the LLM module could not be initialized for the provider in the settings");
		FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM module for batch translation"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Initialize batch state
	BatchOptions = Options;
	bBatchInProgress = true;
	bCancellationRequested = false;
	CurrentItemIndex = -1;
	CurrentRequest.Reset();
	++BatchSerial;
	BatchStartTime = FPlatformTime::Seconds();
	BatchItems.Empty();
	BlueprintCache.Empty();
	CurrentResult = FN2CBatchTranslationResult();

	// Convert TagInfos to BatchItems
	for (const FN2CTagInfo& TagInfo : TagInfos)
	{
		FN2CBatchTranslationItem Item;
		Item.TagInfo = TagInfo;
		Item.Status = EN2CBatchItemStatus::Pending;
		BatchItems.Add(Item);
	}
	CurrentResult.TotalCount = BatchItems.Num();
	AssignOutputFolders();

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Starting batch translation with %d items"), BatchItems.Num()),
		EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));

	// Create output directory
	CreateBatchOutputDirectory();

	// Resolve all blueprints and graphs upfront
	if (!ResolveBlueprintsAndGraphs())
	{
		FN2CLogger::Get().LogWarning(TEXT("Some items could not be resolved, continuing with valid items"), TEXT("BatchOrchestrator"));
	}

	// Check if we have any valid items to process
	bool bHasValidItems = false;
	for (const FN2CBatchTranslationItem& Item : BatchItems)
	{
		if (Item.Status == EN2CBatchItemStatus::Pending)
		{
			bHasValidItems = true;
			break;
		}
	}

	if (!bHasValidItems)
	{
		TArray<FString> Reasons;
		for (const FN2CBatchTranslationItem& Item : BatchItems)
		{
			Reasons.AddUnique(Item.ErrorMessage);
		}
		OutError = FString::Printf(TEXT("NO_VALID_ITEMS: none of the %d items could be resolved (%s)"), BatchItems.Num(),
			*FString::Join(Reasons, TEXT("; ")));
		FN2CLogger::Get().LogError(TEXT("No valid items to process in batch"), TEXT("BatchOrchestrator"));
		CleanupBatch();
		return false;
	}

	// Each item's request reports its own result (HandleItemResult); the module-wide OnTranslationResponseReceived
	// broadcast is not used, so another caller's translation can never be taken for a batch item

// Start processing first item
	ProcessNextItem();
	return true;
}

void UN2CBatchTranslationOrchestrator::CancelBatch()
{
	if (!bBatchInProgress)
	{
		return;
	}

	FN2CLogger::Get().Log(TEXT("Batch translation cancellation requested"), EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
	bCancellationRequested = true;

	// Cancel the in-flight request now instead of waiting for its response (which could take minutes)
	FN2CLLMRequestHandlePtr Request = MoveTemp(CurrentRequest);
	CurrentRequest.Reset();
	if (!Request.IsValid() || !Request->Cancel())
	{
		// No request in flight, or its result is being handled: ProcessNextItem sees the flag after it
		return;
	}

	// The request will never complete: its item is skipped, then ProcessNextItem skips the rest and finalizes
	if (BatchItems.IsValidIndex(CurrentItemIndex) && BatchItems[CurrentItemIndex].Status == EN2CBatchItemStatus::Processing)
	{
		FN2CBatchTranslationItem& Item = BatchItems[CurrentItemIndex];
		Item.Status = EN2CBatchItemStatus::Skipped;
		Item.ErrorMessage = TEXT("Cancelled while translating");
		CurrentResult.SkippedCount++;
		FN2CLogger::Get().Log(FString::Printf(TEXT("Cancelled the in-flight translation of %s"), *Item.TagInfo.GraphName),
			EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
	}
	ProcessNextItem();
}

float UN2CBatchTranslationOrchestrator::GetBatchProgress() const
{
	if (BatchItems.IsEmpty())
	{
		return 0.0f;
	}

	int32 ProcessedCount = 0;
	for (const FN2CBatchTranslationItem& Item : BatchItems)
	{
		if (Item.Status != EN2CBatchItemStatus::Pending && Item.Status != EN2CBatchItemStatus::Processing)
		{
			ProcessedCount++;
		}
	}

	return static_cast<float>(ProcessedCount) / static_cast<float>(BatchItems.Num());
}

void UN2CBatchTranslationOrchestrator::ProcessNextItem()
{
	// Find next pending item
	CurrentItemIndex++;
	while (CurrentItemIndex < BatchItems.Num())
	{
		if (BatchItems[CurrentItemIndex].Status == EN2CBatchItemStatus::Pending)
		{
			break;
		}
		CurrentItemIndex++;
	}

	// Check if done
	if (CurrentItemIndex >= BatchItems.Num())
	{
		FinalizeBatch();
		return;
	}

	// Check cancellation
	if (bCancellationRequested)
	{
		for (int32 i = CurrentItemIndex; i < BatchItems.Num(); i++)
		{
			if (BatchItems[i].Status == EN2CBatchItemStatus::Pending)
			{
				BatchItems[i].Status = EN2CBatchItemStatus::Skipped;
				CurrentResult.SkippedCount++;
			}
		}
		FinalizeBatch();
		return;
	}

	FN2CBatchTranslationItem& Item = BatchItems[CurrentItemIndex];
	Item.Status = EN2CBatchItemStatus::Processing;

	// Broadcast progress (Blueprint and Native)
	OnProgress.Broadcast(CurrentItemIndex, BatchItems.Num(), Item.TagInfo.GraphName);
	OnProgressNative.Broadcast(CurrentItemIndex, BatchItems.Num(), Item.TagInfo.GraphName);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Processing item %d/%d: %s"), CurrentItemIndex + 1, BatchItems.Num(), *Item.TagInfo.GraphName),
		EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));

	// Collect and serialize the graph
	FString JsonPayload = CollectAndSerializeGraph(Item.CachedGraph.Get(), Item.CachedBlueprint.Get());

	if (JsonPayload.IsEmpty())
	{
		Item.Status = EN2CBatchItemStatus::Failed;
		Item.ErrorMessage = TEXT("Failed to collect/serialize graph");
		Item.TranslationResponse.Error = FN2CTranslationError::Make(Item.ErrorMessage);
		CurrentResult.FailureCount++;
		CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);

		FN2CLogger::Get().LogError(
			FString::Printf(TEXT("Failed to collect/serialize graph: %s"), *Item.TagInfo.GraphName),
			TEXT("BatchOrchestrator"));

		// Broadcast item completion with failure (Blueprint and Native)
		OnItemComplete.Broadcast(Item.TagInfo, Item.TranslationResponse, false, CurrentItemIndex, BatchItems.Num());
		OnItemCompleteNative.Broadcast(Item.TagInfo, Item.TranslationResponse, false, CurrentItemIndex, BatchItems.Num());

		// Continue to next item
		ProcessNextItem();
		return;
	}

	// Cache the JSON payload for saving alongside the translation
	Item.CachedJsonPayload = JsonPayload;

	// Send to LLM module
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (LLMModule)
	{
		// The request re-reads the provider, model and key from the settings, so a change made during the batch applies
		// from this item on. Its result comes back through this item's own OnParsed, tagged with the batch and item it
		// was sent for; HandleItemResult saves it once, in the batch's archive run (card 0252).
		const int32 Serial = BatchSerial;
		const int32 ItemIndex = CurrentItemIndex;
		TWeakObjectPtr<UN2CBatchTranslationOrchestrator> WeakThis(this);
		const FOnN2CTranslationParsed OnParsed = FOnN2CTranslationParsed::CreateLambda(
			[WeakThis, Serial, ItemIndex](const FN2CTranslationResponse& Response, bool bSuccess)
			{
				if (UN2CBatchTranslationOrchestrator* StrongThis = WeakThis.Get())
				{
					StrongThis->HandleItemResult(Serial, ItemIndex, Response, bSuccess);
				}
			});
		// A batch with its own provider, model and language (translate-batch) sends every item with them (and keeps
		// the module from writing an MCP run: the batch saves the item); the editor's batch reads the settings per item
		FN2CLLMRequestHandleRef Request = BatchOptions.bUseRequestConfig
			? LLMModule->ProcessN2CJsonWithOverrides(JsonPayload, BatchOptions.RequestConfig, BatchOptions.Language,
				FOnLLMResponseReceived(), OnParsed, /*bArchiveRun*/ false)
			: LLMModule->ProcessN2CJson(JsonPayload, FOnLLMResponseReceived(), OnParsed);

		// An error found before sending completes inside the call (and may already have moved on to the next item,
		// which then holds CurrentRequest): keep only a request that is really in flight for this item
		if (!Request->IsFinished() && Serial == BatchSerial && ItemIndex == CurrentItemIndex)
		{
			CurrentRequest = Request;
		}
	}
	else
	{
		Item.Status = EN2CBatchItemStatus::Failed;
		Item.ErrorMessage = TEXT("LLM module not available");
		Item.TranslationResponse.Error = FN2CTranslationError::Make(Item.ErrorMessage);
		CurrentResult.FailureCount++;
		CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);

		// Broadcast item completion with failure (Blueprint and Native)
		OnItemComplete.Broadcast(Item.TagInfo, Item.TranslationResponse, false, CurrentItemIndex, BatchItems.Num());
		OnItemCompleteNative.Broadcast(Item.TagInfo, Item.TranslationResponse, false, CurrentItemIndex, BatchItems.Num());
		ProcessNextItem();
	}
}

void UN2CBatchTranslationOrchestrator::HandleItemResult(int32 Serial, int32 ItemIndex, const FN2CTranslationResponse& Response, bool bSuccess)
{
	// Only the result of the request this batch sent for its current item
	if (!bBatchInProgress || Serial != BatchSerial || ItemIndex != CurrentItemIndex || !BatchItems.IsValidIndex(CurrentItemIndex))
	{
		return;
	}
	CurrentRequest.Reset();

	FN2CBatchTranslationItem& Item = BatchItems[CurrentItemIndex];

	// Only process if this item is currently being processed
	if (Item.Status != EN2CBatchItemStatus::Processing)
	{
		return;
	}

	Item.TranslationResponse = Response;

	if (bSuccess)
	{
		Item.Status = EN2CBatchItemStatus::Completed;
		CurrentResult.SuccessCount++;
		CurrentResult.TotalInputTokens += Response.Usage.InputTokens;
		CurrentResult.TotalOutputTokens += Response.Usage.OutputTokens;
		CurrentResult.TotalCacheReadInputTokens += Response.Usage.CacheReadInputTokens;
		CurrentResult.TotalCacheWriteInputTokens += Response.Usage.CacheWriteInputTokens;
		CurrentResult.TotalReasoningTokens += Response.Usage.ReasoningTokens;

		// Save individual translation files
		SaveItemTranslation(Item);

		FN2CLogger::Get().Log(
			FString::Printf(TEXT("Successfully translated: %s (tokens: %d in, %d out)"),
				*Item.TagInfo.GraphName, Response.Usage.InputTokens, Response.Usage.OutputTokens),
			EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
	}
	else
	{
		Item.Status = EN2CBatchItemStatus::Failed;
		// The specific reason (kind, HTTP status, provider message) from the typed error
		Item.ErrorMessage = Response.Error.IsError() ? Response.Error.ToDisplayString() : FString(TEXT("Translation failed"));
		CurrentResult.FailureCount++;
		CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);

		FN2CLogger::Get().LogError(
			FString::Printf(TEXT("Translation failed for %s: %s"), *Item.TagInfo.GraphName, *Item.ErrorMessage),
			TEXT("BatchOrchestrator"));
	}

	// Broadcast per-item completion (Blueprint and Native)
	OnItemComplete.Broadcast(Item.TagInfo, Response, bSuccess, CurrentItemIndex, BatchItems.Num());
	OnItemCompleteNative.Broadcast(Item.TagInfo, Response, bSuccess, CurrentItemIndex, BatchItems.Num());

	// Continue to next item
	ProcessNextItem();
}

void UN2CBatchTranslationOrchestrator::FinalizeBatch()
{
	// Calculate final timing
	CurrentResult.TotalTimeSeconds = static_cast<float>(FPlatformTime::Seconds() - BatchStartTime);
	CurrentResult.BatchOutputPath = BatchOutputPath;

	// Generate batch summary, and the run's manifest once more (it already lists every saved item)
	GenerateBatchSummary();
	if (BatchArchive.IsValid())
	{
		BatchArchive->WriteManifest();
	}

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Batch translation complete. Success: %d, Failed: %d, Skipped: %d, Time: %.2fs"),
			CurrentResult.SuccessCount, CurrentResult.FailureCount, CurrentResult.SkippedCount, CurrentResult.TotalTimeSeconds),
		EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));

	// Broadcast batch completion (Blueprint and Native)
	OnBatchComplete.Broadcast(CurrentResult);
	OnBatchCompleteNative.Broadcast(CurrentResult);

	// Clean up
	CleanupBatch();
}

void UN2CBatchTranslationOrchestrator::CleanupBatch()
{
	// A request still held here was cancelled or has completed; drop the reference
	CurrentRequest.Reset();
	BatchArchive.Reset();

// Clear blueprint cache
	BlueprintCache.Empty();

	// Reset state
	bBatchInProgress = false;
	bCancellationRequested = false;
	CurrentItemIndex = -1;
}

bool UN2CBatchTranslationOrchestrator::ResolveBlueprintsAndGraphs()
{
	bool bAnyResolved = false;

	for (int32 ItemIndex = 0; ItemIndex < BatchItems.Num(); ++ItemIndex)
	{
		FN2CBatchTranslationItem& Item = BatchItems[ItemIndex];

		// A graph the caller resolved already, when it still belongs to the Blueprint at the item's path
		if (BatchOptions.Graphs.IsValidIndex(ItemIndex))
		{
			UEdGraph* Given = BatchOptions.Graphs[ItemIndex].Get();
			UBlueprint* Owner = Given ? Given->GetTypedOuter<UBlueprint>() : nullptr;
			if (Owner && Owner->GetPathName() == Item.TagInfo.BlueprintPath)
			{
				FString Reason;
				if (!FN2CGraphUtils::CanTranslateGraph(Given, &Reason))
				{
					Item.Status = EN2CBatchItemStatus::Failed;
					Item.ErrorMessage = Reason;
					CurrentResult.FailureCount++;
					CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);
					FN2CLogger::Get().LogError(Item.ErrorMessage, TEXT("BatchOrchestrator"));
					continue;
				}
				Item.CachedBlueprint = Owner;
				Item.CachedGraph = Given;
				bAnyResolved = true;
				continue;
			}
		}

		// Load blueprint (using cache if already loaded)
		UBlueprint* Blueprint = nullptr;
		if (TWeakObjectPtr<UBlueprint>* CachedBP = BlueprintCache.Find(Item.TagInfo.BlueprintPath))
		{
			Blueprint = CachedBP->Get();
		}
		else
		{
			Blueprint = LoadBlueprintFromPath(Item.TagInfo.BlueprintPath);
			if (Blueprint)
			{
				BlueprintCache.Add(Item.TagInfo.BlueprintPath, Blueprint);
			}
		}

		if (!Blueprint)
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = FString::Printf(TEXT("Failed to load blueprint: %s"), *Item.TagInfo.BlueprintPath);
			CurrentResult.FailureCount++;
			CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(Item.ErrorMessage, TEXT("BatchOrchestrator"));
			continue;
		}

		Item.CachedBlueprint = Blueprint;

		// Parse and find graph by GUID
		FGuid GraphGuid;
		if (!FGuid::Parse(Item.TagInfo.GraphGuid, GraphGuid) || !GraphGuid.IsValid())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = FString::Printf(TEXT("Invalid graph GUID: %s"), *Item.TagInfo.GraphGuid);
			CurrentResult.FailureCount++;
			CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(Item.ErrorMessage, TEXT("BatchOrchestrator"));
			continue;
		}

		// Every graph of the Blueprint: interface implementations and collapsed graphs too (card 0251)
		UEdGraph* Graph = FN2CGraphUtils::FindGraphByGuid(Blueprint, GraphGuid);
		FString Reason;
		if (!Graph)
		{
			Reason = FString::Printf(TEXT("Graph not found: no graph %s has GUID %s. The graph may have been deleted; remove the tag."),
				*FN2CGraphUtils::DescribeGraph(Item.TagInfo.GraphName, Item.TagInfo.BlueprintPath), *Item.TagInfo.GraphGuid);
		}
		else
		{
			FN2CGraphUtils::CanTranslateGraph(Graph, &Reason);
		}
		if (!Reason.IsEmpty())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = Reason;
			CurrentResult.FailureCount++;
			CurrentResult.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(Item.ErrorMessage, TEXT("BatchOrchestrator"));
			continue;
		}

		Item.CachedGraph = Graph;
		bAnyResolved = true;

		FN2CLogger::Get().Log(
			FString::Printf(TEXT("Resolved graph '%s' in blueprint '%s'"), *Item.TagInfo.GraphName, *Item.TagInfo.BlueprintPath),
			EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
	}

	return bAnyResolved;
}

UBlueprint* UN2CBatchTranslationOrchestrator::LoadBlueprintFromPath(const FString& BlueprintPath)
{
	FSoftObjectPath SoftPath(BlueprintPath);
	UObject* LoadedObject = SoftPath.TryLoad();

	if (!LoadedObject)
	{
		FN2CLogger::Get().LogError(
			FString::Printf(TEXT("Failed to load object at path: %s"), *BlueprintPath),
			TEXT("BatchOrchestrator"));
		return nullptr;
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(LoadedObject);
	if (!Blueprint)
	{
		FN2CLogger::Get().LogError(
			FString::Printf(TEXT("Object at path is not a Blueprint: %s"), *BlueprintPath),
			TEXT("BatchOrchestrator"));
		return nullptr;
	}

	return Blueprint;
}

FString UN2CBatchTranslationOrchestrator::CollectAndSerializeGraph(UEdGraph* Graph, UBlueprint* Blueprint)
{
	if (!Graph || !Blueprint)
	{
		return FString();
	}

	// Collect nodes from the graph
	TArray<UK2Node*> CollectedNodes;
	if (!FN2CNodeCollector::Get().CollectNodesFromGraph(Graph, CollectedNodes))
	{
		FN2CLogger::Get().LogError(TEXT("Failed to collect nodes from graph"), TEXT("BatchOrchestrator"));
		return FString();
	}

	if (CollectedNodes.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("No nodes collected from graph"), TEXT("BatchOrchestrator"));
		return FString();
	}

	// Translate to N2CBlueprint structure
	TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(CollectedNodes);
	if (Capture.HasError())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to generate N2CStruct: %s"), *Capture.GetError()), TEXT("BatchOrchestrator"));
		return FString();
	}

	// Serialize to JSON (minified for LLM)
	return FN2CSerializer::ToJson(Capture.GetValue().Blueprint, EN2CJsonStyle::Condensed);
}

void UN2CBatchTranslationOrchestrator::CreateBatchOutputDirectory()
{
	// One archive run for the batch: <base>/BatchTranslation_<yyyyMMdd-HHmmss-fff>_<run id>, with its manifest
	FN2CTranslationRunInfo Info;
	Info.Kind = EN2CTranslationRunKind::Batch;
	Info.Name = TEXT("BatchTranslation");
	Info.Provider = GetSaveProvider();
	Info.Model = GetSaveModel();
	Info.Language = GetSaveLanguage();
	BatchArchive = FN2CTranslationArchive::BeginRun(Info);
	BatchOutputPath = BatchArchive.IsValid() ? BatchArchive->GetFolder() : FString();

	if (BatchOutputPath.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("Failed to create the batch output directory"), TEXT("BatchOrchestrator"));
	}
	else
	{
		FN2CLogger::Get().Log(
			FString::Printf(TEXT("Created batch output directory: %s"), *BatchOutputPath),
			EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
	}
}

void UN2CBatchTranslationOrchestrator::SaveItemTranslation(const FN2CBatchTranslationItem& Item)
{
	if (!BatchArchive.IsValid())
	{
		return;
	}

	// Every entry of the response in its own folder under the item's folder, written once (card 0252)
	FN2CArchiveSource Source;
	Source.InputJson = Item.CachedJsonPayload;
	Source.GraphName = Item.TagInfo.GraphName;
	Source.BlueprintPath = Item.TagInfo.BlueprintPath;
	Source.GraphGuid = Item.TagInfo.GraphGuid;
	const FString ItemFolder = Item.OutputFolderName.IsEmpty()
		? FN2CPathUtils::GetTranslationFileStem(Item.TagInfo.GraphName) : Item.OutputFolderName;
	FN2CArchivedResponse Written;
	BatchArchive->WriteResponse(Item.TranslationResponse, Source, ItemFolder, Written);

	// Record the tagged graph once, at its root entry's folder (UN2CGraphStateManager::LoadTranslation reads it back)
	const FN2CArchivedEntry* Root = Written.GetRoot();
	FGuid GraphGuid;
	if (!Root || !FGuid::Parse(Item.TagInfo.GraphGuid, GraphGuid) || !GraphGuid.IsValid()
		|| !Item.TranslationResponse.Graphs.IsValidIndex(Root->ResponseIndex))
	{
		return;
	}
	const FN2CGraphTranslation& Graph = Item.TranslationResponse.Graphs[Root->ResponseIndex];

	FN2CTranslationSummary Summary;
	// First line of the declaration as a preview
	if (!Graph.Code.GraphDeclaration.IsEmpty())
	{
		int32 NewlineIndex;
		Summary.DeclarationPreview = Graph.Code.GraphDeclaration.FindChar('\n', NewlineIndex)
			? Graph.Code.GraphDeclaration.Left(NewlineIndex).TrimStartAndEnd()
			: Graph.Code.GraphDeclaration.TrimStartAndEnd();
	}
	if (!Graph.Code.GraphImplementation.IsEmpty())
	{
		int32 LineCount = 1;
		for (const TCHAR Char : Graph.Code.GraphImplementation)
		{
			LineCount += Char == '\n' ? 1 : 0;
		}
		Summary.ImplementationLines = LineCount;
	}
	Summary.bHasNotes = !Graph.Code.ImplementationNotes.IsEmpty();

	// The shared language id and record path, which UN2CGraphStateManager::LoadTranslation reads back
	UN2CGraphStateManager::Get().RecordSavedTranslation(
		GraphGuid,
		Item.TagInfo.GraphName,
		FSoftObjectPath(Item.TagInfo.BlueprintPath),
		Root->Folder,
		GetSaveProvider(),
		GetSaveModel(),
		GetSaveLanguage(),
		Summary
	);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Recorded translation state for graph: %s"), *Item.TagInfo.GraphName),
		EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
}

void UN2CBatchTranslationOrchestrator::GenerateBatchSummary()
{
	if (BatchOutputPath.IsEmpty())
	{
		return;
	}

	// Get settings for metadata
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();

	// Build summary JSON
	TSharedPtr<FJsonObject> SummaryJson = MakeShared<FJsonObject>();

	SummaryJson->SetStringField(TEXT("version"), TEXT("1.0"));
	SummaryJson->SetStringField(TEXT("timestamp"), FDateTime::Now().ToIso8601());

	// Statistics
	TSharedPtr<FJsonObject> StatsJson = MakeShared<FJsonObject>();
	StatsJson->SetNumberField(TEXT("total"), CurrentResult.TotalCount);
	StatsJson->SetNumberField(TEXT("successful"), CurrentResult.SuccessCount);
	StatsJson->SetNumberField(TEXT("failed"), CurrentResult.FailureCount);
	StatsJson->SetNumberField(TEXT("skipped"), CurrentResult.SkippedCount);
	StatsJson->SetNumberField(TEXT("durationSeconds"), CurrentResult.TotalTimeSeconds);
	StatsJson->SetNumberField(TEXT("totalInputTokens"), CurrentResult.TotalInputTokens);
	StatsJson->SetNumberField(TEXT("totalOutputTokens"), CurrentResult.TotalOutputTokens);
	StatsJson->SetNumberField(TEXT("totalCacheReadInputTokens"), CurrentResult.TotalCacheReadInputTokens);
	StatsJson->SetNumberField(TEXT("totalCacheWriteInputTokens"), CurrentResult.TotalCacheWriteInputTokens);
	StatsJson->SetNumberField(TEXT("totalReasoningTokens"), CurrentResult.TotalReasoningTokens);
	SummaryJson->SetObjectField(TEXT("statistics"), StatsJson);

	// Settings
	TSharedPtr<FJsonObject> SettingsJson = MakeShared<FJsonObject>();
	if (Settings || BatchOptions.bUseRequestConfig)
	{
		SettingsJson->SetStringField(TEXT("provider"), GetSaveProvider());
		SettingsJson->SetStringField(TEXT("model"), GetSaveModel());
		SettingsJson->SetStringField(TEXT("language"), N2C::GetLanguage(GetSaveLanguage()).Id); // Python, not EN2CCodeLanguage::Python
	}
	SummaryJson->SetObjectField(TEXT("settings"), SettingsJson);

	// Items
	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	for (const FN2CBatchTranslationItem& Item : BatchItems)
	{
		TSharedPtr<FJsonObject> ItemJson = MakeShared<FJsonObject>();
		ItemJson->SetStringField(TEXT("graphName"), Item.TagInfo.GraphName);
		ItemJson->SetStringField(TEXT("blueprintPath"), Item.TagInfo.BlueprintPath);
		ItemJson->SetStringField(TEXT("tag"), Item.TagInfo.Tag);
		ItemJson->SetStringField(TEXT("category"), Item.TagInfo.Category);
		ItemJson->SetStringField(TEXT("status"), UEnum::GetValueAsString(Item.Status));
		ItemJson->SetStringField(TEXT("graphGuid"), Item.TagInfo.GraphGuid);
		if (Item.Status == EN2CBatchItemStatus::Completed)
		{
			ItemJson->SetStringField(TEXT("outputFolder"), Item.OutputFolderName);
		}

		if (Item.Status == EN2CBatchItemStatus::Completed)
		{
			TSharedPtr<FJsonObject> TokensJson = MakeShared<FJsonObject>();
			TokensJson->SetNumberField(TEXT("input"), Item.TranslationResponse.Usage.InputTokens);
			TokensJson->SetNumberField(TEXT("output"), Item.TranslationResponse.Usage.OutputTokens);
			TokensJson->SetNumberField(TEXT("cacheReadInput"), Item.TranslationResponse.Usage.CacheReadInputTokens);
			TokensJson->SetNumberField(TEXT("cacheWriteInput"), Item.TranslationResponse.Usage.CacheWriteInputTokens);
			TokensJson->SetNumberField(TEXT("reasoning"), Item.TranslationResponse.Usage.ReasoningTokens);
			ItemJson->SetObjectField(TEXT("tokensUsed"), TokensJson);
		}

		if (!Item.ErrorMessage.IsEmpty())
		{
			ItemJson->SetStringField(TEXT("error"), Item.ErrorMessage);
		}

		// The typed error of a failed LLM request, so a reader can tell a rate limit from a bad key or a parse failure
		const FN2CTranslationError& ItemError = Item.TranslationResponse.Error;
		if (Item.Status == EN2CBatchItemStatus::Failed && ItemError.IsError())
		{
			ItemJson->SetStringField(TEXT("errorKind"), FN2CTranslationError::KindToString(ItemError.Kind));
			if (ItemError.HttpStatus > 0)
			{
				ItemJson->SetNumberField(TEXT("httpStatus"), ItemError.HttpStatus);
			}
			ItemJson->SetBoolField(TEXT("retryable"), ItemError.bRetryable);
			ItemJson->SetStringField(TEXT("providerMessage"), ItemError.Message);
		}

		ItemsArray.Add(MakeShared<FJsonValueObject>(ItemJson));
	}
	SummaryJson->SetArrayField(TEXT("items"), ItemsArray);

	// Serialize and save
	FString SummaryContent;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SummaryContent);
	FJsonSerializer::Serialize(SummaryJson.ToSharedRef(), Writer);

	FString SummaryPath = FPaths::Combine(BatchOutputPath, TEXT("BatchSummary.json"));
	if (!FN2CTranslationArchive::SaveTextAtomic(SummaryPath, SummaryContent))
	{
		FN2CLogger::Get().LogWarning(
			FString::Printf(TEXT("Failed to save batch summary: %s"), *SummaryPath),
			TEXT("BatchOrchestrator"));
	}
}

void UN2CBatchTranslationOrchestrator::AssignOutputFolders()
{
	TSet<FString> Used;
	auto Claim = [&Used](const FString& Candidate)
	{
		const FString Key = Candidate.ToLower(); // folder names must differ by more than case
		if (Candidate.IsEmpty() || Used.Contains(Key))
		{
			return false;
		}
		Used.Add(Key);
		return true;
	};

	for (FN2CBatchTranslationItem& Item : BatchItems)
	{
		const FString GraphStem = FN2CPathUtils::GetTranslationFileStem(Item.TagInfo.GraphName);
		const FString BlueprintStem = FN2CPathUtils::MakeSafeFileStem(FPackageName::ObjectPathToObjectName(Item.TagInfo.BlueprintPath));

		FString Folder = GraphStem;
		if (!Claim(Folder))
		{
			const FString Qualified = BlueprintStem.IsEmpty() ? GraphStem : BlueprintStem + TEXT("__") + GraphStem;
			Folder = Qualified;
			for (int32 Suffix = 2; !Claim(Folder); ++Suffix)
			{
				Folder = FString::Printf(TEXT("%s_%d"), *Qualified, Suffix);
			}
		}
		Item.OutputFolderName = Folder;
	}
}

FString UN2CBatchTranslationOrchestrator::GetItemOutputDirectory(const FN2CBatchTranslationItem& Item) const
{
	const FString Folder = Item.OutputFolderName.IsEmpty() ? FN2CPathUtils::GetTranslationFileStem(Item.TagInfo.GraphName)
		: Item.OutputFolderName;
	return BatchOutputPath.IsEmpty() || Folder.IsEmpty() ? FString() : FPaths::Combine(BatchOutputPath, Folder);
}

EN2CCodeLanguage UN2CBatchTranslationOrchestrator::GetSaveLanguage() const
{
	if (BatchOptions.bUseRequestConfig)
	{
		return BatchOptions.Language;
	}
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;
}

FString UN2CBatchTranslationOrchestrator::GetSaveProvider() const
{
	if (BatchOptions.bUseRequestConfig)
	{
		return UEnum::GetValueAsString(BatchOptions.RequestConfig.Provider);
	}
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings ? UEnum::GetValueAsString(Settings->Provider) : FString(TEXT("Unknown"));
}

FString UN2CBatchTranslationOrchestrator::GetSaveModel() const
{
	if (BatchOptions.bUseRequestConfig)
	{
		return BatchOptions.RequestConfig.Model;
	}
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings ? Settings->GetActiveModel() : FString(TEXT("Unknown"));
}

// ==================== Batch JSON Export (No LLM) ====================

bool UN2CBatchTranslationOrchestrator::BatchExportJson(
	const TArray<FN2CTagInfo>& TagInfos,
	FN2CBatchJsonExportResult& Result,
	bool bMinifyJson)
{
	Result = FN2CBatchJsonExportResult();

	// Validate input
	if (TagInfos.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("Cannot export with empty TagInfos array"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Check if batch translation is in progress (don't overlap)
	if (bBatchInProgress)
	{
		FN2CLogger::Get().LogWarning(TEXT("A batch translation is in progress, cannot start JSON export"), TEXT("BatchOrchestrator"));
		return false;
	}

	double StartTime = FPlatformTime::Seconds();

	// Prepare items for export
	TArray<FN2CBatchTranslationItem> ExportItems;
	for (const FN2CTagInfo& TagInfo : TagInfos)
	{
		FN2CBatchTranslationItem Item;
		Item.TagInfo = TagInfo;
		Item.Status = EN2CBatchItemStatus::Pending;
		ExportItems.Add(Item);
	}
	Result.TotalCount = ExportItems.Num();

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Starting batch JSON export with %d items"), ExportItems.Num()),
		EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));

	// A new folder under the translation output directory: BatchJson_<yyyyMMdd-HHmmss-fff>_<id>
	const FString OutputPath = FN2CTranslationArchive::CreateUniqueFolder(TEXT("BatchJson"));
	if (OutputPath.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("Failed to create the JSON export directory"), TEXT("BatchOrchestrator"));
		return false;
	}
	Result.OutputPath = OutputPath;

	// Clear and prepare blueprint cache
	TMap<FString, TWeakObjectPtr<UBlueprint>> LocalBlueprintCache;

	// Process each item synchronously
	for (FN2CBatchTranslationItem& Item : ExportItems)
	{
		// Load blueprint (using cache if already loaded)
		UBlueprint* Blueprint = nullptr;
		if (TWeakObjectPtr<UBlueprint>* CachedBP = LocalBlueprintCache.Find(Item.TagInfo.BlueprintPath))
		{
			Blueprint = CachedBP->Get();
		}
		else
		{
			Blueprint = LoadBlueprintFromPath(Item.TagInfo.BlueprintPath);
			if (Blueprint)
			{
				LocalBlueprintCache.Add(Item.TagInfo.BlueprintPath, Blueprint);
			}
		}

		if (!Blueprint)
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = TEXT("Failed to load blueprint");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Failed to load blueprint: %s"), *Item.TagInfo.BlueprintPath),
				TEXT("BatchOrchestrator"));
			continue;
		}

		Item.CachedBlueprint = Blueprint;

		// Parse and find graph by GUID
		FGuid GraphGuid;
		if (!FGuid::Parse(Item.TagInfo.GraphGuid, GraphGuid) || !GraphGuid.IsValid())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = TEXT("Invalid graph GUID");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Invalid graph GUID: %s"), *Item.TagInfo.GraphGuid),
				TEXT("BatchOrchestrator"));
			continue;
		}

		UEdGraph* Graph = FN2CGraphUtils::FindGraphByGuid(Blueprint, GraphGuid);
		FString Reason;
		if (!Graph)
		{
			Reason = FString::Printf(TEXT("Graph not found: no graph %s has GUID %s. The graph may have been deleted; remove the tag."),
				*FN2CGraphUtils::DescribeGraph(Item.TagInfo.GraphName, Item.TagInfo.BlueprintPath), *Item.TagInfo.GraphGuid);
		}
		else
		{
			FN2CGraphUtils::CanTranslateGraph(Graph, &Reason);
		}
		if (!Reason.IsEmpty())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = Reason;
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(Item.ErrorMessage, TEXT("BatchOrchestrator"));
			continue;
		}

		Item.CachedGraph = Graph;

		// Collect nodes from the graph
		TArray<UK2Node*> CollectedNodes;
		if (!FN2CNodeCollector::Get().CollectNodesFromGraph(Graph, CollectedNodes) || CollectedNodes.IsEmpty())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = TEXT("Failed to collect nodes");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Failed to collect nodes from graph: %s"), *Item.TagInfo.GraphName),
				TEXT("BatchOrchestrator"));
			continue;
		}

		// Translate to N2CBlueprint structure
		const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(CollectedNodes);
		if (Capture.HasError())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			// The capture's own reason, such as a graph of only ghost default events (card 0433)
			Item.ErrorMessage = Capture.GetError();
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Failed to generate N2CStruct for: %s: %s"), *Item.TagInfo.GraphName, *Capture.GetError()),
				TEXT("BatchOrchestrator"));
			continue;
		}

		// Serialize with formatting based on minify option
		FString JsonOutput = FN2CSerializer::ToJson(Capture.GetValue().Blueprint,
			bMinifyJson ? EN2CJsonStyle::Condensed : EN2CJsonStyle::Pretty);

		if (JsonOutput.IsEmpty())
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = TEXT("Failed to serialize to JSON");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			continue;
		}

		// Save individual JSON file
		FString SafeGraphName = FN2CPathUtils::GetTranslationFileStem(Item.TagInfo.GraphName);
		FString JsonPath = OutputPath / FString::Printf(TEXT("%s.json"), *SafeGraphName);

		if (FN2CTranslationArchive::SaveTextAtomic(JsonPath, JsonOutput))
		{
			Item.Status = EN2CBatchItemStatus::Completed;
			Result.SuccessCount++;
			FN2CLogger::Get().Log(
				FString::Printf(TEXT("Exported: %s"), *SafeGraphName),
				EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));

			// Record JSON export state in the graph state manager
			if (GraphGuid.IsValid())
			{
				// Make the output path relative to the project directory
				FString RelativeJsonPath = JsonPath;
				FString ProjectDir = FPaths::ProjectDir();
				if (RelativeJsonPath.StartsWith(ProjectDir))
				{
					RelativeJsonPath = RelativeJsonPath.Mid(ProjectDir.Len());
				}

				UN2CGraphStateManager::Get().SetJsonExportState(
					GraphGuid,
					Item.TagInfo.GraphName,
					FSoftObjectPath(Item.TagInfo.BlueprintPath),
					RelativeJsonPath,
					bMinifyJson
				);
			}
		}
		else
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = TEXT("Failed to save file");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Failed to save JSON file: %s"), *JsonPath),
				TEXT("BatchOrchestrator"));
		}
	}

	// Generate combined markdown file
	GenerateCombinedMarkdown(ExportItems, OutputPath);
	Result.CombinedMarkdownPath = OutputPath / TEXT("Combined_Blueprints.md");

	// Calculate duration
	Result.TotalTimeSeconds = static_cast<float>(FPlatformTime::Seconds() - StartTime);

	FN2CLogger::Get().Log(
		FString::Printf(TEXT("Batch JSON export complete. Success: %d, Failed: %d, Time: %.2fs"),
			Result.SuccessCount, Result.FailureCount, Result.TotalTimeSeconds),
		EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));

	// Broadcast completion
	OnJsonExportComplete.Broadcast(Result);

	return true;
}

void UN2CBatchTranslationOrchestrator::GenerateCombinedMarkdown(
	const TArray<FN2CBatchTranslationItem>& Items,
	const FString& OutputPath)
{
	FString MarkdownContent;

	// Header
	MarkdownContent += TEXT("# Combined Blueprint JSON Export\n\n");
	MarkdownContent += FString::Printf(TEXT("Generated: %s\n\n"), *FDateTime::Now().ToString());
	MarkdownContent += TEXT("This document contains serialized Unreal Engine Blueprint graphs in the NodeToCode JSON format. ");
	MarkdownContent += TEXT("Each graph represents visual scripting logic that can be understood and translated to code.\n\n");
	MarkdownContent += TEXT("---\n\n");

	// NodeToCode JSON Specification: the shared fragment every CodeGen prompt includes (card 0132)
	MarkdownContent += TEXT("## NodeToCode JSON Format Specification\n\n");
	MarkdownContent += UN2CSystemPromptManager::GetN2CJsonSpec();
	MarkdownContent += TEXT("\n\n");

	MarkdownContent += TEXT("---\n\n");

	// Blueprint Structure Overview
	MarkdownContent += TEXT("## Blueprint Structure Overview\n\n");
	MarkdownContent += TEXT("The following Blueprint assets and their graphs are included in this export:\n\n");

	// Group items by blueprint path
	TMap<FString, TArray<const FN2CBatchTranslationItem*>> BlueprintGroups;
	for (const FN2CBatchTranslationItem& Item : Items)
	{
		if (Item.Status == EN2CBatchItemStatus::Completed)
		{
			BlueprintGroups.FindOrAdd(Item.TagInfo.BlueprintPath).Add(&Item);
		}
	}

	// Output hierarchical structure
	for (const auto& Pair : BlueprintGroups)
	{
		// Extract just the asset name from the full path
		FString AssetName = FPaths::GetBaseFilename(Pair.Key);
		MarkdownContent += FString::Printf(TEXT("### `%s`\n\n"), *AssetName);
		MarkdownContent += FString::Printf(TEXT("**Asset Path:** `%s`\n\n"), *Pair.Key);
		MarkdownContent += TEXT("**Graphs:**\n");
		for (const FN2CBatchTranslationItem* Item : Pair.Value)
		{
			FString AnchorName = Item->TagInfo.GraphName.ToLower().Replace(TEXT(" "), TEXT("-"));
			MarkdownContent += FString::Printf(TEXT("- [%s](#%s)"),
				*Item->TagInfo.GraphName,
				*AnchorName);
			if (!Item->TagInfo.Tag.IsEmpty())
			{
				MarkdownContent += FString::Printf(TEXT(" *(Tag: %s)*"), *Item->TagInfo.Tag);
			}
			MarkdownContent += TEXT("\n");
		}
		MarkdownContent += TEXT("\n");
	}
	MarkdownContent += TEXT("---\n\n");

	// Table of contents for graphs
	MarkdownContent += TEXT("## Graph Contents\n\n");
	for (const FN2CBatchTranslationItem& Item : Items)
	{
		if (Item.Status == EN2CBatchItemStatus::Completed)
		{
			FString AnchorName = Item.TagInfo.GraphName.ToLower().Replace(TEXT(" "), TEXT("-"));
			MarkdownContent += FString::Printf(TEXT("- [%s](#%s)\n"),
				*Item.TagInfo.GraphName,
				*AnchorName);
		}
	}
	MarkdownContent += TEXT("\n---\n\n");

	// Each graph's JSON
	for (const FN2CBatchTranslationItem& Item : Items)
	{
		if (Item.Status != EN2CBatchItemStatus::Completed)
		{
			continue;
		}

		// Section header with blueprint and graph name
		MarkdownContent += FString::Printf(TEXT("## %s\n\n"), *Item.TagInfo.GraphName);
		MarkdownContent += FString::Printf(TEXT("**Blueprint:** `%s`\n\n"), *Item.TagInfo.BlueprintPath);

		if (!Item.TagInfo.Tag.IsEmpty() || !Item.TagInfo.Category.IsEmpty())
		{
			MarkdownContent += FString::Printf(TEXT("**Tag:** %s | **Category:** %s\n\n"),
				*Item.TagInfo.Tag,
				*Item.TagInfo.Category);
		}

		// Read the JSON file we saved
		FString SafeGraphName = FN2CPathUtils::GetTranslationFileStem(Item.TagInfo.GraphName);
		FString JsonPath = OutputPath / FString::Printf(TEXT("%s.json"), *SafeGraphName);
		FString JsonContent;

		if (FFileHelper::LoadFileToString(JsonContent, *JsonPath))
		{
			// JSON code block
			MarkdownContent += TEXT("```json\n");
			MarkdownContent += JsonContent;
			MarkdownContent += TEXT("\n```\n\n");
		}
		else
		{
			MarkdownContent += TEXT("*Failed to load JSON content*\n\n");
		}

		MarkdownContent += TEXT("---\n\n");
	}

	// Save combined markdown
	FString MarkdownPath = OutputPath / TEXT("Combined_Blueprints.md");
	if (!FN2CTranslationArchive::SaveTextAtomic(MarkdownPath, MarkdownContent))
	{
		FN2CLogger::Get().LogWarning(
			FString::Printf(TEXT("Failed to save combined markdown: %s"), *MarkdownPath),
			TEXT("BatchOrchestrator"));
	}
	else
	{
		FN2CLogger::Get().Log(
			FString::Printf(TEXT("Generated combined markdown: %s"), *MarkdownPath),
			EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
	}
}
