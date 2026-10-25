// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "Core/N2CNodeCollector.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Models/N2CJsonContract.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"
#include "Misc/Paths.h"
#include "Utils/N2CPathUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

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
	// Validate input
	if (TagInfos.IsEmpty())
	{
		FN2CLogger::Get().LogError(TEXT("Cannot start batch translation with empty TagInfos array"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Check if already in progress
	if (bBatchInProgress)
	{
		FN2CLogger::Get().LogWarning(TEXT("Batch translation already in progress"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Check if LLM module is already processing
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
	{
		FN2CLogger::Get().LogWarning(TEXT("LLM module is already processing a translation"), TEXT("BatchOrchestrator"));
		return false;
	}

	// Initialize LLM module if needed
	if (LLMModule && !LLMModule->IsInitialized())
	{
		if (!LLMModule->Initialize())
		{
			FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM module for batch translation"), TEXT("BatchOrchestrator"));
			return false;
		}
	}

	// Initialize batch state
	bBatchInProgress = true;
	bCancellationRequested = false;
	CurrentItemIndex = -1;
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
		FN2CLogger::Get().LogError(TEXT("No valid items to process in batch"), TEXT("BatchOrchestrator"));
		CleanupBatch();
		return false;
	}

	// Bind to LLM module's response delegate
	if (LLMModule && !bIsBoundToLLMModule)
	{
		LLMModule->OnTranslationResponseReceived.AddDynamic(
			this, &UN2CBatchTranslationOrchestrator::HandleTranslationResponse);
		bIsBoundToLLMModule = true;
	}

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
	FN2CBlueprint CapturedBlueprint;
	FString JsonPayload = CollectAndSerializeGraph(Item.CachedGraph.Get(), Item.CachedBlueprint.Get(), CapturedBlueprint);

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
		// The ProcessN2CJson that triggers OnTranslationResponseReceived; the translation is saved with this item's capture
		LLMModule->ProcessN2CJson(JsonPayload, CapturedBlueprint, FOnLLMResponseReceived());
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

void UN2CBatchTranslationOrchestrator::HandleTranslationResponse(const FN2CTranslationResponse& Response, bool bSuccess)
{
	if (!bBatchInProgress || CurrentItemIndex < 0 || CurrentItemIndex >= BatchItems.Num())
	{
		return;
	}

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

	// Generate batch summary
	GenerateBatchSummary();

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
	// Unbind from LLM module delegate
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (LLMModule && bIsBoundToLLMModule)
	{
		LLMModule->OnTranslationResponseReceived.RemoveDynamic(
			this, &UN2CBatchTranslationOrchestrator::HandleTranslationResponse);
		bIsBoundToLLMModule = false;
	}

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

	for (FN2CBatchTranslationItem& Item : BatchItems)
	{
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

		UEdGraph* Graph = FindGraphByGuid(Blueprint, GraphGuid);
		if (!Graph)
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = FString::Printf(TEXT("Graph not found with GUID: %s"), *Item.TagInfo.GraphGuid);
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

UEdGraph* UN2CBatchTranslationOrchestrator::FindGraphByGuid(UBlueprint* Blueprint, const FGuid& GraphGuid)
{
	if (!Blueprint || !GraphGuid.IsValid())
	{
		return nullptr;
	}

	// Search in UbergraphPages (event graphs)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	// Search in FunctionGraphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	// Search in MacroGraphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	// Search in DelegateSignatureGraphs
	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	return nullptr;
}

FString UN2CBatchTranslationOrchestrator::CollectAndSerializeGraph(UEdGraph* Graph, UBlueprint* Blueprint, FN2CBlueprint& OutCapture)
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
		FN2CLogger::Get().LogError(TEXT("Failed to generate N2CStruct"), TEXT("BatchOrchestrator"));
		return FString();
	}

	// Serialize to JSON (minified for LLM)
	FString JsonOutput = FN2CSerializer::ToJson(Capture.GetValue().Blueprint, EN2CJsonStyle::Condensed);
	OutCapture = MoveTemp(Capture.GetValue().Blueprint);

	return JsonOutput;
}

void UN2CBatchTranslationOrchestrator::CreateBatchOutputDirectory()
{
	// Get current timestamp
	FDateTime Now = FDateTime::Now();
	FString Timestamp = Now.ToString(TEXT("%Y-%m-%d-%H.%M.%S"));

	// Create folder name
	FString FolderName = FString::Printf(TEXT("BatchTranslation_%s"), *Timestamp);

	// Get base path
	FString BasePath = GetTranslationBasePath();

	BatchOutputPath = FPaths::Combine(BasePath, FolderName);

	if (!EnsureDirectoryExists(BatchOutputPath))
	{
		FN2CLogger::Get().LogError(
			FString::Printf(TEXT("Failed to create batch output directory: %s"), *BatchOutputPath),
			TEXT("BatchOrchestrator"));
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
	if (BatchOutputPath.IsEmpty())
	{
		return;
	}

	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	EN2CCodeLanguage TargetLanguage = Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;

	// Save each graph's files
	for (const FN2CGraphTranslation& Graph : Item.TranslationResponse.Graphs)
	{
		// Use TagInfo graph name for the folder (more reliable)
		FString GraphName = Item.TagInfo.GraphName;
		if (GraphName.IsEmpty())
		{
			GraphName = Graph.GraphName;
		}
		if (GraphName.IsEmpty())
		{
			continue;
		}

		// Names can come from the LLM response: use a safe stem for every path
		const FString Stem = FN2CPathUtils::GetTranslationFileStem(GraphName);
		if (Stem.IsEmpty())
		{
			continue;
		}

		// Create directory for this graph
		FString GraphDir = FPaths::Combine(BatchOutputPath, Stem);
		if (!FN2CPathUtils::IsUnderDirectoryResolved(GraphDir, BatchOutputPath))
		{
			FN2CLogger::Get().LogWarning(
				FString::Printf(TEXT("Skipping graph '%s': its folder would be outside the batch output folder"), *GraphName),
				TEXT("BatchOrchestrator"));
			continue;
		}
		if (!EnsureDirectoryExists(GraphDir))
		{
			FN2CLogger::Get().LogWarning(
				FString::Printf(TEXT("Failed to create graph directory: %s"), *GraphDir),
				TEXT("BatchOrchestrator"));
			continue;
		}

		// Save declaration file (C++ only)
		if (TargetLanguage == EN2CCodeLanguage::Cpp && !Graph.Code.GraphDeclaration.IsEmpty())
		{
			FString HeaderPath = FPaths::Combine(GraphDir, Stem + TEXT(".h"));
			if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
			{
				FN2CLogger::Get().LogWarning(
					FString::Printf(TEXT("Failed to save header file: %s"), *HeaderPath),
					TEXT("BatchOrchestrator"));
			}
		}

		// Save implementation file with appropriate extension
		if (!Graph.Code.GraphImplementation.IsEmpty())
		{
			FString Extension = GetFileExtensionForLanguage();
			FString ImplPath = FPaths::Combine(GraphDir, Stem + Extension);
			if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath))
			{
				FN2CLogger::Get().LogWarning(
					FString::Printf(TEXT("Failed to save implementation file: %s"), *ImplPath),
					TEXT("BatchOrchestrator"));
			}
		}

		// Save implementation notes
		if (!Graph.Code.ImplementationNotes.IsEmpty())
		{
			FString NotesPath = FPaths::Combine(GraphDir, Stem + TEXT("_Notes.txt"));
			if (!FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath))
			{
				FN2CLogger::Get().LogWarning(
					FString::Printf(TEXT("Failed to save notes file: %s"), *NotesPath),
					TEXT("BatchOrchestrator"));
			}
		}

		// Save the N2C Blueprint JSON (the serialized graph that was sent to the LLM)
		if (!Item.CachedJsonPayload.IsEmpty())
		{
			FString JsonPath = FPaths::Combine(GraphDir, Stem + TEXT(".json"));
			if (!FFileHelper::SaveStringToFile(Item.CachedJsonPayload, *JsonPath))
			{
				FN2CLogger::Get().LogWarning(
					FString::Printf(TEXT("Failed to save JSON file: %s"), *JsonPath),
					TEXT("BatchOrchestrator"));
			}
		}

		// Record translation state in the graph state manager
		FGuid GraphGuid;
		FGuid::Parse(Item.TagInfo.GraphGuid, GraphGuid);

		if (GraphGuid.IsValid())
		{
			// Build translation summary
			FN2CTranslationSummary Summary;

			// Get first line of declaration as preview
			if (!Graph.Code.GraphDeclaration.IsEmpty())
			{
				int32 NewlineIndex;
				if (Graph.Code.GraphDeclaration.FindChar('\n', NewlineIndex))
				{
					Summary.DeclarationPreview = Graph.Code.GraphDeclaration.Left(NewlineIndex).TrimStartAndEnd();
				}
				else
				{
					Summary.DeclarationPreview = Graph.Code.GraphDeclaration.TrimStartAndEnd();
				}
			}

			// Count implementation lines
			if (!Graph.Code.GraphImplementation.IsEmpty())
			{
				int32 LineCount = 1;
				for (const TCHAR& Char : Graph.Code.GraphImplementation)
				{
					if (Char == '\n')
					{
						LineCount++;
					}
				}
				Summary.ImplementationLines = LineCount;
			}

			Summary.bHasNotes = !Graph.Code.ImplementationNotes.IsEmpty();

			// Get provider info
			FString Provider = Settings ? UEnum::GetValueAsString(Settings->Provider) : TEXT("Unknown");
			FString Model = Settings ? Settings->GetActiveModel() : TEXT("Unknown");
			FString Language = Settings ? UEnum::GetValueAsString(Settings->TargetLanguage) : TEXT("Cpp");

			// Make the output path relative to the project directory
			FString RelativeGraphDir = GraphDir;
			FString ProjectDir = FPaths::ProjectDir();
			if (RelativeGraphDir.StartsWith(ProjectDir))
			{
				RelativeGraphDir = RelativeGraphDir.Mid(ProjectDir.Len());
			}

			// Set translation state
			UN2CGraphStateManager::Get().SetTranslationState(
				GraphGuid,
				GraphName,
				FSoftObjectPath(Item.TagInfo.BlueprintPath),
				RelativeGraphDir,
				Provider,
				Model,
				Language,
				Summary
			);

			FN2CLogger::Get().Log(
				FString::Printf(TEXT("Recorded translation state for graph: %s"), *GraphName),
				EN2CLogSeverity::Info, TEXT("BatchOrchestrator"));
		}
	}
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
	SummaryJson->SetObjectField(TEXT("statistics"), StatsJson);

	// Settings
	TSharedPtr<FJsonObject> SettingsJson = MakeShared<FJsonObject>();
	if (Settings)
	{
		SettingsJson->SetStringField(TEXT("provider"), UEnum::GetValueAsString(Settings->Provider));
		SettingsJson->SetStringField(TEXT("model"), Settings->GetActiveModel());
		SettingsJson->SetStringField(TEXT("language"), UEnum::GetValueAsString(Settings->TargetLanguage));
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

		if (Item.Status == EN2CBatchItemStatus::Completed)
		{
			TSharedPtr<FJsonObject> TokensJson = MakeShared<FJsonObject>();
			TokensJson->SetNumberField(TEXT("input"), Item.TranslationResponse.Usage.InputTokens);
			TokensJson->SetNumberField(TEXT("output"), Item.TranslationResponse.Usage.OutputTokens);
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
	if (!FFileHelper::SaveStringToFile(SummaryContent, *SummaryPath))
	{
		FN2CLogger::Get().LogWarning(
			FString::Printf(TEXT("Failed to save batch summary: %s"), *SummaryPath),
			TEXT("BatchOrchestrator"));
	}
}

FString UN2CBatchTranslationOrchestrator::GetTranslationBasePath() const
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	FString BasePath;

	if (Settings && !Settings->CustomTranslationOutputDirectory.Path.IsEmpty())
	{
		BasePath = Settings->CustomTranslationOutputDirectory.Path;
	}
	else
	{
		BasePath = FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
	}

	return BasePath;
}

FString UN2CBatchTranslationOrchestrator::GetFileExtensionForLanguage() const
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	EN2CCodeLanguage Language = Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;

	switch (Language)
	{
		case EN2CCodeLanguage::Cpp:
			return TEXT(".cpp");
		case EN2CCodeLanguage::Python:
			return TEXT(".py");
		case EN2CCodeLanguage::JavaScript:
			return TEXT(".js");
		case EN2CCodeLanguage::CSharp:
			return TEXT(".cs");
		case EN2CCodeLanguage::Swift:
			return TEXT(".swift");
		case EN2CCodeLanguage::Pseudocode:
			return TEXT(".md");
		default:
			return TEXT(".txt");
	}
}

bool UN2CBatchTranslationOrchestrator::EnsureDirectoryExists(const FString& DirectoryPath) const
{
	if (!FPaths::DirectoryExists(DirectoryPath))
	{
		return FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
	}
	return true;
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

	// Create output directory with timestamp
	FDateTime Now = FDateTime::Now();
	FString Timestamp = Now.ToString(TEXT("%Y-%m-%d-%H.%M.%S"));
	FString OutputPath = GetTranslationBasePath() / FString::Printf(TEXT("BatchJson_%s"), *Timestamp);

	if (!EnsureDirectoryExists(OutputPath))
	{
		FN2CLogger::Get().LogError(
			FString::Printf(TEXT("Failed to create output directory: %s"), *OutputPath),
			TEXT("BatchOrchestrator"));
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

		UEdGraph* Graph = FindGraphByGuid(Blueprint, GraphGuid);
		if (!Graph)
		{
			Item.Status = EN2CBatchItemStatus::Failed;
			Item.ErrorMessage = TEXT("Graph not found");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Graph not found with GUID: %s"), *Item.TagInfo.GraphGuid),
				TEXT("BatchOrchestrator"));
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
			Item.ErrorMessage = TEXT("Failed to serialize");
			Result.FailureCount++;
			Result.FailedGraphNames.Add(Item.TagInfo.GraphName);
			FN2CLogger::Get().LogError(
				FString::Printf(TEXT("Failed to generate N2CStruct for: %s"), *Item.TagInfo.GraphName),
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

		if (FFileHelper::SaveStringToFile(JsonOutput, *JsonPath))
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

	// NodeToCode JSON Specification
	MarkdownContent += TEXT("## NodeToCode JSON Format Specification\n\n");
	MarkdownContent += TEXT("The JSON format below (\"N2C JSON\") represents Unreal Engine Blueprint graphs as structured data. ");
	MarkdownContent += TEXT("This format captures the complete logic of Blueprint visual scripts including nodes, pins, connections, and execution flow.\n\n");

	MarkdownContent += TEXT("### Understanding the Format\n\n");
	MarkdownContent += TEXT("**Blueprints** in Unreal Engine are visual scripting assets that contain one or more **graphs**. ");
	MarkdownContent += TEXT("Each graph contains **nodes** (representing operations like function calls, variable access, or flow control) ");
	MarkdownContent += TEXT("connected by **pins** (typed input/output connectors). The N2C JSON preserves this structure:\n\n");
	MarkdownContent += TEXT("- **Execution Flow**: Nodes connected via `Exec` pins run in the order of their exec wires. The `flows.execution` array has one entry per wire, from the exec output pin to the exec input pin it runs (e.g., `N1.P3->N2.P5`): the source pin tells which output fired (a Branch's True or False, a Switch case, a Sequence's then_0 or then_1, a loop's Loop Body or Completed) and the target pin which input runs (a Gate's Enter, Open or Close, a Timeline's Play or Reverse). Documents with version 1.0.0 list node pairs (`N1->N2`) instead.\n");
	MarkdownContent += TEXT("- **Data Flow**: Data passes between nodes via typed pins. The `flows.data` object maps each output pin to the input pin it feeds (e.g., `N1.P2` connects to `N2.P1`), or to an array of input pins when one output feeds several (e.g., `N3.P4` feeds `[\"N4.P2\", \"N5.P3\"]`). Every connected data input appears once as a target.\n");
	MarkdownContent += TEXT("- **Nodes That Never Run**: A node may carry `\"unused\": true` (a pure node whose outputs nothing that runs reads), `\"unreachable\": true` (a node with exec pins that no event or entry reaches) or `\"disabled\": true` (disabled in the editor, not compiled; when it has one exec input and one exec output, `flows.execution` already links its predecessor straight to its successor). Blueprint never runs these nodes: do not translate them or the values they compute, and list them in the implementation notes. The editor's greyed-out placeholder events (a new Blueprint's unwired BeginPlay, ActorBeginOverlap and Tick) are left out.\n");
	MarkdownContent += TEXT("- **Return Values**: A function returns only what is wired into the input pins of its `FunctionResult` node(s), or those pins' `default_value`; never a value that is not wired into a `FunctionResult` pin.\n");
	MarkdownContent += TEXT("- **Node Types**: Each node has a `type` indicating its purpose (CallFunction, VariableGet, VariableSet, Event, Branch, ForLoop, etc.).\n");
	MarkdownContent += TEXT("- **Pin Types**: Pins have types like Exec, Boolean, Integer, Float, String, Object, Struct, etc.\n\n");

	MarkdownContent += TEXT("### JSON Structure Reference\n\n");
	MarkdownContent += TEXT("```json\n");
	MarkdownContent += TEXT("{\n");
	MarkdownContent += FString::Printf(TEXT("  \"version\": \"%s\",\n"), N2CJsonContract::Version);
	MarkdownContent += TEXT("  \"metadata\": {\n");
	MarkdownContent += TEXT("    \"Name\": \"BlueprintName\",\n");
	MarkdownContent += TEXT("    \"BlueprintType\": \"Normal | Const | MacroLibrary | Interface | LevelScript | FunctionLibrary\",\n");
	MarkdownContent += TEXT("    \"BlueprintClass\": \"ClassName\"\n");
	MarkdownContent += TEXT("  },\n");
	MarkdownContent += TEXT("  \"graphs\": [\n");
	MarkdownContent += TEXT("    {\n");
	MarkdownContent += TEXT("      \"name\": \"GraphName\",\n");
	MarkdownContent += TEXT("      \"graph_type\": \"Function | EventGraph | Macro | Composite | Construction\",\n");
	MarkdownContent += TEXT("      \"owner_blueprint\": \"OtherBlueprint (optional: only on a graph from another Blueprint)\",\n");
	MarkdownContent += TEXT("      \"nodes\": [\n");
	MarkdownContent += TEXT("        {\n");
	MarkdownContent += TEXT("          \"id\": \"N1\",\n");
	MarkdownContent += TEXT("          \"type\": \"CallFunction | VariableGet | VariableSet | Event | Branch | ...\",\n");
	MarkdownContent += TEXT("          \"name\": \"Node Display Name\",\n");
	MarkdownContent += TEXT("          \"member_parent\": \"OwningClass (optional)\",\n");
	MarkdownContent += TEXT("          \"member_name\": \"FunctionOrVariableName (optional)\",\n");
	MarkdownContent += TEXT("          \"pure\": false,\n");
	MarkdownContent += TEXT("          \"latent\": false,\n");
	MarkdownContent += TEXT("          \"input_pins\": [...],\n");
	MarkdownContent += TEXT("          \"output_pins\": [...]\n");
	MarkdownContent += TEXT("        }\n");
	MarkdownContent += TEXT("      ],\n");
	MarkdownContent += TEXT("      \"flows\": {\n");
	MarkdownContent += TEXT("        \"execution\": [\"N1.P3->N2.P5\", \"N2.P7->N3.P9\"],\n");
	MarkdownContent += TEXT("        \"data\": { \"N1.P2\": \"N2.P1\", \"N3.P4\": [\"N4.P2\", \"N5.P3\"] }\n");
	MarkdownContent += TEXT("      }\n");
	MarkdownContent += TEXT("    }\n");
	MarkdownContent += TEXT("  ],\n");
	MarkdownContent += TEXT("  \"structs\": [...],\n");
	MarkdownContent += TEXT("  \"enums\": [...]\n");
	MarkdownContent += TEXT("}\n");
	MarkdownContent += TEXT("```\n\n");

	MarkdownContent += TEXT("### Pin Object Structure\n\n");
	MarkdownContent += TEXT("Each pin in `input_pins` or `output_pins` has:\n\n");
	MarkdownContent += TEXT("| Field | Description |\n");
	MarkdownContent += TEXT("|-------|-------------|\n");
	MarkdownContent += TEXT("| `id` | Pin identifier (e.g., \"P1\") |\n");
	MarkdownContent += TEXT("| `name` | Display name of the pin |\n");
	MarkdownContent += TEXT("| `type` | Pin type: Exec, Boolean, Integer, Float, String, Object, Struct, etc. |\n");
	MarkdownContent += TEXT("| `sub_type` | Additional type info (e.g., struct/class name) |\n");
	MarkdownContent += TEXT("| `default_value` | Literal value if not connected |\n");
	MarkdownContent += TEXT("| `connected` | Whether this pin is linked to another |\n");
	MarkdownContent += TEXT("| `is_reference` | Passed by reference |\n");
	MarkdownContent += TEXT("| `is_array`, `is_map`, `is_set` | Container type flags |\n\n");

	MarkdownContent += TEXT("### Common Node Types\n\n");
	MarkdownContent += TEXT("| Type | Description |\n");
	MarkdownContent += TEXT("|------|-------------|\n");
	MarkdownContent += TEXT("| `Event` | Entry point (BeginPlay, Tick, custom events) |\n");
	MarkdownContent += TEXT("| `CallFunction` | Function call on an object or static library |\n");
	MarkdownContent += TEXT("| `VariableGet` | Read a variable value |\n");
	MarkdownContent += TEXT("| `VariableSet` | Write a variable value |\n");
	MarkdownContent += TEXT("| `Branch` | If/else conditional |\n");
	MarkdownContent += TEXT("| `Sequence` | Execute multiple paths in order |\n");
	MarkdownContent += TEXT("| `ForLoop` | For loop with index |\n");
	MarkdownContent += TEXT("| `ForEachLoop` | Iterate over array elements |\n");
	MarkdownContent += TEXT("| `Cast` | Type cast to specific class |\n");
	MarkdownContent += TEXT("| `MakeStruct` | Construct a struct value |\n");
	MarkdownContent += TEXT("| `BreakStruct` | Extract struct members |\n\n");

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
	if (!FFileHelper::SaveStringToFile(MarkdownContent, *MarkdownPath))
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
