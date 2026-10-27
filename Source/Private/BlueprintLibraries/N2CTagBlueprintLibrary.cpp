// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"

// UN2CTagBlueprintLibrary Implementation

bool UN2CTagBlueprintLibrary::GetFocusedGraph(UBlueprint*& OutBlueprint, UEdGraph*& OutGraph, FString& OutError)
{
	// The graph the focused Blueprint editor shows (core, not the MCP utilities: card 0345)
	OutBlueprint = nullptr;
	OutGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
	if (!OutGraph)
	{
		OutError = TEXT("NO_FOCUSED_GRAPH: No graph is currently focused in the active Blueprint editor.");
		return false;
	}
	OutBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(OutGraph);
	if (!OutBlueprint)
	{
		OutError = FString::Printf(TEXT("INTERNAL_ERROR: Could not find owning Blueprint for focused graph: %s"), *OutGraph->GetName());
		return false;
	}
	return true;
}
FN2CTagInfo UN2CTagBlueprintLibrary::TagFocusedBlueprintGraph(
	const FString& Tag,
	const FString& Category,
	const FString& Description,
	bool& bSuccess,
	FString& ErrorMessage)
{
	FN2CTagInfo ResultInfo;
	bSuccess = false;
	
	if (Tag.IsEmpty())
	{
		ErrorMessage = TEXT("Tag name cannot be empty");
		return ResultInfo;
	}
	
	// Get the focused graph and owning Blueprint
	UBlueprint* OwningBlueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	
	if (!GetFocusedGraph(OwningBlueprint, FocusedGraph, GraphError))
	{
		ErrorMessage = GraphError;
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("TagFocusedBlueprintGraph failed: %s"), *GraphError));
		return ResultInfo;
	}
	
	// Validate graph has a valid GUID
	if (!FocusedGraph->GraphGuid.IsValid())
	{
		ErrorMessage = TEXT("Current graph does not have a valid GUID");
		FN2CLogger::Get().LogWarning(TEXT("TagFocusedBlueprintGraph failed: Graph has no valid GUID"));
		return ResultInfo;
	}
	
	// Create the tagged graph struct
	FSoftObjectPath BlueprintPath(OwningBlueprint);
	const FString FinalCategory = FN2CTagEntry::NormalizeCategory(Category);
	FN2CTaggedBlueprintGraph TaggedGraph(
		Tag,
		FinalCategory,
		Description,
		FocusedGraph->GraphGuid,
		FocusedGraph->GetFName().ToString(),
		BlueprintPath
	);
	
	// Add the tag using the tag manager
	bSuccess = UN2CGraphStateManager::Get().AddTag(TaggedGraph);
	
	if (bSuccess)
	{
		ResultInfo = FN2CTagInfo::FromTaggedGraph(TaggedGraph);
		FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully tagged graph %s with '%s'"), 
			*FocusedGraph->GetFName().ToString(), *Tag), EN2CLogSeverity::Info);
	}
	else
	{
		ErrorMessage = TEXT("Failed to add tag to tag manager");
		FN2CLogger::Get().LogError(TEXT("Failed to add tag to tag manager"));
	}
	
	return ResultInfo;
}

