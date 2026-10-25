// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CMainWindow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Models/N2CStyle.h"
#include "Core/Widgets/SN2CTagManager.h"
#include "Core/N2CDesignTokens.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CGraphStateManager.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/N2CResponseParserBase.h"
#include "Async/Async.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CNodeCollector.h"
#include "Core/N2CCapture.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Layout/Visibility.h"
#include "Styling/AppStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "Core/N2CSettings.h"
#include "Utils/N2CPathUtils.h"

#define LOCTEXT_NAMESPACE "SN2CMainWindow"

void SN2CMainWindow::Construct(const FArguments& InArgs)
{
	OnBatchCompleteDelegate = InArgs._OnBatchComplete;
	OnTranslationCompleteDelegate = InArgs._OnTranslationComplete;
	bShowTranslationViewer = false;
	bShowBatchProgress = false;
	bIsSingleTranslationInProgress = false;

	ChildSlot
	[
		SNew(SOverlay)
		// Base layer - Tag Manager
		+ SOverlay::Slot()
		[
			SAssignNew(TagManager, SN2CTagManager)
			.ShowSearchBar(InArgs._ShowSearchBar)
			.ShowActionBar(InArgs._ShowActionBar)
			.OnTagSelected(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleTagSelected))
			.OnCategorySelected(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleCategorySelected))
			.OnSelectionChanged(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleSelectionChanged))
			.OnGraphDoubleClicked(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleGraphDoubleClicked))
			.OnBatchTranslateRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleBatchTranslateRequested))
			.OnExportJsonRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleExportJsonRequested))
			.OnRemoveSelectedRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleRemoveSelectedRequested))
			.OnSingleTranslateRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleSingleTranslateRequested))
			.OnSingleJsonExportRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleSingleJsonExportRequested))
			.OnViewTranslationRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HandleViewTranslationRequested))
		]

		// Translation Viewer overlay layer
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.Visibility(this, &SN2CMainWindow::GetTranslationOverlayVisibility)
			.BlurStrength(2.0f)
			[
				SNew(SBorder)
				.BorderImage(&N2CStyle::GetPanelBorderBrush())
				.BorderBackgroundColor(UIBindAlpha(&FN2CUIColors::BgPanelDarker, 0.30f))
				.Padding(FN2CSpacing::XXL)
				[
					SAssignNew(TranslationViewer, SN2CTranslationViewer)
					.OnCloseRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::HideTranslationViewer))
				]
			]
		]

		// Batch Progress modal layer with full-screen overlay background
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.Visibility(this, &SN2CMainWindow::GetBatchProgressVisibility)
			.BorderImage(&N2CStyle::GetPanelBorderBrush())
			.BorderBackgroundColor(UIBindAlpha(&FN2CUIColors::BgPanelDarker, 0.30f))
			.Padding(0.0f)
			[
				// Center the modal within the full-screen overlay
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SAssignNew(BatchProgressModal, SN2CBatchProgressModal)
					.OnCancelRequested(FSimpleDelegate::CreateLambda([this]()
					{
						UN2CBatchTranslationOrchestrator::Get().CancelBatch();
						HideBatchProgress();
					}))
				]
			]
		]
	];

	// Bind to batch orchestrator native delegates (for C++ Slate widgets)
	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();

	// Store weak reference to self for safe lambda captures (renamed to avoid shadowing)
	TWeakPtr<SN2CMainWindow> WeakSelf = SharedThis(this);

	BatchItemCompleteHandle = Orchestrator.OnItemCompleteNative.AddLambda(
		[WeakSelf](const FN2CTagInfo& TagInfo, const FN2CTranslationResponse& Response, bool bSuccess, int32 ItemIndex, int32 TotalCount)
		{
			if (TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin())
			{
				StrongSelf->OnBatchItemComplete(TagInfo, Response, bSuccess, ItemIndex, TotalCount);
			}
		}
	);

	BatchCompleteHandle = Orchestrator.OnBatchCompleteNative.AddLambda(
		[WeakSelf](const FN2CBatchTranslationResult& Result)
		{
			if (TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin())
			{
				StrongSelf->OnBatchComplete(Result);
			}
		}
	);

	BatchProgressHandle = Orchestrator.OnProgressNative.AddLambda(
		[WeakSelf](int32 CurrentIndex, int32 TotalCount, const FString& GraphName)
		{
			if (TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin())
			{
				StrongSelf->OnBatchProgress(CurrentIndex, TotalCount, GraphName);
			}
		}
	);

	// Subscribe to overlay translation requests
	OverlayTranslationRequestHandle = FN2CEditorIntegration::Get().OnOverlayTranslationRequested.AddLambda(
		[WeakSelf](const FGuid& GraphGuid, const FString& GraphName, const FString& BlueprintPath)
		{
			if (TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin())
			{
				StrongSelf->HandleOverlayTranslationRequest(GraphGuid, GraphName, BlueprintPath);
			}
		}
	);
}

