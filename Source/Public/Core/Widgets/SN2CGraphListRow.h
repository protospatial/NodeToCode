// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STableRow.h"
#include "TagManager/Models/N2CTagManagerTypes.h"

// Delegate declarations for row events
DECLARE_DELEGATE_OneParam(FOnRowCheckboxChanged, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowTranslateClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowJsonExportClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowViewTranslationClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowRefreshClicked, TSharedPtr<FN2CGraphListItem>);

/**
 * One row of the tagged graphs list (card 0267): a multi-column table row, so every cell follows the header's
 * columns (resizing, order). The list's selection is the checked set; the row shows hover actions in its Blueprint
 * cell. Double-click and the right-click menu are the list's (SListView events).
 */
class NODETOCODE_API SN2CGraphListRow : public SMultiColumnTableRow<TSharedPtr<FN2CGraphListItem>>
{
public:
	SLATE_BEGIN_ARGS(SN2CGraphListRow) {}
		/** The graph list item to display */
		SLATE_ARGUMENT(TSharedPtr<FN2CGraphListItem>, Item)
		/** Called when the checkbox state changes */
		SLATE_EVENT(FOnRowCheckboxChanged, OnCheckboxChanged)
		/** Called when the translate button is clicked */
		SLATE_EVENT(FOnRowTranslateClicked, OnTranslateClicked)
		/** Called when the JSON export button is clicked */
		SLATE_EVENT(FOnRowJsonExportClicked, OnJsonExportClicked)
		/** Called when the view translation button is clicked */
		SLATE_EVENT(FOnRowViewTranslationClicked, OnViewTranslationClicked)
		/** Called when the refresh button is clicked on the context column */
		SLATE_EVENT(FOnRowRefreshClicked, OnRefreshClicked)
	SLATE_END_ARGS()

	/** Column IDs, shared with the list's header row */
	static const FName Column_Checkbox;
	static const FName Column_GraphName;
	static const FName Column_Blueprint;
	static const FName Column_Context;

	/** Constructs this row in OwnerTable */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable);

	/** SMultiColumnTableRow: the cell of one header column */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	/** Force a visual update of the row */
	void UpdateVisuals();

	/** Get the item this row represents */
	TSharedPtr<FN2CGraphListItem> GetItem() const { return Item; }

private:
	/** The graph list item data */
	TSharedPtr<FN2CGraphListItem> Item;

	// Delegates
	FOnRowCheckboxChanged OnCheckboxChangedDelegate;
	FOnRowTranslateClicked OnTranslateClickedDelegate;
	FOnRowJsonExportClicked OnJsonExportClickedDelegate;
	FOnRowViewTranslationClicked OnViewTranslationClickedDelegate;
	FOnRowRefreshClicked OnRefreshClickedDelegate;

	// Event handlers
	void OnCheckboxStateChanged(ECheckBoxState NewState);
	FReply HandleTranslateClicked();
	FReply HandleJsonExportClicked();
	FReply HandleViewTranslationClicked();
	void HandleRefreshClicked();

	/** A cell: Content on the row's background (checked, hovered) */
	TSharedRef<SWidget> MakeCell(const TSharedRef<SWidget>& CellContent, bool bFill = true);

	// Visual state helpers (called via TAttribute binding)
	FSlateColor GetBackgroundColor() const;
	static const FSlateBrush* GetButtonOverlayBrush();
	FSlateColor GetButtonBackgroundColor() const;
	EVisibility GetActionButtonsVisibility() const;
	ECheckBoxState GetCheckboxState() const;
	bool IsViewButtonEnabled() const;
	FText GetViewButtonTooltip() const;

	/** The graph name's tooltip: every tag that lists the graph in this view */
	FText GetTagsTooltip() const;

	// Context column helpers
	float GetContextUsagePercent() const;
	FText GetTokensText() const;
	FText GetCostText() const;
};
