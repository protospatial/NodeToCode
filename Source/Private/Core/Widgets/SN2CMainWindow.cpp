// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CMainWindow.h"
#include "Core/Widgets/SN2CGetStartedCard.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CTagManager.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Core/N2CEditorIntegration.h"
#include "Subsystems/N2CTranslationSubsystem.h"
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
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "Core/Style/N2CUIColors.h"
#include "Utils/N2CPathUtils.h"
#include "Utils/N2CGraphUtils.h"

#define LOCTEXT_NAMESPACE "SN2CMainWindow"

void SN2CMainWindow::Construct(const FArguments& InArgs)
{
	OnBatchCompleteDelegate = InArgs._OnBatchComplete;
	OnTranslationCompleteDelegate = InArgs._OnTranslationComplete;
	bShowTranslationViewer = false;
	bShowBatchProgress = false;

	ChildSlot
	[
		SNew(SOverlay)
		// Base layer - the Get Started card (first run, card 0269) above the Tag Manager
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FN2CSpacing::SM, FN2CSpacing::SM, FN2CSpacing::SM, 0.0f)
			[
				SNew(SN2CGetStartedCard)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
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
				.BorderBackgroundColor(N2C::UI::BindAlpha(&FN2CUIColors::BgPanelDarker, 0.30f))
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
			.BorderBackgroundColor(N2C::UI::BindAlpha(&FN2CUIColors::BgPanelDarker, 0.30f))
			.Padding(0.0f)
			[
				// Center the modal within the full-screen overlay
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SAssignNew(BatchProgressModal, SN2CBatchProgressModal)
					.OnCancelRequested(FSimpleDelegate::CreateSP(this, &SN2CMainWindow::CancelOrCloseProgress))
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

	// Observe the translation sessions (card 0254): the overlay's and a row's Translate start them in the subsystem, so
	// one click is one request however many windows are open; while this window exists it shows them, so they get no
	// notification
	if (UN2CTranslationSubsystem* Translations = UN2CTranslationSubsystem::Get())
	{
		Translations->AddPresenter();
		bIsPresenter = true;
		SessionChangedHandle = Translations->OnSessionChangedNative.AddLambda([WeakSelf](const FN2CTranslationSession& Session)
		{
			if (TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin())
			{
				StrongSelf->HandleSessionChanged(Session);
			}
		});
		ShowResultHandle = Translations->OnShowResultRequested.AddLambda([WeakSelf](const FN2CTranslationSession& Session)
		{
			if (TSharedPtr<SN2CMainWindow> StrongSelf = WeakSelf.Pin())
			{
				StrongSelf->ShowSessionResult(Session);
			}
		});
	}
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

	// Stop observing the translation sessions (the subsystem is gone when the editor exits)
	if (UN2CTranslationSubsystem* Translations = UN2CTranslationSubsystem::Get())
	{
		Translations->OnSessionChangedNative.Remove(SessionChangedHandle);
		Translations->OnShowResultRequested.Remove(ShowResultHandle);
		if (bIsPresenter)
		{
			Translations->RemovePresenter();
		}
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
	StartBatchTranslation(GetSelectedGraphs());
}

void SN2CMainWindow::StartBatchTranslation(const TArray<FN2CTagInfo>& SelectedGraphs)
{
	UN2CTranslationSubsystem* Translations = UN2CTranslationSubsystem::Get();
	if (SelectedGraphs.Num() == 0 || !Translations)
	{
		return;
	}
	ObservedSession.Invalidate();
	ObservedBatch.Invalidate();

	// Initialize and show the batch progress modal: one row per graph, in the orchestrator's item order
	if (BatchProgressModal.IsValid())
	{
		BatchProgressModal->Initialize(SelectedGraphs);
	}
	ShowBatchProgress();

	// A batch session of the subsystem, which holds the busy state and ends the session whether or not this window
	// still exists. When it does not start (a translation runs, the provider cannot be used, no graph resolves), the
	// dialog goes and the reason is said.
	UN2CBatchTranslationOrchestrator& Orchestrator = UN2CBatchTranslationOrchestrator::Get();
	FString StartError;
	const FGuid BatchId = Translations->StartBatch(SelectedGraphs, FN2CBatchTranslationOptions(), &StartError);
	if (!BatchId.IsValid())
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CMainWindow] Batch translation did not start: %s"), *StartError);
		HideBatchProgress();
		if (StartError.StartsWith(TEXT("NO_VALID_ITEMS")))
		{
			// No selected graph resolved: say which and why (each message names its graph and Blueprint)
			NotifyUnresolvedBatchItems(Orchestrator.GetBatchItems(), TEXT("Batch translation did not start."));
		}
		else
		{
			FNotificationInfo Info(FText::Format(NSLOCTEXT("NodeToCode", "BatchDidNotStartReason", "Batch translation did not start: {0}"),
				FText::FromString(StartError)));
			Info.ExpireDuration = 8.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
		return;
	}
	// The batch may have ended inside StartBatch (every graph failed before a request was sent)
	const FN2CTranslationSession* Batch = Translations->FindSession(BatchId);
	if (Batch && !Batch->Info.IsFinished())
	{
		ObservedBatch = BatchId;
	}

	// Graphs that did not resolve fail before any request: mark them in the modal and say why
	const TArray<FN2CBatchTranslationItem>& Items = Orchestrator.GetBatchItems();
	if (BatchProgressModal.IsValid())
	{
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			const FN2CBatchTranslationItem& Item = Items[Index];
			if (Item.Status == EN2CBatchItemStatus::Failed && !Item.CachedGraph.IsValid())
			{
				BatchProgressModal->MarkItemCompleteAt(Index, false, Item.ErrorMessage);
			}
		}
	}
	NotifyUnresolvedBatchItems(Items, TEXT("Some graphs were skipped."));
}

void SN2CMainWindow::NotifyUnresolvedBatchItems(const TArray<FN2CBatchTranslationItem>& Items, const FString& Lead)
{
	TArray<FString> Messages;
	for (const FN2CBatchTranslationItem& Item : Items)
	{
		if (Item.Status == EN2CBatchItemStatus::Failed && !Item.CachedGraph.IsValid() && !Item.ErrorMessage.IsEmpty())
		{
			Messages.Add(Item.ErrorMessage);
		}
	}
	if (Messages.Num() == 0)
	{
		return;
	}
	FString Text = Lead + TEXT(" ") + Messages[0];
	if (Messages.Num() > 1)
	{
		Text += FString::Printf(TEXT(" (and %d more: see the Output Log)"), Messages.Num() - 1);
	}
	FN2CGraphUtils::NotifyFailure(Text);
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

	// Into the folder the window's Output field shows (card 0258), and say how it went
	const FString OutputDirectory = TagManager.IsValid() ? TagManager->GetOutputPath() : FString();
	FN2CBatchJsonExportResult Result;
	const bool bRan = UN2CBatchTranslationOrchestrator::Get().BatchExportJson(SelectedGraphs, Result, bMinify, OutputDirectory);
	NotifyJsonExportResult(bRan, Result);
}

void SN2CMainWindow::NotifyJsonExportResult(bool bRan, const FN2CBatchJsonExportResult& Result)
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FNotificationInfo Info(FText::GetEmpty());
	Info.ExpireDuration = 8.0f;
	Info.bFireAndForget = true;
	SNotificationItem::ECompletionState State = SNotificationItem::CS_Success;
	if (!bRan)
	{
		Info.Text = FText::Format(NSLOCTEXT("NodeToCode", "ExportJsonDidNotRun", "Export JSON did not run: {0}"),
			FText::FromString(Result.ErrorMessage.IsEmpty() ? FString(TEXT("see the Output Log.")) : Result.ErrorMessage));
		State = SNotificationItem::CS_Fail;
	}
	else
	{
		Info.Text = Result.FailureCount == 0
			? FText::Format(NSLOCTEXT("NodeToCode", "ExportJsonDone", "Exported {0} graphs as JSON to {1}"),
				FText::AsNumber(Result.SuccessCount), FText::FromString(Result.OutputPath))
			: FText::Format(NSLOCTEXT("NodeToCode", "ExportJsonPartial", "Exported {0} of {1} graphs as JSON to {2}. Failed: {3} (the Output Log says why)"),
				FText::AsNumber(Result.SuccessCount), FText::AsNumber(Result.TotalCount), FText::FromString(Result.OutputPath),
				FText::FromString(FString::Join(Result.FailedGraphNames, TEXT(", "))));
		State = Result.SuccessCount == 0 ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success;
		const FString Folder = Result.OutputPath;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([Folder]() { FPlatformProcess::ExploreFolder(*Folder); });
		Info.HyperlinkText = NSLOCTEXT("NodeToCode", "ExportJsonOpenFolder", "Open folder");
	}
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(State);
	}
}