SN2CMainWindow::~SN2CMainWindow()
{
	// Unbind from batch orchestrator native delegates
	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();

	if (BatchItemCompleteHandle.IsValid())
	{
		Orchestrator.OnItemCompleteNative.Remove(BatchItemCompleteHandle);
	}
	if (BatchCompleteHandle.IsValid())
	{
		Orchestrator.OnBatchCompleteNative.Remove(BatchCompleteHandle);
	}
	if (BatchProgressHandle.IsValid())
	{
		Orchestrator.OnProgressNative.Remove(BatchProgressHandle);
	}

	// Unbind from overlay translation requests
	if (OverlayTranslationRequestHandle.IsValid())
	{
		FN2CEditorIntegration::Get().OnOverlayTranslationRequested.Remove(OverlayTranslationRequestHandle);
	}
}

void SN2CMainWindow::RefreshData()
{
	if (TagManager.IsValid())
	{
		TagManager->RefreshData();
	}
}

TArray<FN2CTagInfo> SN2CMainWindow::GetSelectedGraphs() const
{
	if (TagManager.IsValid())
	{
		return TagManager->GetSelectedGraphs();
	}
	return TArray<FN2CTagInfo>();
}

int32 SN2CMainWindow::GetSelectedCount() const
{
	if (TagManager.IsValid())
	{
		return TagManager->GetSelectedCount();
	}
	return 0;
}

void SN2CMainWindow::ShowTranslationViewer(const FN2CTagInfo& GraphInfo)
{
	if (TranslationViewer.IsValid())
	{
		if (TranslationViewer->LoadTranslation(GraphInfo))
		{
			bShowTranslationViewer = true;
		}
	}
}

void SN2CMainWindow::ShowTranslationViewer(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent)
{
	if (TranslationViewer.IsValid())
	{
		TranslationViewer->LoadTranslation(Translation, GraphName, JsonContent);
		bShowTranslationViewer = true;
	}
}

void SN2CMainWindow::HideTranslationViewer()
{
	bShowTranslationViewer = false;
	if (TranslationViewer.IsValid())
	{
		TranslationViewer->Clear();
	}
}

void SN2CMainWindow::ShowBatchProgress()
{
	bShowBatchProgress = true;
}

void SN2CMainWindow::HideBatchProgress()
{
	bShowBatchProgress = false;
	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->Reset();
	}
}

// ==================== Tag Manager Event Handlers ====================

void SN2CMainWindow::HandleTagSelected()
{
	// Tag selection is handled internally by tag manager
}

void SN2CMainWindow::HandleCategorySelected()
{
	// Category selection is handled internally by tag manager
}

void SN2CMainWindow::HandleSelectionChanged()
{
	// Selection changes are reflected in the action bar automatically
}

void SN2CMainWindow::HandleGraphDoubleClicked()
{
	if (TagManager.IsValid())
	{
		FN2CTagInfo Graph = TagManager->GetDoubleClickedGraph();
		if (!Graph.GraphGuid.IsEmpty())
		{
			NavigateToGraph(Graph);
		}
	}
}

// ==================== Single Graph Action Handlers ====================

void SN2CMainWindow::HandleSingleTranslateRequested()
{
	if (TagManager.IsValid())
	{
		FN2CTagInfo Graph = TagManager->GetTranslateRequestedGraph();
		if (!Graph.GraphGuid.IsEmpty())
		{
			TranslateSingleGraph(Graph);
		}
	}
}

void SN2CMainWindow::HandleSingleJsonExportRequested()
{
	if (TagManager.IsValid())
	{
		FN2CTagInfo Graph = TagManager->GetJsonExportRequestedGraph();
		if (!Graph.GraphGuid.IsEmpty())
		{
			ExportSingleGraphToJson(Graph);
		}
	}
}

