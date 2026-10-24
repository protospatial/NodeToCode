// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Models/N2CBatchTranslationTypes.h"

/**
 * Status for a batch progress item
 */
enum class EN2CBatchProgressItemStatus : uint8
{
	Pending,
	InProgress,
	Completed,
	Failed
};

/**
 * Individual item in the batch progress list
 */
struct FN2CBatchProgressItem
{
	FString GraphName;
	EN2CBatchProgressItemStatus Status;
	/** Why a failed item failed (the typed translation error's display string); empty otherwise */
	FString ErrorMessage;

	FN2CBatchProgressItem()
		: Status(EN2CBatchProgressItemStatus::Pending)
	{
	}

	FN2CBatchProgressItem(const FString& InGraphName, EN2CBatchProgressItemStatus InStatus = EN2CBatchProgressItemStatus::Pending)
		: GraphName(InGraphName)
		, Status(InStatus)
	{
	}
};

/**
 * Batch progress modal widget
 * Displays progress for batch translation operations
 */
class NODETOCODE_API SN2CBatchProgressModal : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SN2CBatchProgressModal)
		: _ModalWidth(380.0f)
	{}
		/** Called when the cancel button is clicked */
		SLATE_EVENT(FSimpleDelegate, OnCancelRequested)
		/** Width of the modal */
		SLATE_ARGUMENT(float, ModalWidth)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/**
	 * Initialize the progress modal with a list of graphs
	 * @param GraphNames Names of all graphs that will be processed
	 */
	void Initialize(const TArray<FString>& GraphNames);

	/**
	 * Update progress to show current item being processed
	 * @param CurrentIndex Zero-based index of current item
	 * @param TotalCount Total number of items
	 * @param CurrentGraphName Name of the graph currently being processed
	 */
	void SetProgress(int32 CurrentIndex, int32 TotalCount, const FString& CurrentGraphName);

	/**
	 * Mark an item as completed
	 * @param GraphName Name of the completed graph
	 * @param bSuccess Whether the translation succeeded
	 * @param ErrorMessage Why it failed (shown under the item and as its tooltip); ignored on success
	 */
	void MarkItemComplete(const FString& GraphName, bool bSuccess, const FString& ErrorMessage = FString());

	/** The error message recorded for an item, empty if none (for tests and callers) */
	FString GetItemErrorMessage(const FString& GraphName) const;

	/**
	 * Set the final result when batch is complete
	 * @param Result The batch translation result
	 */
	void SetResult(const FN2CBatchTranslationResult& Result);

	/** Reset the modal for a new batch operation */
	void Reset();

	/** Get current progress percentage (0.0 - 1.0) */
	float GetProgressPercent() const;

private:
	FSimpleDelegate OnCancelRequestedDelegate;

	// UI elements
	TSharedPtr<STextBlock> CurrentItemText;
	TSharedPtr<STextBlock> ProgressCountText;
	TSharedPtr<SListView<TSharedPtr<FN2CBatchProgressItem>>> ProgressListView;
	TSharedPtr<SButton> CancelButton;

	// Progress state
	TArray<TSharedPtr<FN2CBatchProgressItem>> ProgressItems;
	int32 CurrentItemIndex;
	int32 TotalItemCount;
	FString CurrentGraphName;
	bool bIsComplete;

	/** Handle cancel button click */
	FReply HandleCancelClicked();

	/** Generate a row for the progress list */
	TSharedRef<ITableRow> GenerateProgressRow(TSharedPtr<FN2CBatchProgressItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get progress bar fill percentage */
	TOptional<float> GetProgressBarPercent() const;

	/** Get the icon for a progress item status */
	FText GetStatusIcon(EN2CBatchProgressItemStatus Status) const;

	/** Get the color for a progress item status */
	FSlateColor GetStatusColor(EN2CBatchProgressItemStatus Status) const;

	/** Get the text color for a progress item */
	FSlateColor GetItemTextColor(EN2CBatchProgressItemStatus Status) const;
};