FN2CTagInfo UN2CTagBlueprintLibrary::TagBlueprintGraph(
	UBlueprint* BlueprintAsset,
	const FString& GraphName,
	const FString& Tag,
	const FString& Category,
	const FString& Description,
	bool& bSuccess,
	FString& ErrorMessage)
{
	FN2CTagInfo ResultInfo;
	bSuccess = false;
	
	if (!BlueprintAsset)
	{
		ErrorMessage = TEXT("Blueprint asset is null");
		return ResultInfo;
	}
	
	if (Tag.IsEmpty())
	{
		ErrorMessage = TEXT("Tag name cannot be empty");
		return ResultInfo;
	}
	
	// Find the graph among every graph of the Blueprint (card 0294): event graphs, functions, macros, delegate
	// signatures and collapsed graphs, in FBlueprintEditorUtils::GetAllGraphs order
	UEdGraph* TargetGraph = nullptr;
	TArray<UEdGraph*> AllGraphs;
	BlueprintAsset->GetAllGraphs(AllGraphs);
	
	if (GraphName.IsEmpty())
	{
		// Use the first graph: the first event graph, else the first function, else any graph
		if (BlueprintAsset->UbergraphPages.Num() > 0)
		{
			TargetGraph = BlueprintAsset->UbergraphPages[0];
		}
		else if (BlueprintAsset->FunctionGraphs.Num() > 0)
		{
			TargetGraph = BlueprintAsset->FunctionGraphs[0];
		}
		else if (AllGraphs.Num() > 0)
		{
			TargetGraph = AllGraphs[0];
		}
	}
	else
	{
		// Find graph by name
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetFName().ToString() == GraphName)
			{
				TargetGraph = Graph;
				break;
			}
		}
	}
	
	if (!TargetGraph)
	{
		ErrorMessage = GraphName.IsEmpty() ? 
			TEXT("No graphs found in Blueprint") : 
			FString::Printf(TEXT("Graph '%s' not found in Blueprint"), *GraphName);
		return ResultInfo;
	}
	
	// Validate graph has a valid GUID
	if (!TargetGraph->GraphGuid.IsValid())
	{
		ErrorMessage = TEXT("Target graph does not have a valid GUID");
		return ResultInfo;
	}
	
	// Create the tagged graph struct
	FSoftObjectPath BlueprintPath(BlueprintAsset);
	const FString FinalCategory = FN2CTagEntry::NormalizeCategory(Category);
	FN2CTaggedBlueprintGraph TaggedGraph(
		Tag,
		FinalCategory,
		Description,
		TargetGraph->GraphGuid,
		TargetGraph->GetFName().ToString(),
		BlueprintPath
	);
	
	// Add the tag using the tag manager
	bSuccess = UN2CGraphStateManager::Get().AddTag(TaggedGraph);
	
	if (bSuccess)
	{
		ResultInfo = FN2CTagInfo::FromTaggedGraph(TaggedGraph);
		FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully tagged graph %s with '%s'"), 
			*TargetGraph->GetFName().ToString(), *Tag), EN2CLogSeverity::Info);
	}
	else
	{
		ErrorMessage = TEXT("Failed to add tag to tag manager");
		FN2CLogger::Get().LogError(TEXT("Failed to add tag to tag manager"));
	}
	
	return ResultInfo;
}

bool UN2CTagBlueprintLibrary::RemoveTag(
	const FString& GraphGuid,
	const FString& Tag,
	const FString& Category,
	const FString& BlueprintPath)
{
	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("RemoveTag: Invalid GUID format: %s"), *GraphGuid));
		return false;
	}

	// The graph is {Blueprint, GraphGuid} (card 0192): a GUID alone names a graph only while one Blueprint has it
	// (a duplicated Blueprint keeps the original's graph GUIDs), so the Blueprint path picks the copy (card 0344, as #198)
	const FN2CGraphKey Key = BlueprintPath.TrimStartAndEnd().IsEmpty()
		? FN2CGraphKey(Guid)
		: FN2CGraphKey(FN2CGraphKey::NormalizeBlueprintPath(BlueprintPath), Guid);
	return UN2CGraphStateManager::Get().RemoveTag(Key, Tag, Category);
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::GetTagsForGraph(const FString& GraphGuid)
{
	TArray<FN2CTagInfo> Result;
	
	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("GetTagsForGraph: Invalid GUID format: %s"), *GraphGuid));
		return Result;
	}
	
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CGraphStateManager::Get().GetTaggedGraphs(Guid);
	for (const FN2CTaggedBlueprintGraph& Tag : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(Tag));
	}
	
	return Result;
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::GetGraphsWithTag(
	const FString& Tag,
	const FString& Category)
{
	TArray<FN2CTagInfo> Result;
	
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CGraphStateManager::Get().FindTaggedGraphs(Tag, Category);
	for (const FN2CTaggedBlueprintGraph& TaggedGraph : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(TaggedGraph));
	}
	
	return Result;
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::GetTagsInCategory(const FString& Category)
{
	TArray<FN2CTagInfo> Result;
	
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CGraphStateManager::Get().FindTaggedGraphsInCategory(Category);
	for (const FN2CTaggedBlueprintGraph& Tag : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(Tag));
	}
	
	return Result;
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::GetAllTags()
{
	TArray<FN2CTagInfo> Result;
	
	const TArray<FN2CTaggedBlueprintGraph> Tags = UN2CGraphStateManager::Get().GetAllTags();
	for (const FN2CTaggedBlueprintGraph& Tag : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(Tag));
	}
	
	return Result;
}

TArray<FString> UN2CTagBlueprintLibrary::GetAllTagNames()
{
	return UN2CGraphStateManager::Get().GetAllTagNames();
}