void SN2CMainWindow::HandleViewTranslationRequested()
{
	if (TagManager.IsValid())
	{
		FN2CTagInfo Graph = TagManager->GetViewTranslationRequestedGraph();
		if (!Graph.GraphGuid.IsEmpty())
		{
			ShowTranslationViewer(Graph);
		}
	}
}

// ==================== Batch Action Handlers ====================

void SN2CMainWindow::HandleBatchTranslateRequested()
{
	TArray<FN2CTagInfo> SelectedGraphs = GetSelectedGraphs();
	if (SelectedGraphs.Num() == 0)
	{
		return;
	}

	// Check if already translating
	if (FN2CEditorIntegration::Get().IsAnyTranslationInProgress())
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CMainWindow] Translation already in progress"));
		return;
	}

	// Set global translation state
	FN2CEditorIntegration::Get().SetTranslationInProgress(true);

	// Initialize and show the batch progress modal
	TArray<FString> GraphNames;
	for (const FN2CTagInfo& Graph : SelectedGraphs)
	{
		GraphNames.Add(Graph.GraphName);
	}

	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->Initialize(GraphNames);
	}
	ShowBatchProgress();

	// Start the batch translation. It returns false without calling OnBatchComplete when the module is busy,
	// fails to initialize or no item resolves, so roll the UI state back here.
	if (!UN2CBatchTranslationOrchestrator::Get().StartBatchTranslation(SelectedGraphs))
	{
		UE_LOG(LogNodeToCode, Error, TEXT("[SN2CMainWindow] Batch translation did not start (busy, LLM module failed to initialize, or no graph could be resolved)"));
		FN2CEditorIntegration::Get().SetTranslationInProgress(false);
		HideBatchProgress();
		FNotificationInfo Info(NSLOCTEXT("NodeToCode", "BatchDidNotStart", "Batch translation did not start. Check the provider settings and the Output Log."));
		Info.ExpireDuration = 6.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

void SN2CMainWindow::HandleExportJsonRequested()
{
	TArray<FN2CTagInfo> SelectedGraphs = GetSelectedGraphs();
	if (SelectedGraphs.Num() == 0)
	{
		return;
	}

	// Get minify setting from tag manager
	bool bMinify = TagManager.IsValid() ? TagManager->IsMinifyJsonEnabled() : false;

	// Perform batch JSON export
	FN2CBatchJsonExportResult Result;
	UN2CBatchTranslationOrchestrator::Get().BatchExportJson(SelectedGraphs, Result, bMinify);
}

void SN2CMainWindow::HandleRemoveSelectedRequested()
{
	TArray<FN2CTagInfo> SelectedGraphs = GetSelectedGraphs();
	if (SelectedGraphs.Num() == 0)
	{
		return;
	}

	// Remove tags from all selected graphs: one save and one reload broadcast for the whole selection
	UN2CGraphStateManager& StateManager = UN2CGraphStateManager::Get();
	{
		FN2CGraphStateBulkUpdate Bulk(StateManager);
		for (const FN2CTagInfo& Graph : SelectedGraphs)
		{
			FGuid GraphGuid;
			if (FGuid::Parse(Graph.GraphGuid, GraphGuid))
			{
				StateManager.RemoveTag(GraphGuid, Graph.Tag, Graph.Category);
			}
		}
	}

	// Refresh the UI
	RefreshData();
}

// ==================== Backend Integration ====================

void SN2CMainWindow::TranslateSingleGraph(const FN2CTagInfo& Graph)
{
	// One path for row and overlay translations: it checks and sets both in-progress flags, initializes the
	// LLM module, shows progress, and clears the flags on every success and error path.
	FGuid GraphGuid;
	if (!FGuid::Parse(Graph.GraphGuid, GraphGuid))
	{
		UE_LOG(LogNodeToCode, Error, TEXT("[SN2CMainWindow] Invalid graph GUID for row translation: %s"), *Graph.GraphGuid);
		return;
	}
	HandleOverlayTranslationRequest(GraphGuid, Graph.GraphName, Graph.BlueprintPath);
}

void SN2CMainWindow::ExportSingleGraphToJson(const FN2CTagInfo& Graph)
{
	// Load the blueprint
	FSoftObjectPath BlueprintPath(Graph.BlueprintPath);
	UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintPath.TryLoad());
	if (!Blueprint)
	{
		return;
	}

	// Find the graph
	FGuid GraphGuid;
	if (!FGuid::Parse(Graph.GraphGuid, GraphGuid))
	{
		return;
	}

	UEdGraph* EdGraph = FindGraphByGuid(Blueprint, GraphGuid);
	if (!EdGraph)
	{
		return;
	}

	// Collect and translate nodes
	TArray<UK2Node*> CollectedNodes;
	FN2CEditorIntegration::Get().CollectNodesFromGraph(EdGraph, CollectedNodes);

	FN2CBlueprint N2CBlueprint;
	FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprint(CollectedNodes, N2CBlueprint);

	bool bMinify = TagManager.IsValid() ? TagManager->IsMinifyJsonEnabled() : false;
	FString JsonContent = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(N2CBlueprint, !bMinify);

	// Show in translation viewer (JSON tab)
	if (TranslationViewer.IsValid())
	{
		TranslationViewer->SetJsonContent(JsonContent, Graph.GraphName);
		bShowTranslationViewer = true;
	}
}

