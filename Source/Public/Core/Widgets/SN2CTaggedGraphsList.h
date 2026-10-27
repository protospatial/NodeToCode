// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/SHeaderRow.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "Core/Widgets/SN2CGraphListRow.h"

// Delegate for when a row's refresh button is clicked
DECLARE_DELEGATE_OneParam(FOnRowRefreshClicked, TSharedPtr<FN2CGraphListItem>);

/** What the graphs list is showing, for its header and its empty-state text */
enum class EN2CGraphsListView : uint8
{
	Prompt,    // nothing selected
	Category,  // the graphs in one category
	Tag,       // the graphs with one tag
	Search     // the graphs matching the search text, across all tags
};

/**
 * Slate widget for displaying tagged graphs in a list/table view
 */
class NODETOCODE_API SN2CTaggedGraphsList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CTaggedGraphsList)
	{}
		/** Called when selection changes */
		SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
		/** Called when a graph row is double-clicked */
		SLATE_EVENT(FSimpleDelegate, OnGraphDoubleClicked)
		/** Called when translate button is clicked on a single graph */
		SLATE_EVENT(FSimpleDelegate, OnSingleTranslateRequested)
		/** Called when JSON export button is clicked on a single graph */
		SLATE_EVENT(FSimpleDelegate, OnSingleJsonExportRequested)
		/** Called when view translation button is clicked on a single graph */
		SLATE_EVENT(FSimpleDelegate, OnViewTranslationRequested)
		/** Called when refresh button is clicked on a single graph row */
		SLATE_EVENT(FOnRowRefreshClicked, OnRefreshTokensRequested)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor - cleans up delegate bindings */
	virtual ~SN2CTaggedGraphsList();

	/**
	 * Set the graphs to display: one row per graph (Blueprint and graph GUID), whatever number of tags put it in the
	 * view. With bKeepChecks, a graph checked before stays checked (a refresh of the same view)
	 */
	void SetGraphs(const TArray<FN2CTagInfo>& InTagInfos, bool bKeepChecks = false);

	/** One graph's identity in the list: its Blueprint and graph GUID (copies of a Blueprint share graph GUIDs) */
	static FString GetGraphKey(const FN2CTagInfo& Info);

	/** Infos with each graph once (the first entry of each), in order */
	static TArray<FN2CTagInfo> UniqueGraphs(const TArray<FN2CTagInfo>& Infos);

	/** Refresh token estimates for all items using the token estimation service */
	void RefreshTokenEstimates();

	/** Refresh token estimate for a specific item */
	void RefreshTokenEstimate(TSharedPtr<FN2CGraphListItem> Item);

	/** Set what the list shows: the header names it and the empty-state text explains an empty list */
	void SetView(EN2CGraphsListView InView, const FString& InCategory = FString(), const FString& InTag = FString());

	/** The header text shown */
	FText GetHeaderText() const;

	/** The text shown over an empty list */
	FText GetEmptyText() const;

	/** The rows shown (after the search filter) */
	const TArray<TSharedPtr<FN2CGraphListItem>>& GetShownItems() const { return FilteredItems; }

	/** Apply search filter to graphs */
	void SetSearchFilter(const FString& SearchText);

	/** Get currently selected graphs, each once */
	TArray<FN2CTagInfo> GetSelectedGraphs() const;

	/** Every (graph, tag) entry of the checked rows: what Remove Selected removes */
	TArray<FN2CTagInfo> GetSelectedTagEntries() const;

	/** Get count of selected graphs */
	int32 GetSelectedCount() const;

	/** Select all graphs */
	void SelectAll();

	/** Deselect all graphs */
	void DeselectAll();

	/** Get the graph that was double-clicked */
	FN2CTagInfo GetDoubleClickedGraph() const;

	/** Get the graph for which translate was requested */
	FN2CTagInfo GetTranslateRequestedGraph() const;

	/** Get the graph for which JSON export was requested */
	FN2CTagInfo GetJsonExportRequestedGraph() const;

	/** Get the graph for which view translation was requested */
	FN2CTagInfo GetViewTranslationRequestedGraph() const;

	/** Delegate fired when selection changes */
	FSimpleDelegate OnSelectionChangedDelegate;

	/** Delegate fired when a graph is double-clicked */
	FSimpleDelegate OnGraphDoubleClickedDelegate;

	/** Delegate fired when translate is requested for a single graph */
	FSimpleDelegate OnSingleTranslateRequestedDelegate;

	/** Delegate fired when JSON export is requested for a single graph */
	FSimpleDelegate OnSingleJsonExportRequestedDelegate;

	/** Delegate fired when view translation is requested for a single graph */
	FSimpleDelegate OnViewTranslationRequestedDelegate;

	/** Delegate fired when refresh tokens is requested for a single graph */
	FOnRowRefreshClicked OnRefreshTokensRequestedDelegate;

