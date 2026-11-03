// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Models/N2CBatchTranslationTypes.h"

struct FN2CTagInfo;

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

	/** Name of the Blueprint holding the graph, shown beside it so same-named graphs can be told apart; may be empty */
	FString BlueprintName;
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
	 * Initialize the modal with a batch's graphs, one row per graph in batch order, each labelled with its Blueprint.
	 * Rows are then addressed by item index (SetProgress, MarkItemCompleteAt), so graphs with the same name in different
	 * Blueprints (EventGraph, a function name) each keep their own status (card 0489).
	 */
	void Initialize(const TArray<FN2CTagInfo>& Graphs);

	/**
	 * Update progress to show current item being processed: the row at CurrentIndex is marked in progress (rows are
	 * addressed by index, not by name)
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

	/**
	 * Mark the row at ItemIndex (the orchestrator's item index) as completed or failed. Use this for batches: the name
	 * overload finds the first row with that name, which is wrong when several graphs share it.
	 */
	void MarkItemCompleteAt(int32 ItemIndex, bool bSuccess, const FString& ErrorMessage = FString());

	/** The error message recorded for an item, empty if none (for tests and callers) */
	FString GetItemErrorMessage(const FString& GraphName) const;

	/** The rows in order (for tests and callers) */
	const TArray<TSharedPtr<FN2CBatchProgressItem>>& GetItems() const { return ProgressItems; }

	/** The label a row shows: "Graph (Blueprint)", or the graph name alone */
	static FString GetItemLabel(const FN2CBatchProgressItem& Item);

	/**
	 * Set the final result when batch is complete
	 * @param Result The batch translation result
	 */
	void SetResult(const FN2CBatchTranslationResult& Result);

	/** Reset the modal for a new batch operation */
	void Reset();

	/** Get current progress percentage (0.0 - 1.0) */
	float GetProgressPercent() const;

	/** The FAppStyle icon for a progress item status (not a Unicode glyph, which depends on font coverage) */
	static const FSlateBrush* GetStatusBrush(EN2CBatchProgressItemStatus Status);

	/** The status in words: the icon's tooltip */
	static FText GetStatusLabel(EN2CBatchProgressItemStatus Status);

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

	/** Get the color for a progress item status */
	FSlateColor GetStatusColor(EN2CBatchProgressItemStatus Status) const;

	/** Get the text color for a progress item */
	FSlateColor GetItemTextColor(EN2CBatchProgressItemStatus Status) const;
};