void SN2CMainWindow::NavigateToGraph(const FN2CTagInfo& Graph)
{
	// Load the blueprint
	FSoftObjectPath BlueprintPath(Graph.BlueprintPath);
	UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintPath.TryLoad());
	if (!Blueprint)
	{
		return;
	}

	// Find the graph
	FGuid GraphGuid;
	if (!FGuid::Parse(Graph.GraphGuid, GraphGuid))
	{
		return;
	}

	UEdGraph* EdGraph = FindGraphByGuid(Blueprint, GraphGuid);
	if (!EdGraph)
	{
		return;
	}

	// Open the blueprint editor
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);

	// Open the specific graph tab in the Blueprint editor
	// Use FKismetEditorUtilities to open and focus the graph
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(EdGraph);
}

UEdGraph* SN2CMainWindow::FindGraphByGuid(UBlueprint* Blueprint, const FGuid& GraphGuid)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Search UbergraphPages
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	// Search FunctionGraphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	// Search MacroGraphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	// Search DelegateSignatureGraphs
	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			return Graph;
		}
	}

	return nullptr;
}

// ==================== Batch Translation Callbacks ====================

void SN2CMainWindow::OnBatchItemComplete(const FN2CTagInfo& TagInfo, const FN2CTranslationResponse& Response,
	bool bSuccess, int32 ItemIndex, int32 TotalCount)
{
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CMainWindow] OnBatchItemComplete: %s (success=%s, item %d/%d)"),
		*TagInfo.GraphName, bSuccess ? TEXT("true") : TEXT("false"), ItemIndex + 1, TotalCount);

	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->MarkItemComplete(TagInfo.GraphName, bSuccess,
			bSuccess ? FString() : Response.Error.ToDisplayString());
	}
}

void SN2CMainWindow::OnBatchComplete(const FN2CBatchTranslationResult& Result)
{
	// Clear global translation state
	FN2CEditorIntegration::Get().SetTranslationInProgress(false);

	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->SetResult(Result);
	}

	// Refresh the tag manager data to update bHasTranslation state on list items
	// This makes the View button become interactable for newly translated graphs
	RefreshData();

	// Fire external delegate
	OnBatchCompleteDelegate.ExecuteIfBound(Result);
}

void SN2CMainWindow::OnBatchProgress(int32 CurrentIndex, int32 TotalCount, const FString& GraphName)
{
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CMainWindow] OnBatchProgress: %s (item %d/%d)"),
		*GraphName, CurrentIndex + 1, TotalCount);

	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->SetProgress(CurrentIndex, TotalCount, GraphName);
	}
}

// ==================== Overlay Translation Handling ====================

