// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CGraphStateTypes.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "N2CTagManager.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnBlueprintTagAdded, const FN2CTaggedBlueprintGraph&);
/** A tag was removed from a graph: the key names the Blueprint too, because a duplicated Blueprint's graphs share GUIDs */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBlueprintTagRemoved, const FN2CGraphKey& /*Graph*/, const FString& /*Tag*/);
/** Many tags may have changed at once (Clear All, a bulk removal, a migration or a reload): re-read all tags */
DECLARE_MULTICAST_DELEGATE(FOnBlueprintTagsReloaded);

/**
 * @class UN2CTagManager
 * @brief Singleton manager for Blueprint graph tagging functionality
 *
 * This class manages tags applied to Blueprint graphs, providing functionality
 * to add, remove, query, and persist tags.
 *
 * NOTE: This class now delegates to UN2CGraphStateManager for all operations.
 * It is maintained for backward compatibility with existing code.
 * New code should use UN2CGraphStateManager directly for access to
 * translation and JSON export state in addition to tags.
 */
UCLASS()
class NODETOCODE_API UN2CTagManager : public UObject
{
	GENERATED_BODY()

public:
	/** Get the singleton instance */
	static UN2CTagManager& Get();

	/** The singleton if Get() has created it and it has not been shut down, else nullptr (does not create it) */
	static UN2CTagManager* GetIfExists() { return Instance; }

	/** Initialize the tag manager (initializes UN2CGraphStateManager) */
	void Initialize();

	/** Shutdown the tag manager (shuts down UN2CGraphStateManager) */
	void Shutdown();

	/**
	 * Add a tag to a Blueprint graph
	 * @param TaggedGraph The tag information to add
	 * @return True if the tag was added successfully
	 */
	bool AddTag(const FN2CTaggedBlueprintGraph& TaggedGraph);

	/**
	 * Remove a specific tag from a graph. Every GraphGuid parameter here is an FN2CGraphKey: pass the Blueprint too
	 * (FN2CGraphKey(Blueprint, Guid)); a bare GUID finds the graph only while no other Blueprint has one with that GUID.
	 * @param GraphGuid The graph
	 * @param Tag The tag name to remove
	 * @param Category The tag category
	 * @return True if the tag was removed
	 */
	bool RemoveTag(const FN2CGraphKey& GraphGuid, const FString& Tag, const FString& Category);

	/**
	 * Remove tag(s) from a graph by tag name only (removes all matching tags regardless of category)
	 * @param GraphGuid The GUID of the graph
	 * @param Tag The tag name to remove
	 * @param OutRemovedTag If successful, contains the removed tag info (first one if multiple)
	 * @return Number of tags removed (0 if none found)
	 */
	int32 RemoveTagByName(const FN2CGraphKey& GraphGuid, const FString& Tag, FN2CTaggedBlueprintGraph& OutRemovedTag);

	/**
	 * Remove all tags from a graph
	 * @param GraphGuid The GUID of the graph
	 * @return Number of tags removed
	 */
	int32 RemoveAllTagsFromGraph(const FN2CGraphKey& GraphGuid);

	/**
	 * Get all tags for a specific graph
	 * @param GraphGuid The GUID of the graph
	 * @return Array of tags for the graph
	 */
	TArray<FN2CTaggedBlueprintGraph> GetTagsForGraph(const FN2CGraphKey& GraphGuid) const;

	/**
	 * Get all graphs with a specific tag
	 * @param Tag The tag name
	 * @param Category Optional category filter (empty string for all categories)
	 * @return Array of tagged graphs
	 */
	TArray<FN2CTaggedBlueprintGraph> GetGraphsWithTag(const FString& Tag, const FString& Category = TEXT("")) const;

	/**
	 * Get all tags in a specific category
	 * @param Category The category name
	 * @return Array of tagged graphs in the category
	 */
	TArray<FN2CTaggedBlueprintGraph> GetTagsInCategory(const FString& Category) const;

	/**
	 * Get all unique tag names
	 * @return Array of unique tag names
	 */
	TArray<FString> GetAllTagNames() const;

	/**
	 * Get all unique categories
	 * @return Array of unique category names
	 */
	TArray<FString> GetAllCategories() const;

	/**
	 * Check if a graph has a specific tag
	 * @param GraphGuid The GUID of the graph
	 * @param Tag The tag name
	 * @param Category The tag category
	 * @return True if the graph has the tag
	 */
	bool GraphHasTag(const FN2CGraphKey& GraphGuid, const FString& Tag, const FString& Category) const;

	/** Get all tags in the system */
	const TArray<FN2CTaggedBlueprintGraph>& GetAllTags() const;

	/** Clear all tags (use with caution); translation and export state are kept. Saves once and broadcasts OnTagsReloaded once */
	void ClearAllTags();

	/** Save tags to persistent storage */
	bool SaveTags();

	/** Load tags from persistent storage */
	bool LoadTags();

	/** Delegate fired when a tag is added */
	FOnBlueprintTagAdded OnBlueprintTagAdded;

	/** Delegate fired when a tag is removed */
	FOnBlueprintTagRemoved OnBlueprintTagRemoved;

	/** Delegate fired when many tags changed at once (no per-tag events were sent for them) */
	FOnBlueprintTagsReloaded OnTagsReloaded;

	/** Shuts the singleton down if it exists (module shutdown); does not create it */
	static void ShutdownInstance();

	/**
	 * Builds the OnBlueprintTagAdded payload from a graph state the graph state manager broadcast after AddTag:
	 * the tag just added, which is the graph's last tag. False if the graph has no tags.
	 */
	static bool MakeTagAddedEvent(const struct FN2CGraphState& GraphState, FN2CTaggedBlueprintGraph& OutTag);

private:
	/** Constructor */
	UN2CTagManager();

	/** The singleton instance */
	static UN2CTagManager* Instance;

	/** Subscriptions to the graph state manager, removed in Shutdown */
	FDelegateHandle GraphTagAddedHandle;
	FDelegateHandle GraphTagRemovedHandle;
	FDelegateHandle GraphStatesReloadedHandle;
};