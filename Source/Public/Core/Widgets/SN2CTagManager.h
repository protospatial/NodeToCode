// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "TagManager/Models/N2CTagManagerTypes.h"

class SN2CTagCategoryTree;
class SN2CTaggedGraphsList;
class SN2CContextWindowVisualizer;

/**
 * Main Slate widget for the Tag Manager UI
 * Contains the categories tree, graphs list, search bar, and action buttons
 */
class NODETOCODE_API SN2CTagManager : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CTagManager)
		: _ShowSearchBar(true)
		, _ShowActionBar(true)
		, _MinifyJsonByDefault(false)
	{}
		/** Whether to show the search bar */
		SLATE_ARGUMENT(bool, ShowSearchBar)
		/** Whether to show the action bar */
		SLATE_ARGUMENT(bool, ShowActionBar)
		/** Default state of minify JSON checkbox */
		SLATE_ARGUMENT(bool, MinifyJsonByDefault)

		/** Called when a tag is selected */
		SLATE_EVENT(FSimpleDelegate, OnTagSelected)
		/** Called when a category is selected */
		SLATE_EVENT(FSimpleDelegate, OnCategorySelected)
		/** Called when selection changes in the list */
		SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
		/** Called when a graph is double-clicked */
		SLATE_EVENT(FSimpleDelegate, OnGraphDoubleClicked)
		/** Called when batch translate is requested */
		SLATE_EVENT(FSimpleDelegate, OnBatchTranslateRequested)
		/** Called when export JSON is requested */
		SLATE_EVENT(FSimpleDelegate, OnExportJsonRequested)
		/** Called when remove selected is requested */
		SLATE_EVENT(FSimpleDelegate, OnRemoveSelectedRequested)
		/** Called when single graph translate is requested */
		SLATE_EVENT(FSimpleDelegate, OnSingleTranslateRequested)
		/** Called when single graph JSON export is requested */
		SLATE_EVENT(FSimpleDelegate, OnSingleJsonExportRequested)
		/** Called when view translation is requested */
		SLATE_EVENT(FSimpleDelegate, OnViewTranslationRequested)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Refresh all data */
	void RefreshData();

	/** Get the currently selected tag (empty if category selected) */
	FString GetSelectedTag() const;

	/** Get the currently selected category */
	FString GetSelectedCategory() const;

	/** Check if a category is selected (vs a tag) */
	bool IsSelectedCategory() const;

	/** Get selected graphs from the list */
	TArray<FN2CTagInfo> GetSelectedGraphs() const;

	/** Get count of selected graphs */
	int32 GetSelectedCount() const;

	/** Get whether minify JSON is enabled */
	bool IsMinifyJsonEnabled() const;

	/** Get the output path */
	FString GetOutputPath() const;

	/** Get the double-clicked graph */
	FN2CTagInfo GetDoubleClickedGraph() const;

	/** Get the graph for which translate was requested */
	FN2CTagInfo GetTranslateRequestedGraph() const;

	/** Get the graph for which JSON export was requested */
	FN2CTagInfo GetJsonExportRequestedGraph() const;

	/** Get the graph for which view translation was requested */
	FN2CTagInfo GetViewTranslationRequestedGraph() const;

	/** Programmatically select a tag */
	void SelectTag(const FString& InTag, const FString& InCategory);

	/** Programmatically select a category */
	void SelectCategory(const FString& Category);

	/** Set search filter text */
	void SetSearchFilter(const FString& SearchText);

	// Delegates
	FSimpleDelegate OnTagSelectedDelegate;
	FSimpleDelegate OnCategorySelectedDelegate;
	FSimpleDelegate OnSelectionChangedDelegate;
	FSimpleDelegate OnGraphDoubleClickedDelegate;
	FSimpleDelegate OnBatchTranslateRequestedDelegate;
	FSimpleDelegate OnExportJsonRequestedDelegate;
	FSimpleDelegate OnRemoveSelectedRequestedDelegate;
	FSimpleDelegate OnSingleTranslateRequestedDelegate;
	FSimpleDelegate OnSingleJsonExportRequestedDelegate;
	FSimpleDelegate OnViewTranslationRequestedDelegate;

private:
	/** Handle tree selection change */
	void HandleTreeSelectionChanged();

	/** Handle list selection change */
	void HandleListSelectionChanged();

	/** Handle graph double-click */
	void HandleGraphDoubleClicked();

	/** Handle search text change */
	void HandleSearchTextChanged(const FText& NewText);

	/** Handle batch translate button click */
	FReply HandleBatchTranslateClicked();

	/** Handle export JSON button click */
	FReply HandleExportJsonClicked();

	/** Handle remove selected button click */
	FReply HandleRemoveSelectedClicked();

	/** Prune Missing: removes the graph state of Blueprints that were deleted (after a confirmation) */
	FReply HandlePruneMissingClicked();

	/** Handle browse button click */
	FReply HandleBrowseClicked();

	/** Update graphs list based on tree selection */
	void UpdateGraphsList();

	/** Update selection count display */
	void UpdateSelectionDisplay();

	/** Handle single graph translate request */
	void HandleSingleTranslateRequested();

	/** Handle single graph JSON export request */
	void HandleSingleJsonExportRequested();

	/** Handle view translation request */
	void HandleViewTranslationRequested();

	/** The category tree widget */
	TSharedPtr<SN2CTagCategoryTree> CategoryTree;

	/** The graphs list widget */
	TSharedPtr<SN2CTaggedGraphsList> GraphsList;

	/** The context window visualizer widget */
	TSharedPtr<SN2CContextWindowVisualizer> ContextVisualizer;

	/** Search text box */
	TSharedPtr<SEditableTextBox> SearchBox;

	/** Selection count text */
	TSharedPtr<STextBlock> SelectionCountText;

	/** Minify JSON checkbox */
	TSharedPtr<SCheckBox> MinifyCheckbox;

	/** Output path text box */
	TSharedPtr<SEditableTextBox> OutputPathBox;

	/** Whether minify is enabled */
	bool bMinifyJson;

	/** Output directory path */
	FString OutputPath;

	/** Current search filter text */
	FString CurrentSearchFilter;

	/** Delegate handles for tag manager events */
	FDelegateHandle OnTagAddedHandle;
	FDelegateHandle OnTagRemovedHandle;
	FDelegateHandle OnTagsReloadedHandle;

	/** Handle tag added event */
	void HandleTagAdded(const struct FN2CTaggedBlueprintGraph& TagInfo);

	/** Handle tag removed event */
	void HandleTagRemoved(const struct FN2CGraphKey& Graph, const FString& RemovedTag);

public:
	/** Destructor - cleans up delegate bindings */
	virtual ~SN2CTagManager();
};
