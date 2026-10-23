// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Models/N2CTaggedBlueprintGraph.h"

class FBlueprintEditor;
class SN2CCircularProgressBar;

/**
 * Overlay widget that appears in the top-right corner of Blueprint graph editors
 * Provides quick access to:
 * - Copy graph JSON to clipboard
 * - Initiate LLM translation
 * - View/add/remove tags
 */
class NODETOCODE_API SN2CGraphOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CGraphOverlay)
		: _GraphGuid()
		, _BlueprintPath()
		, _GraphName()
	{}
		/** GUID of the graph this overlay is for */
		SLATE_ARGUMENT(FGuid, GraphGuid)
		/** Path to the owning Blueprint */
		SLATE_ARGUMENT(FString, BlueprintPath)
		/** Name of the graph */
		SLATE_ARGUMENT(FString, GraphName)
		/** Weak reference to the Blueprint editor */
		SLATE_ARGUMENT(TWeakPtr<FBlueprintEditor>, BlueprintEditor)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor - cleans up delegate bindings */
	virtual ~SN2CGraphOverlay();

	/** Update the overlay when graph or tags change */
	void RefreshTagCount();

	/** Set translation in progress state */
	void SetTranslating(bool bInProgress);

private:
	// UI Elements
	TSharedPtr<SMenuAnchor> TagMenuAnchor;
	TSharedPtr<SWidget> TranslateSpinner;

	// State
	FGuid GraphGuid;
	FString BlueprintPath;
	FString GraphName;
	TWeakPtr<FBlueprintEditor> BlueprintEditor;
	bool bIsTranslating = false;
	int32 CachedTagCount = 0;

	// Combo box options (must persist for SComboBox)
	TArray<TSharedPtr<FString>> TagOptions;
	TArray<TSharedPtr<FString>> CategoryOptions;

	// Delegate handles for cleanup
	FDelegateHandle OnTagAddedHandle;
	FDelegateHandle OnTagRemovedHandle;
	FDelegateHandle OnTagsReloadedHandle;
	FDelegateHandle OnTranslationStateChangedHandle;
	FDelegateHandle OnModelChangedHandle;
	FDelegateHandle OnCacheInvalidatedHandle;

	// Context window tracking
	int32 CachedTokenCount = 0;
	float CachedContextUsagePercent = 0.0f;
	float CachedEstimatedCost = 0.0f;
	int32 CachedNestedGraphCount = 0;

	// Global translation state callback
	void OnGlobalTranslationStateChanged(bool bInProgress);

	// Button click handlers
	FReply OnOpenWindowClicked();
	FReply OnCopyJsonClicked();
	FReply OnTranslateClicked();
	FReply OnTagButtonClicked();

	// Tag popover content
	TSharedRef<SWidget> CreateTagPopoverContent();
	void OnAddTagRequested(const FString& TagName, const FString& CategoryName);
	void OnRemoveTagRequested(const FN2CTaggedBlueprintGraph& TagInfo);

	// Tag manager callbacks
	void OnTagAdded(const FN2CTaggedBlueprintGraph& TagInfo);
	void OnTagRemoved(const FGuid& RemovedGraphGuid, const FString& RemovedTag);

	// Visual state helpers
	EVisibility GetSpinnerVisibility() const;
	EVisibility GetTranslateTextVisibility() const;
	FText GetTagCountText() const;
	FText GetCopyJsonTooltip() const;
	FText GetTranslateTooltip() const;
	FText GetTagButtonTooltip() const;
	FSlateColor GetTagButtonColor() const;

	// Context usage helpers
	void RefreshTokenEstimate();
	void OnModelChanged();
	void OnCacheInvalidated();
	float GetContextUsagePercent() const;
	FText GetContextUsagePercentText() const;
	FText GetContextTooltipText() const;

	// Number formatting helper
	static FString FormatNumber(int64 Number);
};
