// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Models/N2CTranslation.h"
#include "Engine/Blueprint.h"
#include "N2CTagBlueprintLibrary.generated.h"

/**
 * @struct FN2CTagInfo
 * @brief Simplified tag information structure for Blueprint use
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTagInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|Tags")
	FString Tag;

	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|Tags")
	FString Category = TEXT("Default");

	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|Tags")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FString GraphGuid;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FString GraphName;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FString BlueprintPath;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FDateTime Timestamp;

	FN2CTagInfo() {}

	/** Create from FN2CTaggedBlueprintGraph */
	static FN2CTagInfo FromTaggedGraph(const FN2CTaggedBlueprintGraph& TaggedGraph);

	/** Convert to FN2CTaggedBlueprintGraph */
	FN2CTaggedBlueprintGraph ToTaggedGraph() const;
};

/**
 * @class UN2CTagBlueprintLibrary
 * @brief Blueprint function library for managing Blueprint graph tags
 * 
 * This library exposes tag management functionality to Blueprints,
 * allowing UI integration for manual tag management.
 */
UCLASS()
class NODETOCODE_API UN2CTagBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Add a tag to the currently focused Blueprint graph
	 * @param Tag The tag name to apply
	 * @param Category The category for the tag (default: "Default")
	 * @param Description Optional description for the tag
	 * @param bSuccess Whether the tag was successfully added
	 * @param ErrorMessage Error message if the operation failed
	 * @return The created tag info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Tag Focused Blueprint Graph", ExpandBoolAsExecs = "bSuccess"))
	static FN2CTagInfo TagFocusedBlueprintGraph(
		const FString& Tag,
		const FString& Category,
		const FString& Description,
		bool& bSuccess,
		FString& ErrorMessage
	);

	/**
	 * Add a tag to a specific Blueprint graph
	 * @param BlueprintAsset The Blueprint containing the graph
	 * @param GraphName The name of the graph to tag (empty for first graph)
	 * @param Tag The tag name to apply
	 * @param Category The category for the tag
	 * @param Description Optional description for the tag
	 * @param bSuccess Whether the tag was successfully added
	 * @param ErrorMessage Error message if the operation failed
	 * @return The created tag info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Tag Blueprint Graph", ExpandBoolAsExecs = "bSuccess"))
	static FN2CTagInfo TagBlueprintGraph(
		UBlueprint* BlueprintAsset,
		const FString& GraphName,
		const FString& Tag,
		const FString& Category,
		const FString& Description,
		bool& bSuccess,
		FString& ErrorMessage
	);

	/**
	 * Remove a tag from a graph
	 * @param GraphGuid The GUID of the graph (as string)
	 * @param Tag The tag name to remove
	 * @param Category The tag category
	 * @param BlueprintPath The owning Blueprint's path (a tag info's Blueprint Path; object or package path). The graph
	 *        is {Blueprint, GraphGuid}: a duplicated Blueprint keeps the original's graph GUIDs, so without it the
	 *        removal works only while one Blueprint has a graph with this GUID (as the Python bridge's RemoveTag, #198)
	 * @return True if the tag was removed
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Remove Tag", AdvancedDisplay = "BlueprintPath"))
	static bool RemoveTag(
		const FString& GraphGuid,
		const FString& Tag,
		const FString& Category,
		const FString& BlueprintPath = TEXT("")
	);

	/**
	 * Get all tags for a specific graph
	 * @param GraphGuid The GUID of the graph (as string)
	 * @return Array of tag info for the graph
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get Tags For Graph"))
	static TArray<FN2CTagInfo> GetTagsForGraph(const FString& GraphGuid);

	/**
	 * Get all graphs with a specific tag
	 * @param Tag The tag name
	 * @param Category Optional category filter (empty for all)
	 * @return Array of tag info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get Graphs With Tag"))
	static TArray<FN2CTagInfo> GetGraphsWithTag(
		const FString& Tag,
		const FString& Category = TEXT("")
	);

	/**
	 * Get all tags in a specific category
	 * @param Category The category name
	 * @return Array of tag info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get Tags In Category"))
	static TArray<FN2CTagInfo> GetTagsInCategory(const FString& Category);

	/**
	 * Get all tags in the system
	 * @return Array of all tag info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get All Tags"))
	static TArray<FN2CTagInfo> GetAllTags();

	/**
	 * Get all unique tag names
	 * @return Array of unique tag names
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get All Tag Names"))
	static TArray<FString> GetAllTagNames();

	/**
	 * Get all unique categories
	 * @return Array of unique category names
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get All Categories"))
	static TArray<FString> GetAllCategories();

	/**
	 * Check if a graph has a specific tag
	 * @param GraphGuid The GUID of the graph (as string)
	 * @param Tag The tag name
	 * @param Category The tag category
	 * @return True if the graph has the tag
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Graph Has Tag"))
	static bool GraphHasTag(
		const FString& GraphGuid,
		const FString& Tag,
		const FString& Category
	);

	/**
	 * Clear all tags (use with caution)
	 * @return Number of tags cleared
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Clear All Tags", DevelopmentOnly))
	static int32 ClearAllTags();

	/**
	 * Save tags to disk
	 * @return True if save was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Save Tags"))
	static bool SaveTags();

	/**
	 * Load tags from disk
	 * @return True if load was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Load Tags"))
	static bool LoadTags();

	/**
	 * Get tag statistics
	 * @param TotalTags Total number of tags
	 * @param UniqueGraphs Number of unique graphs with tags
	 * @param UniqueTagNames Number of unique tag names
	 * @param UniqueCategories Number of unique categories
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Get Tag Statistics"))
	static void GetTagStatistics(
		int32& TotalTags,
		int32& UniqueGraphs,
		int32& UniqueTagNames,
		int32& UniqueCategories
	);

	/**
	 * Search tags by partial match
	 * @param SearchTerm The search term to match against tag names
	 * @param bSearchInDescription Also search in descriptions
	 * @return Array of matching tag info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Tags", meta = (DisplayName = "Search Tags"))
	static TArray<FN2CTagInfo> SearchTags(
		const FString& SearchTerm,
		bool bSearchInDescription = false
	);

	// ==================== Translation Functions ====================

	/**
	 * Check if a graph has a translation available
	 * @param GraphGuid The GUID of the graph (as string)
	 * @return True if a translation exists for this graph
	 */
	UFUNCTION(BlueprintPure, Category = "NodeToCode|Translation", meta = (DisplayName = "Has Translation"))
	static bool HasTranslation(const FString& GraphGuid);

	/**
	 * Load a translation for a graph
	 * @param GraphGuid The GUID of the graph (as string)
	 * @param OutTranslation The loaded translation data
	 * @return True if the translation was loaded successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Translation", meta = (DisplayName = "Load Translation"))
	static bool LoadTranslation(const FString& GraphGuid, FN2CGraphTranslation& OutTranslation);

	/**
	 * Load a translation for a graph using tag info
	 * @param TagInfo The tag info containing the graph GUID
	 * @param OutTranslation The loaded translation data
	 * @return True if the translation was loaded successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Translation", meta = (DisplayName = "Load Translation From Tag Info"))
	static bool LoadTranslationFromTagInfo(const FN2CTagInfo& TagInfo, FN2CGraphTranslation& OutTranslation);
};