void SN2CMainWindow::HandleRemoveSelectedRequested()
{
	// A row lists its graph once with every tag that put it in the view (a category or search view): remove them all
	TArray<FN2CTagInfo> SelectedGraphs = TagManager.IsValid() ? TagManager->GetSelectedTagEntries() : TArray<FN2CTagInfo>();
	if (SelectedGraphs.Num() == 0)
	{
		return;
	}

	TArray<FN2CTagRemovalRequest> Requests;
	TSet<FN2CGraphKey> Graphs;
	for (const FN2CTagInfo& Graph : SelectedGraphs)
	{
		// The row's own Blueprint: a duplicated Blueprint's graph has the same GUID (card 0192)
		const FN2CGraphKey Key = FN2CGraphKey::FromStrings(Graph.BlueprintPath, Graph.GraphGuid);
		if (Key.IsValid())
		{
			Requests.Add({Key, Graph.Tag, Graph.Category});
			Graphs.Add(Key);
		}
	}
	if (Requests.Num() == 0)
	{
		return;
	}

	const FText Question = FText::Format(
		LOCTEXT("RemoveSelectedConfirm", "Remove {0} {0}|plural(one=tag,other=tags) from {1} {1}|plural(one=graph,other=graphs)? Translations and JSON exports are kept."),
		FText::AsNumber(Requests.Num()), FText::AsNumber(Graphs.Num()));
	if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
	{
		return;
	}

	// One save and one reload broadcast for the whole selection; the tag list refreshes once on that broadcast
	TArray<FN2CRemovedTag> Removed = UN2CGraphStateManager::Get().RemoveTags(Requests);
	RefreshData();
	if (Removed.Num() == 0)
	{
		return;
	}

	// Offer Undo: the snapshot puts each tag back with its description and timestamp. The lambda holds no widget, so
	// it works after this window closes; the holder stops a second click restoring twice.
	TSharedRef<TWeakPtr<SNotificationItem>> NotificationHolder = MakeShared<TWeakPtr<SNotificationItem>>();
	FNotificationInfo Info(FText::Format(
		LOCTEXT("RemoveSelectedDone", "Removed {0} {0}|plural(one=tag,other=tags)"), FText::AsNumber(Removed.Num())));
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		LOCTEXT("RemoveSelectedUndo", "Undo"),
		LOCTEXT("RemoveSelectedUndoToolTip", "Put the removed tags back"),
		FSimpleDelegate::CreateLambda([NotificationHolder, Snapshot = MoveTemp(Removed)]()
		{
			const TSharedPtr<SNotificationItem> Item = NotificationHolder->Pin();
			if (!Item.IsValid() || Item->GetCompletionState() == SNotificationItem::CS_Success)
			{
				return;
			}
			const int32 Restored = UN2CGraphStateManager::Get().RestoreTags(Snapshot);
			Item->SetText(FText::Format(
				LOCTEXT("RemoveSelectedUndone", "Restored {0} {0}|plural(one=tag,other=tags)"), FText::AsNumber(Restored)));
			Item->SetCompletionState(SNotificationItem::CS_Success);
			Item->ExpireAndFadeout();
		}),
		SNotificationItem::CS_None));
	Info.ExpireDuration = 10.0f;
	Info.bUseSuccessFailIcons = false;
	*NotificationHolder = FSlateNotificationManager::Get().AddNotification(Info);
}