void SN2CMainWindow::HandleOverlayTranslationRequest(const FGuid& GraphGuid, const FString& GraphName, const FString& BlueprintPath)
{
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CMainWindow] HandleOverlayTranslationRequest: %s (GUID=%s)"),
		*GraphName, *GraphGuid.ToString());

	// Check if already translating
	if (bIsSingleTranslationInProgress || UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress())
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CMainWindow] Translation already in progress, ignoring overlay request"));
		return;
	}

	// Set global translation state to block all overlays
	FN2CEditorIntegration::Get().SetTranslationInProgress(true);

	// Initialize the batch progress modal for a single item
	TArray<FString> GraphNames;
	GraphNames.Add(GraphName);
	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->Initialize(GraphNames);
	}
	ShowBatchProgress();

	// Store pending graph info (we need it for the callback)
	PendingSingleTranslateGraph.GraphGuid = GraphGuid.ToString();
	PendingSingleTranslateGraph.GraphName = GraphName;
	PendingSingleTranslateGraph.BlueprintPath = BlueprintPath;
	bIsSingleTranslationInProgress = true;

	// Load the blueprint and graph
	FSoftObjectPath BPPath(BlueprintPath);
	UBlueprint* Blueprint = Cast<UBlueprint>(BPPath.TryLoad());
	if (!Blueprint)
	{
		UE_LOG(LogNodeToCode, Error, TEXT("[SN2CMainWindow] Failed to load Blueprint: %s"), *BlueprintPath);
		FN2CEditorIntegration::Get().SetTranslationInProgress(false);
		bIsSingleTranslationInProgress = false;
		HideBatchProgress();
		return;
	}

	UEdGraph* EdGraph = FindGraphByGuid(Blueprint, GraphGuid);
	if (!EdGraph)
	{
		UE_LOG(LogNodeToCode, Error, TEXT("[SN2CMainWindow] Failed to find graph with GUID: %s"), *GraphGuid.ToString());
		FN2CEditorIntegration::Get().SetTranslationInProgress(false);
		bIsSingleTranslationInProgress = false;
		HideBatchProgress();
		return;
	}

	// Collect and translate nodes
	TArray<UK2Node*> CollectedNodes;
	FN2CEditorIntegration::Get().CollectNodesFromGraph(EdGraph, CollectedNodes);

	FN2CBlueprint N2CBlueprint;
	FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprint(CollectedNodes, N2CBlueprint);

	FString JsonContent = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(N2CBlueprint, false);
	if (JsonContent.IsEmpty())
	{
		UE_LOG(LogNodeToCode, Error, TEXT("[SN2CMainWindow] Failed to serialize graph to JSON"));
		FN2CEditorIntegration::Get().SetTranslationInProgress(false);
		bIsSingleTranslationInProgress = false;
		HideBatchProgress();
		return;
	}

	// Update progress modal to show processing
	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->SetProgress(0, 1, GraphName);
	}

	// Store weak reference for safe lambda capture
	TWeakPtr<SN2CMainWindow> WeakSelf = SharedThis(this);
	FString CapturedGraphName = GraphName;

	// Send to LLM
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (LLMModule && LLMModule->Initialize())
	{
		// Saved with this graph's capture, whatever is captured while the request is in flight
		LLMModule->ProcessN2CJson(JsonContent, N2CBlueprint, FOnLLMResponseReceived::CreateLambda(
			[WeakSelf, CapturedGraphName](const FString& Response)
			{
				// Parse response (can be done on any thread)
				FN2CTranslationResponse TranslationResponse;
				bool bSuccess = false;

				TScriptInterface<IN2CLLMService> ActiveService = UN2CLLMModule::Get()->GetActiveService();
				if (ActiveService.GetInterface())
				{
					UN2CResponseParserBase* Parser = ActiveService->GetResponseParser();
					if (Parser && Parser->ParseLLMResponse(Response, TranslationResponse))
					{
						bSuccess = true;
						UE_LOG(LogNodeToCode, Log, TEXT("[SN2CMainWindow] Overlay translation completed successfully"));
					}
				}
				if (!bSuccess)
				{
					// The typed error (HTTP status, provider message) for the modal
					const FN2CLLMConfig& ModuleConfig = UN2CLLMModule::Get()->GetConfig();
					UN2CLLMModule::FillTranslationError(TranslationResponse, ModuleConfig.Provider, ModuleConfig.Model, Response);
				}

				// UI updates must happen on Game Thread
				AsyncTask(ENamedThreads::GameThread, [WeakSelf, CapturedGraphName, TranslationResponse, bSuccess]()
				{
					TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin();
					if (!StrongSelf.IsValid())
					{
						// Still need to clear global state even if window is gone
						FN2CEditorIntegration::Get().SetTranslationInProgress(false);
						return;
					}

					// Update progress modal
					if (StrongSelf->BatchProgressModal.IsValid())
					{
						StrongSelf->BatchProgressModal->MarkItemComplete(CapturedGraphName, bSuccess,
							bSuccess ? FString() : TranslationResponse.Error.ToDisplayString());

						// Create result for the modal
						FN2CBatchTranslationResult Result;
						Result.TotalCount = 1;
						Result.SuccessCount = bSuccess ? 1 : 0;
						Result.FailureCount = bSuccess ? 0 : 1;
						Result.TotalTimeSeconds = 0.0f; // Not tracked for single translations
						if (!bSuccess)
						{
							Result.FailedGraphNames.Add(CapturedGraphName);
						}
						StrongSelf->BatchProgressModal->SetResult(Result);
					}

					// Clear global translation state
					FN2CEditorIntegration::Get().SetTranslationInProgress(false);

					// Call the completion handler
					StrongSelf->OnSingleTranslationComplete(TranslationResponse, bSuccess);
				});
			}
		));
	}
	else
	{
		UE_LOG(LogNodeToCode, Error, TEXT("[SN2CMainWindow] Failed to initialize LLM Module"));
		FN2CEditorIntegration::Get().SetTranslationInProgress(false);
		bIsSingleTranslationInProgress = false;
		HideBatchProgress();
	}
}

