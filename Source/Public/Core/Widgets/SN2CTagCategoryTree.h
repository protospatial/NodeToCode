// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "TagManager/Models/N2CTagManagerTypes.h"
#include "Models/N2CTaggedBlueprintGraph.h"

/** Every (graph, tag) pair the tag views show; unset means UN2CTagManager::Get().GetAllTags() */
using FN2CTagSource = TFunction<TArray<FN2CTaggedBlueprintGraph>()>;

/**
 * Slate widget for displaying categories and tags in a hierarchical tree view.
 *
 * Items are cached by category and tag name and updated in place, so the tree's selection and expansion survive
 * RefreshData and SetSearchFilter. Selection notifications the tree sends itself (ESelectInfo::Direct: a refresh or
 * filter that hides the selected item, or a programmatic SetSelection) are not forwarded (card 0266).
 */
class NODETOCODE_API SN2CTagCategoryTree : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CTagCategoryTree)
	{}
		/** Called when the user selects a category or tag */
		SLATE_EVENT(FSimpleDelegate, OnSelectionChanged)
		/** Where the tags come from (tests pass an isolated source); unset: the tag manager */
		SLATE_ARGUMENT(FN2CTagSource, TagSource)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Refresh tree data from the tag source, keeping the selection and expansion of items that still exist */
	void RefreshData();

	/** Apply search filter to tree items */
	void SetSearchFilter(const FString& SearchText);

	/** Get the currently selected item */
	TSharedPtr<FN2CTreeItem> GetSelectedItem() const;

	/** Check if selection is a category */
	bool IsSelectedCategory() const;

	/** Check if selection is a tag */
	bool IsSelectedTag() const;

	/** Get selected tag name (empty if category selected) */
	FString GetSelectedTag() const;

	/** Get selected category name */
	FString GetSelectedCategory() const;

	/** Select a specific tag programmatically */
	void SelectTag(const FString& InTag, const FString& InCategory);

	/** Select a specific category programmatically */
	void SelectCategory(const FString& Category);

	/** Clear selection */
	void ClearSelection();

	/** Delegate fired when the user changes the selection */
	FSimpleDelegate OnSelectionChangedDelegate;

private:
	/** Generate a row for the tree view */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FN2CTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get children for a tree item (the filtered ones while a search is active) */
	void OnGetChildren(TSharedPtr<FN2CTreeItem> Item, TArray<TSharedPtr<FN2CTreeItem>>& OutChildren);

	/** Handle selection change */
	void OnSelectionChanged(TSharedPtr<FN2CTreeItem> Item, ESelectInfo::Type SelectInfo);

	/** Handle expansion change */
	void OnExpansionChanged(TSharedPtr<FN2CTreeItem> Item, bool bIsExpanded);

	/** Build tree data from the tag source, reusing the cached items */
	void BuildTreeData();

	/** Rebuild FilteredRootItems and FilteredChildren from CurrentSearchFilter, then refresh the tree */
	void ApplyFilter();

	/** Set each category's expansion (all while searching, else the user's) and re-select SelectedItem if shown */
	void ApplyTreeState();

	/** The tags to show */
	TArray<FN2CTaggedBlueprintGraph> GetTags() const;

	/** The tree view widget */
	TSharedPtr<STreeView<TSharedPtr<FN2CTreeItem>>> TreeView;

	/** Root items (categories) */
	TArray<TSharedPtr<FN2CTreeItem>> RootItems;

	/** All items (for searching) */
	TArray<TSharedPtr<FN2CTreeItem>> AllItems;

	/** Filtered items for display */
	TArray<TSharedPtr<FN2CTreeItem>> FilteredRootItems;

	/** While searching: the children of each shown category that are shown */
	TMap<TSharedPtr<FN2CTreeItem>, TArray<TSharedPtr<FN2CTreeItem>>> FilteredChildren;

	/** Cached items, by category name and by "<category>\n<tag>" (FString keys compare without case) */
	TMap<FString, TSharedPtr<FN2CTreeItem>> CategoryItems;
	TMap<FString, TSharedPtr<FN2CTreeItem>> TagItems;

	/** Currently selected item */
	TSharedPtr<FN2CTreeItem> SelectedItem;

	/** Current search filter */
	FString CurrentSearchFilter;

	/** Where the tags come from */
	FN2CTagSource TagSource;

	/** Set while this widget changes the tree's expansion, so a search does not overwrite the user's */
	bool bApplyingTreeState = false;
};
