// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "Models/N2CTranslation.h"
#include "Models/N2CBatchTranslationTypes.h"

class SN2CTagManager;
class SN2CTranslationViewer;
class SN2CBatchProgressModal;

/**
 * Delegate for when a batch operation completes
 */
DECLARE_DELEGATE_OneParam(FOnN2CBatchOperationComplete, const FN2CBatchTranslationResult&);

/**
 * Delegate for when a single translation completes
 */
DECLARE_DELEGATE_TwoParams(FOnN2CTranslationComplete, const FN2CTranslationResponse&, bool);

/**
 * Main NodeToCode window widget
 * Composes the Tag Manager with Translation Viewer and Batch Progress overlays
 * All action handlers are integrated in C++ - no Blueprint wiring required
 */
class NODETOCODE_API SN2CMainWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CMainWindow)
		: _ShowSearchBar(true)
		, _ShowActionBar(true)
	{}
		/** Whether to show the search bar in the tag manager */
		SLATE_ARGUMENT(bool, ShowSearchBar)
		/** Whether to show the action bar in the tag manager */
		SLATE_ARGUMENT(bool, ShowActionBar)
		/** Called when a batch operation completes */
		SLATE_EVENT(FOnN2CBatchOperationComplete, OnBatchComplete)
		/** Called when a single translation completes */
		SLATE_EVENT(FOnN2CTranslationComplete, OnTranslationComplete)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor - cleans up delegate bindings */
	virtual ~SN2CMainWindow();

	/** Refresh all data */
	void RefreshData();

	/** Get selected graphs from the tag manager */
	TArray<FN2CTagInfo> GetSelectedGraphs() const;

	/** Get the count of selected graphs */
	int32 GetSelectedCount() const;

	/** Show the translation viewer for a specific graph */
	void ShowTranslationViewer(const FN2CTagInfo& GraphInfo);

	/** Show the translation viewer with translation data directly */
	void ShowTranslationViewer(const FN2CGraphTranslation& Translation, const FString& GraphName, const FString& JsonContent = TEXT(""));

	/** Hide the translation viewer overlay */
	void HideTranslationViewer();

	/** Check if translation viewer is visible */
	bool IsTranslationViewerVisible() const { return bShowTranslationViewer; }

	/** Show the batch progress modal */
	void ShowBatchProgress();

	/** Hide the batch progress modal */
	void HideBatchProgress();

	/** Check if batch progress is visible */
	bool IsBatchProgressVisible() const { return bShowBatchProgress; }

	/**
	 * Record a finished single translation of Graph in the graph state, pointing at the folder
	 * UN2CLLMModule::SaveTranslationToDisk wrote for it and the language it was saved as, so the translation viewer
	 * can load it again. False when nothing was saved or Graph has no valid GUID.
	 */
	static bool RecordSingleTranslation(const FN2CTagInfo& Graph, const FN2CTranslationResponse& Response);

private:
	// External event delegates
	FOnN2CBatchOperationComplete OnBatchCompleteDelegate;
	FOnN2CTranslationComplete OnTranslationCompleteDelegate;

	// Child widgets
	TSharedPtr<SN2CTagManager> TagManager;
	TSharedPtr<SN2CTranslationViewer> TranslationViewer;
	TSharedPtr<SN2CBatchProgressModal> BatchProgressModal;

	// Overlay visibility state
	bool bShowTranslationViewer;
	bool bShowBatchProgress;

	// Pending operation state
	FN2CTagInfo PendingSingleTranslateGraph;
	/** The N2C JSON sent for the pending single translation, for the translation viewer's JSON tab */
	FString PendingSingleTranslateJson;
	bool bIsSingleTranslationInProgress;

	// Batch orchestrator delegate handles
	FDelegateHandle BatchItemCompleteHandle;
	FDelegateHandle BatchCompleteHandle;
	FDelegateHandle BatchProgressHandle;

	// Overlay translation request delegate handle
	FDelegateHandle OverlayTranslationRequestHandle;

	// ==================== Tag Manager Event Handlers ====================

	/** Handle tag selection in tree */
	void HandleTagSelected();

	/** Handle category selection in tree */
	void HandleCategorySelected();

	/** Handle selection change in graphs list */
	void HandleSelectionChanged();

	/** Handle graph double-click (navigate to graph) */
	void HandleGraphDoubleClicked();

	// ==================== Single Graph Action Handlers ====================

	/** Handle single graph translate request (from row hover button) */
	void HandleSingleTranslateRequested();

	/** Handle single graph JSON export request (from row hover button) */
	void HandleSingleJsonExportRequested();

	/** Handle view translation request (from row hover button) */
	void HandleViewTranslationRequested();

	// ==================== Batch Action Handlers ====================

	/** Handle batch translate button click */
	void HandleBatchTranslateRequested();

	/** Handle export JSON button click */
	void HandleExportJsonRequested();

	/** Handle remove selected button click */
	void HandleRemoveSelectedRequested();

	// ==================== Backend Integration ====================

	/** Translate a single graph */
	void TranslateSingleGraph(const FN2CTagInfo& Graph);

	/** Export a single graph to JSON */
	void ExportSingleGraphToJson(const FN2CTagInfo& Graph);

	/** Navigate to a graph in the Blueprint editor */
	void NavigateToGraph(const FN2CTagInfo& Graph);

	/** One notification for the batch items that failed before a request (graph not found or not translatable) */
	void NotifyUnresolvedBatchItems(const TArray<FN2CBatchTranslationItem>& Items, const FString& Lead);

	// ==================== Batch Translation Callbacks ====================

	/** Called when a batch item completes */
	void OnBatchItemComplete(const FN2CTagInfo& TagInfo, const FN2CTranslationResponse& Response,
		bool bSuccess, int32 ItemIndex, int32 TotalCount);

	/** Called when the entire batch completes */
	void OnBatchComplete(const FN2CBatchTranslationResult& Result);

	/** Called for batch progress updates */
	void OnBatchProgress(int32 CurrentIndex, int32 TotalCount, const FString& GraphName);

	// ==================== Overlay Translation Handling ====================

	/** Handle translation request from graph overlay */
	void HandleOverlayTranslationRequest(const FGuid& GraphGuid, const FString& GraphName, const FString& BlueprintPath);

	// ==================== Single Translation Callbacks ====================

	/** Called when single graph translation completes */
	void OnSingleTranslationComplete(const FN2CTranslationResponse& Response, bool bSuccess);

	// ==================== Overlay Visibility ====================

	/** Get visibility for translation overlay */
	EVisibility GetTranslationOverlayVisibility() const;

	/** Get visibility for batch progress modal */
	EVisibility GetBatchProgressVisibility() const;

	/** Handle keyboard input (Escape to close overlays) */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
};