// ==================== Backend Integration ====================

void SN2CMainWindow::TranslateSingleGraph(const FN2CTagInfo& Graph)
{
	// A session of the translation subsystem, as the overlay's Translate starts one; this window shows it through
	// HandleSessionChanged like any other
	UN2CTranslationSubsystem* Translations = UN2CTranslationSubsystem::Get();
	if (!Translations)
	{
		return;
	}
	FN2CTranslationRequest Request;
	Request.BlueprintPath = Graph.BlueprintPath;
	Request.GraphGuid = Graph.GraphGuid;
	Request.GraphName = Graph.GraphName;
	Request.Source = EN2CTranslationSource::Row;
	Translations->StartTranslation(Request);
}

void SN2CMainWindow::ExportSingleGraphToJson(const FN2CTagInfo& Graph)
{
	// Every graph of the Blueprint (interface implementations and collapsed graphs too); a failure is shown, not dropped
	UBlueprint* Blueprint = nullptr;
	FString Error;
	UEdGraph* EdGraph = FN2CGraphUtils::ResolveTaggedGraph(Graph.BlueprintPath, Graph.GraphGuid, Graph.GraphName, Blueprint, Error);
	if (!EdGraph)
	{
		FN2CGraphUtils::NotifyFailure(Error);
		return;
	}
	FString Reason;
	if (!FN2CGraphUtils::CanTranslateGraph(EdGraph, &Reason))
	{
		FN2CGraphUtils::NotifyFailure(Reason);
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
	UBlueprint* Blueprint = nullptr;
	FString Error;
	UEdGraph* EdGraph = FN2CGraphUtils::ResolveTaggedGraph(Graph.BlueprintPath, Graph.GraphGuid, Graph.GraphName, Blueprint, Error);
	if (!EdGraph)
	{
		FN2CGraphUtils::NotifyFailure(Error);
		return;
	}

	// Open the blueprint editor
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);

	// Open the specific graph tab in the Blueprint editor
	// Use FKismetEditorUtilities to open and focus the graph
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(EdGraph);
}

