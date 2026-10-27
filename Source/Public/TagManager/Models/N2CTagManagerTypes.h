// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "N2CTagManagerTypes.generated.h"

/**
 * Translation status for a tagged graph
 */
UENUM(BlueprintType)
enum class EN2CGraphTranslationStatus : uint8
{
	Pending     UMETA(DisplayName = "Pending"),
	Translated  UMETA(DisplayName = "Translated"),
	Failed      UMETA(DisplayName = "Failed")
};

/**
 * Status filter options for the tag manager UI
 */
UENUM(BlueprintType)
enum class EN2CStatusFilter : uint8
{
	All         UMETA(DisplayName = "All"),
	Translated  UMETA(DisplayName = "Translated"),
	Pending     UMETA(DisplayName = "Pending"),
	Failed      UMETA(DisplayName = "Failed")
};

/**
 * Tree item type - category or tag
 */
enum class EN2CTreeItemType : uint8
{
	Category,
	Tag
};

/**
 * Tree item for category/tag tree view (non-USTRUCT for Slate use)
 */
struct FN2CTreeItem : public TSharedFromThis<FN2CTreeItem>
{
	/** Display name */
	FString Name;

	/** Category name (empty for category nodes) */
	FString Category;

	/** Number of graphs with this tag/in this category */
	int32 GraphCount;

	/** Item type */
	EN2CTreeItemType ItemType;

	/** Child items (tags under a category) */
	TArray<TSharedPtr<FN2CTreeItem>> Children;

	/** Parent item (for tags) */
	TWeakPtr<FN2CTreeItem> Parent;

	/** Whether this item is expanded in the tree */
	bool bIsExpanded;

	FN2CTreeItem()
		: GraphCount(0)
		, ItemType(EN2CTreeItemType::Category)
		, bIsExpanded(false)
	{
	}

	bool IsCategory() const { return ItemType == EN2CTreeItemType::Category; }
	bool IsTag() const { return ItemType == EN2CTreeItemType::Tag; }

	FString GetDisplayName() const
	{
		if (IsTag())
		{
			return FString::Printf(TEXT("%s (%d)"), *Name, GraphCount);
		}
		return Name;
	}
};

/**
 * List item for graphs table (non-USTRUCT for Slate use)
 */
struct FN2CGraphListItem : public TSharedFromThis<FN2CGraphListItem>
{
	/** Tag information for this graph (its first tag in this view) */
	FN2CTagInfo TagInfo;

	/** Every tag that put this graph in the view, one entry per tag: a graph is listed once (card 0266) */
	TArray<FN2CTagInfo> Tags;

	/** Translation status */
	EN2CGraphTranslationStatus Status;

	/** Whether this item is selected */
	bool bIsSelected;

	/** Whether this item is starred/pinned */
	bool bIsStarred;

	/** Whether a translation exists for this graph (can be viewed) */
	bool bHasTranslation;

	/** Estimated token count for this graph */
	int32 EstimatedTokens = 0;

	/** Estimated cost in USD for this graph */
	float EstimatedCost = 0.0f;

	/** Context usage percentage (0.0 - 1.0), using 65% of context window */
	float ContextUsagePercent = 0.0f;

	/** Whether the token estimate is valid (has been calculated) */
	bool bTokenEstimateValid = false;

	FN2CGraphListItem()
		: Status(EN2CGraphTranslationStatus::Pending)
		, bIsSelected(false)
		, bIsStarred(false)
		, bHasTranslation(false)
		, EstimatedTokens(0)
		, EstimatedCost(0.0f)
		, ContextUsagePercent(0.0f)
		, bTokenEstimateValid(false)
	{
	}

	/** Get shortened blueprint name from path */
	FString GetBlueprintDisplayName() const
	{
		// Extract just the asset name from the full path
		FString AssetName = FPaths::GetBaseFilename(TagInfo.BlueprintPath);
		return AssetName;
	}

	/** Get formatted token count (e.g., "12,345") */
	FString GetFormattedTokens() const
	{
		if (!bTokenEstimateValid || EstimatedTokens == 0)
		{
			return TEXT("-");
		}

		FString Result = FString::Printf(TEXT("%d"), EstimatedTokens);
		int32 InsertPosition = Result.Len() - 3;
		while (InsertPosition > 0)
		{
			Result.InsertAt(InsertPosition, TEXT(","));
			InsertPosition -= 3;
		}
		return Result;
	}

	/** Get formatted cost (e.g., "$0.03") */
	FString GetFormattedCost() const
	{
		if (!bTokenEstimateValid || EstimatedCost <= 0.0f)
		{
			return TEXT("-");
		}

		// Format with 2-4 decimal places depending on cost
		if (EstimatedCost < 0.01f)
		{
			return FString::Printf(TEXT("$%.4f"), EstimatedCost);
		}
		return FString::Printf(TEXT("$%.2f"), EstimatedCost);
	}
};

/**
 * Delegate signatures for tag manager events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTagSelectedEvent, const FString&, Tag, const FString&, Category);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCategorySelectedEvent, const FString&, Category);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGraphDoubleClickedEvent, const FN2CTagInfo&, TagInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectionChangedEvent, int32, SelectedCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBatchTranslateRequestedEvent, const TArray<FN2CTagInfo>&, TagInfos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExportJsonRequestedEvent, const TArray<FN2CTagInfo>&, TagInfos, bool, bMinify);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRemoveSelectedRequestedEvent, const TArray<FN2CTagInfo>&, TagInfos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBatchOperationProgressEvent, int32, Current, int32, Total, const FString&, GraphName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBatchOperationCompleteEvent, bool, bSuccess, const FString&, Message);

// Single graph action delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSingleTranslateRequestedEvent, const FN2CTagInfo&, TagInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSingleJsonExportRequestedEvent, const FN2CTagInfo&, TagInfo, bool, bMinify);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnViewTranslationRequestedEvent, const FN2CTagInfo&, TagInfo);