TArray<FString> UN2CTagBlueprintLibrary::GetAllCategories()
{
	return UN2CGraphStateManager::Get().GetAllCategories();
}

bool UN2CTagBlueprintLibrary::GraphHasTag(
	const FString& GraphGuid,
	const FString& Tag,
	const FString& Category)
{
	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("GraphHasTag: Invalid GUID format: %s"), *GraphGuid));
		return false;
	}
	
	return UN2CGraphStateManager::Get().GraphHasTag(Guid, Tag, Category);
}

int32 UN2CTagBlueprintLibrary::ClearAllTags()
{
	int32 TagCount = UN2CGraphStateManager::Get().GetAllTags().Num();
	UN2CGraphStateManager::Get().ClearAllTags();
	return TagCount;
}

bool UN2CTagBlueprintLibrary::SaveTags()
{
	return UN2CGraphStateManager::Get().SaveState();
}

bool UN2CTagBlueprintLibrary::LoadTags()
{
	return UN2CGraphStateManager::Get().LoadState();
}

void UN2CTagBlueprintLibrary::GetTagStatistics(
	int32& TotalTags,
	int32& UniqueGraphs,
	int32& UniqueTagNames,
	int32& UniqueCategories)
{
	const TArray<FN2CTaggedBlueprintGraph> AllTags = UN2CGraphStateManager::Get().GetAllTags();
	TotalTags = AllTags.Num();
	
	// Count unique graphs
	TSet<FGuid> UniqueGraphGuids;
	for (const FN2CTaggedBlueprintGraph& Tag : AllTags)
	{
		UniqueGraphGuids.Add(Tag.GraphGuid);
	}
	UniqueGraphs = UniqueGraphGuids.Num();
	
	// Get unique counts from manager
	UniqueTagNames = UN2CGraphStateManager::Get().GetAllTagNames().Num();
	UniqueCategories = UN2CGraphStateManager::Get().GetAllCategories().Num();
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::SearchTags(
	const FString& SearchTerm,
	bool bSearchInDescription)
{
	TArray<FN2CTagInfo> Result;
	
	if (SearchTerm.IsEmpty())
	{
		return Result;
	}
	
	const TArray<FN2CTaggedBlueprintGraph> AllTags = UN2CGraphStateManager::Get().GetAllTags();
	FString SearchTermLower = SearchTerm.ToLower();
	
	for (const FN2CTaggedBlueprintGraph& Tag : AllTags)
	{
		bool bMatches = false;
		
		// Search in tag name
		if (Tag.Tag.ToLower().Contains(SearchTermLower))
		{
			bMatches = true;
		}
		// Search in category
		else if (Tag.Category.ToLower().Contains(SearchTermLower))
		{
			bMatches = true;
		}
		// Search in description if requested
		else if (bSearchInDescription && Tag.Description.ToLower().Contains(SearchTermLower))
		{
			bMatches = true;
		}
		
		if (bMatches)
		{
			Result.Add(FN2CTagInfo::FromTaggedGraph(Tag));
		}
	}

	return Result;
}

// ==================== Translation Functions ====================

bool UN2CTagBlueprintLibrary::HasTranslation(const FString& GraphGuid)
{
	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("HasTranslation: Invalid GUID format: %s"), *GraphGuid));
		return false;
	}

	return UN2CGraphStateManager::Get().HasTranslation(Guid);
}

bool UN2CTagBlueprintLibrary::LoadTranslation(const FString& GraphGuid, FN2CGraphTranslation& OutTranslation)
{
	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("LoadTranslation: Invalid GUID format: %s"), *GraphGuid));
		return false;
	}

	return UN2CGraphStateManager::Get().LoadTranslation(Guid, OutTranslation);
}

bool UN2CTagBlueprintLibrary::LoadTranslationFromTagInfo(const FN2CTagInfo& TagInfo, FN2CGraphTranslation& OutTranslation)
{
	// The tag's own Blueprint, so a duplicated Blueprint's graph (same GUID) loads its own translation
	const FN2CGraphKey Key = FN2CGraphKey::FromStrings(TagInfo.BlueprintPath, TagInfo.GraphGuid);
	if (!Key.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("LoadTranslationFromTagInfo: Invalid GUID format: %s"), *TagInfo.GraphGuid));
		return false;
	}
	return UN2CGraphStateManager::Get().LoadTranslation(Key, OutTranslation);
}