// ==================== Batch Translation Callbacks ====================

void SN2CMainWindow::OnBatchItemComplete(const FN2CTagInfo& TagInfo, const FN2CTranslationResponse& Response,
	bool bSuccess, int32 ItemIndex, int32 TotalCount)
{
	UE_LOG(LogNodeToCode, Verbose, TEXT("[SN2CMainWindow] OnBatchItemComplete: %s (success=%s, item %d/%d)"),
		*TagInfo.GraphName, bSuccess ? TEXT("true") : TEXT("false"), ItemIndex + 1, TotalCount);

	// Only a batch this window shows (an MCP translate-batch runs without the window's progress)
	if (BatchProgressModal.IsValid() && bShowBatchProgress)
	{
		// By item index: several graphs in a batch can share a name (card 0489)
		BatchProgressModal->MarkItemCompleteAt(ItemIndex, bSuccess,
			bSuccess ? FString() : Response.Error.ToDisplayString());
	}
}

void SN2CMainWindow::OnBatchComplete(const FN2CBatchTranslationResult& Result)
{
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
	UE_LOG(LogNodeToCode, Verbose, TEXT("[SN2CMainWindow] OnBatchProgress: %s (item %d/%d)"),
		*GraphName, CurrentIndex + 1, TotalCount);

	if (BatchProgressModal.IsValid() && bShowBatchProgress)
	{
		BatchProgressModal->SetProgress(CurrentIndex, TotalCount, GraphName);
	}
}

void SN2CMainWindow::CancelOrCloseProgress()
{
	UN2CTranslationSubsystem* Translations = UN2CTranslationSubsystem::Get();
	const FGuid Running = ObservedBatch.IsValid() ? ObservedBatch : ObservedSession;
	if (!BatchProgressModal.IsValid() || BatchProgressModal->IsComplete() || !Translations || !Running.IsValid())
	{
		// Finished (the button reads Close), or nothing of this window's runs
		ObservedSession.Invalidate();
		ObservedBatch.Invalidate();
		HideBatchProgress();
		return;
	}
	if (BatchProgressModal->IsCancelling())
	{
		return;
	}

	// "Cancelling..." first: the cancelled completion usually arrives inside Cancel and replaces it (a batch's summary
	// with its Skipped rows; a single session's dialog closes)
	BatchProgressModal->SetCancelling();
	Translations->Cancel(Running);

	// A session that is gone or ended without telling this window leaves nothing to wait for
	const FN2CTranslationSession* Session = Translations->FindSession(Running);
	if ((!Session || Session->Info.IsFinished()) && bShowBatchProgress && !BatchProgressModal->IsComplete())
	{
		ObservedSession.Invalidate();
		ObservedBatch.Invalidate();
		HideBatchProgress();
	}
}

