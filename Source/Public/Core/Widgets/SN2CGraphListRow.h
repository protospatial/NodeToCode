// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "TagManager/Models/N2CTagManagerTypes.h"

// Delegate declarations for row events
DECLARE_DELEGATE_OneParam(FOnRowCheckboxChanged, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowTranslateClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowJsonExportClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowViewTranslationClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowDoubleClicked, TSharedPtr<FN2CGraphListItem>);
DECLARE_DELEGATE_OneParam(FOnRowRefreshClicked, TSharedPtr<FN2CGraphListItem>);

/**
 * Custom row widget for the tagged graphs list
 * Handles hover state, selection highlighting, and action buttons
 */
class NODETOCODE_API SN2CGraphListRow : public SCompoundWidget
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
		/** Called when the row is double-clicked */
		SLATE_EVENT(FOnRowDoubleClicked, OnDoubleClicked)
		/** Called when the refresh button is clicked on the context column */
		SLATE_EVENT(FOnRowRefreshClicked, OnRefreshClicked)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	/** Force a visual update of the row */
	void UpdateVisuals();

	/** Get the item this row represents */
	TSharedPtr<FN2CGraphListItem> GetItem() const { return Item; }

private:
	/** The graph list item data */
	TSharedPtr<FN2CGraphListItem> Item;

	/** Whether the mouse is currently hovering over this row */
	bool bIsHovered = false;

	// Delegates
	FOnRowCheckboxChanged OnCheckboxChangedDelegate;
	FOnRowTranslateClicked OnTranslateClickedDelegate;
	FOnRowJsonExportClicked OnJsonExportClickedDelegate;
	FOnRowViewTranslationClicked OnViewTranslationClickedDelegate;
	FOnRowDoubleClicked OnDoubleClickedDelegate;
	FOnRowRefreshClicked OnRefreshClickedDelegate;

	// Event handlers
	void OnCheckboxStateChanged(ECheckBoxState NewState);
	FReply HandleTranslateClicked();
	FReply HandleJsonExportClicked();
	FReply HandleViewTranslationClicked();
	void HandleRefreshClicked();

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