// ==================== Single Translation Callbacks ====================

void SN2CMainWindow::OnSingleTranslationComplete(const FN2CTranslationResponse& Response, bool bSuccess)
{
	bIsSingleTranslationInProgress = false;

	if (bSuccess && Response.Graphs.Num() > 0)
	{
		// Show translation in viewer
		ShowTranslationViewer(Response.Graphs[0], PendingSingleTranslateGraph.GraphName);

		// Update translation state in state manager
		RecordSingleTranslation(PendingSingleTranslateGraph, Response);
	}

	// Fire external delegate
	OnTranslationCompleteDelegate.ExecuteIfBound(Response, bSuccess);

	// Clear pending graph
	PendingSingleTranslateGraph = FN2CTagInfo();
}

bool SN2CMainWindow::RecordSingleTranslation(const FN2CTagInfo& Graph, const FN2CTranslationResponse& Response)
{
	FGuid GraphGuid;
	UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
	if (Response.Graphs.Num() == 0 || !FGuid::Parse(Graph.GraphGuid, GraphGuid) || !LLMModule)
	{
		return false;
	}

	// SaveTranslationToDisk wrote <root>/<stem>/<stem><ext>, the stem from the graph name in the response
	const FString Root = LLMModule->GetLatestTranslationPath();
	const FString Stem = FN2CPathUtils::GetTranslationFileStem(Response.Graphs[0].GraphName);
	if (Root.IsEmpty() || Stem.IsEmpty())
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CMainWindow] Translation of %s was not saved to disk; not recorded"), *Graph.GraphName);
		return false;
	}

	// Create translation summary from the response
	FN2CTranslationSummary Summary;
	const FN2CGraphTranslation& Translation = Response.Graphs[0];
	Summary.DeclarationPreview = Translation.Code.GraphDeclaration.Left(100);
	TArray<FString> Lines;
	Translation.Code.GraphImplementation.ParseIntoArrayLines(Lines, false);
	Summary.ImplementationLines = Lines.Num();
	Summary.bHasNotes = !Translation.Code.ImplementationNotes.IsEmpty();

	UN2CGraphStateManager::Get().RecordSavedTranslation(
		GraphGuid,
		Graph.GraphName,
		FSoftObjectPath(Graph.BlueprintPath),
		FPaths::Combine(Root, Stem),
		TEXT("Unknown"),
		TEXT("Unknown"),
		LLMModule->GetLatestTranslationLanguage(),
		Summary
	);
	return true;
}

// ==================== Overlay Visibility ====================

EVisibility SN2CMainWindow::GetTranslationOverlayVisibility() const
{
	return bShowTranslationViewer ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SN2CMainWindow::GetBatchProgressVisibility() const
{
	return bShowBatchProgress ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SN2CMainWindow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Handle Escape key to close overlays
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (bShowTranslationViewer)
		{
			HideTranslationViewer();
			return FReply::Handled();
		}
		if (bShowBatchProgress)
		{
			// Only allow closing batch progress if complete
			if (BatchProgressModal.IsValid() && !UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress())
			{
				HideBatchProgress();
				return FReply::Handled();
			}
		}
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
