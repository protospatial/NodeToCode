// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "Core/N2CTagManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Utils/N2CLogger.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"

// FN2CTagInfo Implementation
FN2CTagInfo FN2CTagInfo::FromTaggedGraph(const FN2CTaggedBlueprintGraph& TaggedGraph)
{
	FN2CTagInfo Info;
	Info.Tag = TaggedGraph.Tag;
	Info.Category = TaggedGraph.Category;
	Info.Description = TaggedGraph.Description;
	Info.GraphGuid = TaggedGraph.GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
	Info.GraphName = TaggedGraph.GraphName;
	Info.BlueprintPath = TaggedGraph.OwningBlueprint.ToString();
	Info.Timestamp = TaggedGraph.Timestamp;
	return Info;
}

FN2CTaggedBlueprintGraph FN2CTagInfo::ToTaggedGraph() const
{
	FN2CTaggedBlueprintGraph TaggedGraph;
	TaggedGraph.Tag = Tag;
	TaggedGraph.Category = Category;
	TaggedGraph.Description = Description;
	FGuid::Parse(GraphGuid, TaggedGraph.GraphGuid);
	TaggedGraph.GraphName = GraphName;
	TaggedGraph.OwningBlueprint = FSoftObjectPath(BlueprintPath);
	TaggedGraph.Timestamp = Timestamp;
	return TaggedGraph;
}

// UN2CTagBlueprintLibrary Implementation
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
	
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
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
	bSuccess = UN2CTagManager::Get().AddTag(TaggedGraph);
	
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
	
	// Find the graph
	UEdGraph* TargetGraph = nullptr;
	
	if (GraphName.IsEmpty())
	{
		// Use the first graph
		if (BlueprintAsset->UbergraphPages.Num() > 0)
		{
			TargetGraph = BlueprintAsset->UbergraphPages[0];
		}
		else if (BlueprintAsset->FunctionGraphs.Num() > 0)
		{
			TargetGraph = BlueprintAsset->FunctionGraphs[0];
		}
	}
	else
	{
		// Find graph by name
		for (UEdGraph* Graph : BlueprintAsset->UbergraphPages)
		{
			if (Graph && Graph->GetFName().ToString() == GraphName)
			{
				TargetGraph = Graph;
				break;
			}
		}
		
		if (!TargetGraph)
		{
			for (UEdGraph* Graph : BlueprintAsset->FunctionGraphs)
			{
				if (Graph && Graph->GetFName().ToString() == GraphName)
				{
					TargetGraph = Graph;
					break;
				}
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
	bSuccess = UN2CTagManager::Get().AddTag(TaggedGraph);
	
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
	const FString& Category)
{
	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("RemoveTag: Invalid GUID format: %s"), *GraphGuid));
		return false;
	}
	
	return UN2CTagManager::Get().RemoveTag(Guid, Tag, Category);
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
	
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsForGraph(Guid);
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
	
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetGraphsWithTag(Tag, Category);
	for (const FN2CTaggedBlueprintGraph& TaggedGraph : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(TaggedGraph));
	}
	
	return Result;
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::GetTagsInCategory(const FString& Category)
{
	TArray<FN2CTagInfo> Result;
	
	TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsInCategory(Category);
	for (const FN2CTaggedBlueprintGraph& Tag : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(Tag));
	}
	
	return Result;
}

TArray<FN2CTagInfo> UN2CTagBlueprintLibrary::GetAllTags()
{
	TArray<FN2CTagInfo> Result;
	
	const TArray<FN2CTaggedBlueprintGraph>& Tags = UN2CTagManager::Get().GetAllTags();
	for (const FN2CTaggedBlueprintGraph& Tag : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(Tag));
	}
	
	return Result;
}

TArray<FString> UN2CTagBlueprintLibrary::GetAllTagNames()
{
	return UN2CTagManager::Get().GetAllTagNames();
}

TArray<FString> UN2CTagBlueprintLibrary::GetAllCategories()
{
	return UN2CTagManager::Get().GetAllCategories();
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
	
	return UN2CTagManager::Get().GraphHasTag(Guid, Tag, Category);
}

int32 UN2CTagBlueprintLibrary::ClearAllTags()
{
	int32 TagCount = UN2CTagManager::Get().GetAllTags().Num();
	UN2CTagManager::Get().ClearAllTags();
	return TagCount;
}

bool UN2CTagBlueprintLibrary::SaveTags()
{
	return UN2CTagManager::Get().SaveTags();
}

bool UN2CTagBlueprintLibrary::LoadTags()
{
	return UN2CTagManager::Get().LoadTags();
}

void UN2CTagBlueprintLibrary::GetTagStatistics(
	int32& TotalTags,
	int32& UniqueGraphs,
	int32& UniqueTagNames,
	int32& UniqueCategories)
{
	const TArray<FN2CTaggedBlueprintGraph>& AllTags = UN2CTagManager::Get().GetAllTags();
	TotalTags = AllTags.Num();
	
	// Count unique graphs
	TSet<FGuid> UniqueGraphGuids;
	for (const FN2CTaggedBlueprintGraph& Tag : AllTags)
	{
		UniqueGraphGuids.Add(Tag.GraphGuid);
	}
	UniqueGraphs = UniqueGraphGuids.Num();
	
	// Get unique counts from manager
	UniqueTagNames = UN2CTagManager::Get().GetAllTagNames().Num();
	UniqueCategories = UN2CTagManager::Get().GetAllCategories().Num();
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
	
	const TArray<FN2CTaggedBlueprintGraph>& AllTags = UN2CTagManager::Get().GetAllTags();
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