// ==================== Translation Sessions ====================

void SN2CMainWindow::HandleSessionChanged(const FN2CTranslationSession& Session)
{
	// The editor's own Translate buttons; the batch this window started shows through the orchestrator's progress;
	// MCP and Python translations show elsewhere
	const FN2CTranslationSessionInfo& Info = Session.Info;
	if (Info.SessionId == ObservedBatch && Info.IsFinished())
	{
		ObservedBatch.Invalidate();
	}
	if (Info.Source != EN2CTranslationSource::Overlay && Info.Source != EN2CTranslationSource::Row)
	{
		return;
	}

	switch (Info.State)
	{
	case EN2CTranslationSessionState::Queued:
		// A new session: the modal shows it as a batch of one
		ObservedSession = Info.SessionId;
		if (BatchProgressModal.IsValid())
		{
			BatchProgressModal->Initialize(TArray<FString>{Info.GraphName});
		}
		ShowBatchProgress();
		return;
	case EN2CTranslationSessionState::Capturing:
		return;
	case EN2CTranslationSessionState::Sending:
		if (Info.SessionId == ObservedSession && BatchProgressModal.IsValid())
		{
			BatchProgressModal->SetProgress(0, 1, Info.GraphName);
		}
		return;
	case EN2CTranslationSessionState::Refining:
		// Verify and Refine sent the answer back with its findings (card 0313)
		if (Info.SessionId == ObservedSession && BatchProgressModal.IsValid())
		{
			BatchProgressModal->SetProgress(0, 1, FString::Printf(TEXT("%s (refine turn %d)"), *Info.GraphName, Info.RefineTurns));
		}
		return;
	default:
		break;
	}

	const bool bSuccess = Info.State == EN2CTranslationSessionState::Succeeded;
	const bool bObserved = Info.SessionId == ObservedSession;
	if (bObserved)
	{
		ObservedSession.Invalidate();
		if (Info.State == EN2CTranslationSessionState::Cancelled || Session.Json.IsEmpty())
		{
			// Cancelled, or failed before anything was sent (the subsystem's notification says why)
			HideBatchProgress();
		}
		else if (BatchProgressModal.IsValid())
		{
			BatchProgressModal->MarkItemComplete(Info.GraphName, bSuccess, bSuccess ? FString() : Info.ErrorMessage);
			FN2CBatchTranslationResult Result;
			Result.TotalCount = 1;
			Result.SuccessCount = bSuccess ? 1 : 0;
			Result.FailureCount = bSuccess ? 0 : 1;
			Result.TotalTimeSeconds = 0.0f; // Not tracked for single translations
			if (!bSuccess)
			{
				Result.FailedGraphNames.Add(Info.GraphName);
			}
			BatchProgressModal->SetResult(Result);
		}
	}

	if (bSuccess)
	{
		// The tag list's View button for the newly recorded translation, and the result in the viewer
		RefreshData();
		ShowSessionResult(Session);
	}
	if (Info.State != EN2CTranslationSessionState::Cancelled)
	{
		OnTranslationCompleteDelegate.ExecuteIfBound(Session.Response, bSuccess);
	}
}

void SN2CMainWindow::ShowSessionResult(const FN2CTranslationSession& Session)
{
	// The root entry (the graph that was asked for), with the N2C JSON it was made from in the JSON tab
	if (Session.GetRootTranslation())
	{
		// Every graph of the response, the one asked for first (card 0257), in the session's language
		if (TranslationViewer.IsValid())
		{
			const FN2CArchivedEntry* Root = Session.Saved.GetRoot();
			TranslationViewer->LoadResponse(Session.Response, Session.Info.GraphName, Session.Json, Session.Info.Language,
				Root ? Root->ResponseIndex : INDEX_NONE);
			// How Verify and Refine ended for it (card 0313)
			TranslationViewer->SetVerifyOutcome(&Session.Verify);
			bShowTranslationViewer = true;
		}
	}
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
			// As the dialog's button: cancels what runs (through the subsystem), or closes a finished dialog
			CancelOrCloseProgress();
			return FReply::Handled();
		}
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

#undef LOCTEXT_NAMESPACE