private:
	/** Handle model changes from the token estimation service */
	void OnModelChanged();

	/** Handle cache invalidation from the token estimation service */
	void OnCacheInvalidated();
	/** Generate a row for the list view */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FN2CGraphListItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Handle select-all checkbox */
	void OnSelectAllChanged(ECheckBoxState NewState);

	/** Get checkbox state for select-all */
	ECheckBoxState GetSelectAllCheckboxState() const;

	/** Handle select-all button click */
	FReply OnSelectAllClicked();

	/** Filter items based on current filters */
	void ApplyFilters();

	/** Check if item passes current filters */
	bool ItemPassesFilters(const TSharedPtr<FN2CGraphListItem>& Item) const;

	/** Update selection count display */
	void UpdateSelectionDisplay();

	/** Set the header text from the view and the search filter */
	void UpdateHeader();

	// Row widget callbacks
	/** Handle checkbox change from row widget */
	void HandleRowCheckboxChanged(TSharedPtr<FN2CGraphListItem> Item);

	/** Handle translate button click from row widget */
	void HandleRowTranslateClicked(TSharedPtr<FN2CGraphListItem> Item);

	/** Handle JSON export button click from row widget */
	void HandleRowJsonExportClicked(TSharedPtr<FN2CGraphListItem> Item);

	/** Handle view translation button click from row widget */
	void HandleRowViewTranslationClicked(TSharedPtr<FN2CGraphListItem> Item);

	/** Handle double-click from row widget */
	void HandleRowDoubleClicked(TSharedPtr<FN2CGraphListItem> Item);

	/** Handle refresh button click from row widget */
	void HandleRowRefreshClicked(TSharedPtr<FN2CGraphListItem> Item);

	/** The list view widget */
	TSharedPtr<SListView<TSharedPtr<FN2CGraphListItem>>> ListView;

	/** Header row */
	TSharedPtr<SHeaderRow> HeaderRow;

	/** Header path display */
	TSharedPtr<STextBlock> HeaderPathText;

	/** All items (unfiltered) */
	TArray<TSharedPtr<FN2CGraphListItem>> AllItems;

	/** Filtered items for display */
	TArray<TSharedPtr<FN2CGraphListItem>> FilteredItems;

	/** Current search filter */
	FString CurrentSearchFilter;

	/** What the list shows */
	EN2CGraphsListView View = EN2CGraphsListView::Prompt;
	FString ViewCategory;
	FString ViewTag;

	/** Last double-clicked graph */
	FN2CTagInfo LastDoubleClickedGraph;

	/** Last graph for which translate was requested */
	FN2CTagInfo LastTranslateRequestedGraph;

	/** Last graph for which JSON export was requested */
	FN2CTagInfo LastJsonExportRequestedGraph;

	/** Last graph for which view translation was requested */
	FN2CTagInfo LastViewTranslationRequestedGraph;

	/** Column IDs */
	static const FName Column_Checkbox;
	static const FName Column_GraphName;
	static const FName Column_Blueprint;
	static const FName Column_Context;

	/** Delegate handles for token estimation service */
	FDelegateHandle ModelChangedHandle;
	FDelegateHandle CacheInvalidatedHandle;
};
