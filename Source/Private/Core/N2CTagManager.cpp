// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CTagManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"

UN2CTagManager* UN2CTagManager::Instance = nullptr;

UN2CTagManager& UN2CTagManager::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UN2CTagManager>(GetTransientPackage(), UN2CTagManager::StaticClass(), NAME_None, RF_Standalone);
		Instance->AddToRoot(); // Prevent garbage collection
		Instance->Initialize();
	}
	return *Instance;
}

UN2CTagManager::UN2CTagManager()
{
}

void UN2CTagManager::Initialize()
{
	FN2CLogger::Get().Log(TEXT("Initializing Tag Manager (delegating to Graph State Manager)"), EN2CLogSeverity::Info);

	// Ensure the graph state manager is initialized
	UN2CGraphStateManager::Get();

	// Subscribe to graph state manager events and forward them to our delegates
	GraphTagAddedHandle = UN2CGraphStateManager::Get().OnGraphTagAdded.AddLambda([this](const FN2CGraphState& GraphState)
	{
		FN2CTaggedBlueprintGraph AddedTag;
		if (MakeTagAddedEvent(GraphState, AddedTag))
		{
			OnBlueprintTagAdded.Broadcast(AddedTag);
		}
	});

	GraphTagRemovedHandle = UN2CGraphStateManager::Get().OnGraphTagRemoved.AddLambda([this](const FN2CGraphKey& Graph, const FString& Tag)
	{
		OnBlueprintTagRemoved.Broadcast(Graph, Tag);
	});

	GraphStatesReloadedHandle = UN2CGraphStateManager::Get().OnGraphStatesReloaded.AddLambda([this]()
	{
		OnTagsReloaded.Broadcast();
	});
}

bool UN2CTagManager::MakeTagAddedEvent(const FN2CGraphState& GraphState, FN2CTaggedBlueprintGraph& OutTag)
{
	// AddTag appends the new entry and then broadcasts, so the tag just added is the last one
	if (GraphState.Tags.Num() == 0)
	{
		return false;
	}
	const FN2CTagEntry& TagEntry = GraphState.Tags.Last();
	OutTag.Tag = TagEntry.Tag;
	OutTag.Category = TagEntry.Category;
	OutTag.Description = TagEntry.Description;
	OutTag.GraphGuid = GraphState.GraphGuid;
	OutTag.GraphName = GraphState.GraphName;
	OutTag.OwningBlueprint = GraphState.OwningBlueprint;
	OutTag.Timestamp = TagEntry.Timestamp;
	return true;
}

void UN2CTagManager::Shutdown()
{
	FN2CLogger::Get().Log(TEXT("Shutting down Tag Manager"), EN2CLogSeverity::Info);

	// The graph state manager is shut down separately, after this, by FNodeToCodeModule::ShutdownStateManagers
	if (UN2CGraphStateManager* GraphStateManager = UN2CGraphStateManager::GetIfExists())
	{
		GraphStateManager->OnGraphTagAdded.Remove(GraphTagAddedHandle);
		GraphStateManager->OnGraphTagRemoved.Remove(GraphTagRemovedHandle);
		GraphStateManager->OnGraphStatesReloaded.Remove(GraphStatesReloadedHandle);
	}
	GraphTagAddedHandle.Reset();
	GraphTagRemovedHandle.Reset();
	GraphStatesReloadedHandle.Reset();

	if (Instance == this)
	{
		RemoveFromRoot();
		Instance = nullptr;
	}
}

void UN2CTagManager::ShutdownInstance()
{
	if (Instance)
	{
		Instance->Shutdown();
	}
}

bool UN2CTagManager::AddTag(const FN2CTaggedBlueprintGraph& TaggedGraph)
{
	return UN2CGraphStateManager::Get().AddTag(TaggedGraph);
}

bool UN2CTagManager::RemoveTag(const FN2CGraphKey& GraphGuid, const FString& Tag, const FString& Category)
{
	return UN2CGraphStateManager::Get().RemoveTag(GraphGuid, Tag, Category);
}

int32 UN2CTagManager::RemoveTagByName(const FN2CGraphKey& GraphGuid, const FString& Tag, FN2CTaggedBlueprintGraph& OutRemovedTag)
{
	// Get the tags before removal so we can populate OutRemovedTag
	TArray<FN2CTagEntry> TagsBefore = UN2CGraphStateManager::Get().GetTagsForGraph(GraphGuid);
	const FN2CGraphState* State = UN2CGraphStateManager::Get().FindGraphState(GraphGuid);

	// Find the first matching tag to return
	bool bFoundFirst = false;
	for (const FN2CTagEntry& TagEntry : TagsBefore)
	{
		if (TagEntry.Tag.Equals(Tag, ESearchCase::IgnoreCase))
		{
			if (!bFoundFirst && State)
			{
				OutRemovedTag.Tag = TagEntry.Tag;
				OutRemovedTag.Category = TagEntry.Category;
				OutRemovedTag.Description = TagEntry.Description;
				OutRemovedTag.GraphGuid = State->GraphGuid;
				OutRemovedTag.GraphName = State->GraphName;
				OutRemovedTag.OwningBlueprint = State->OwningBlueprint;
				OutRemovedTag.Timestamp = TagEntry.Timestamp;
				bFoundFirst = true;
			}
		}
	}

	return UN2CGraphStateManager::Get().RemoveTagByName(GraphGuid, Tag);
}

int32 UN2CTagManager::RemoveAllTagsFromGraph(const FN2CGraphKey& GraphGuid)
{
	return UN2CGraphStateManager::Get().RemoveAllTagsFromGraph(GraphGuid);
}

TArray<FN2CTaggedBlueprintGraph> UN2CTagManager::GetTagsForGraph(const FN2CGraphKey& GraphGuid) const
{
	TArray<FN2CTaggedBlueprintGraph> Result;
	const FN2CGraphState* State = UN2CGraphStateManager::Get().FindGraphState(GraphGuid);

	if (State)
	{
		for (const FN2CTagEntry& TagEntry : State->Tags)
		{
			FN2CTaggedBlueprintGraph LegacyTag;
			LegacyTag.Tag = TagEntry.Tag;
			LegacyTag.Category = TagEntry.Category;
			LegacyTag.Description = TagEntry.Description;
			LegacyTag.GraphGuid = State->GraphGuid;
			LegacyTag.GraphName = State->GraphName;
			LegacyTag.OwningBlueprint = State->OwningBlueprint;
			LegacyTag.Timestamp = TagEntry.Timestamp;
			Result.Add(LegacyTag);
		}
	}

	return Result;
}

TArray<FN2CTaggedBlueprintGraph> UN2CTagManager::GetGraphsWithTag(const FString& Tag, const FString& Category) const
{
	TArray<FN2CTaggedBlueprintGraph> Result;
	TArray<FN2CGraphState> MatchingStates = UN2CGraphStateManager::Get().GetGraphsWithTag(Tag, Category);

	for (const FN2CGraphState& State : MatchingStates)
	{
		for (const FN2CTagEntry& TagEntry : State.Tags)
		{
			// Only include tags that match the query
			bool bTagMatches = TagEntry.Tag.Equals(Tag, ESearchCase::IgnoreCase);
			bool bCategoryMatches = Category.IsEmpty() || TagEntry.Category.Equals(Category, ESearchCase::IgnoreCase);

			if (bTagMatches && bCategoryMatches)
			{
				FN2CTaggedBlueprintGraph LegacyTag;
				LegacyTag.Tag = TagEntry.Tag;
				LegacyTag.Category = TagEntry.Category;
				LegacyTag.Description = TagEntry.Description;
				LegacyTag.GraphGuid = State.GraphGuid;
				LegacyTag.GraphName = State.GraphName;
				LegacyTag.OwningBlueprint = State.OwningBlueprint;
				LegacyTag.Timestamp = TagEntry.Timestamp;
				Result.Add(LegacyTag);
			}
		}
	}

	return Result;
}

TArray<FN2CTaggedBlueprintGraph> UN2CTagManager::GetTagsInCategory(const FString& Category) const
{
	TArray<FN2CTaggedBlueprintGraph> Result;
	TArray<FN2CGraphState> MatchingStates = UN2CGraphStateManager::Get().GetGraphsInCategory(Category);

	for (const FN2CGraphState& State : MatchingStates)
	{
		for (const FN2CTagEntry& TagEntry : State.Tags)
		{
			if (TagEntry.Category.Equals(Category, ESearchCase::IgnoreCase))
			{
				FN2CTaggedBlueprintGraph LegacyTag;
				LegacyTag.Tag = TagEntry.Tag;
				LegacyTag.Category = TagEntry.Category;
				LegacyTag.Description = TagEntry.Description;
				LegacyTag.GraphGuid = State.GraphGuid;
				LegacyTag.GraphName = State.GraphName;
				LegacyTag.OwningBlueprint = State.OwningBlueprint;
				LegacyTag.Timestamp = TagEntry.Timestamp;
				Result.Add(LegacyTag);
			}
		}
	}

	return Result;
}

TArray<FString> UN2CTagManager::GetAllTagNames() const
{
	return UN2CGraphStateManager::Get().GetAllTagNames();
}

TArray<FString> UN2CTagManager::GetAllCategories() const
{
	return UN2CGraphStateManager::Get().GetAllCategories();
}

bool UN2CTagManager::GraphHasTag(const FN2CGraphKey& GraphGuid, const FString& Tag, const FString& Category) const
{
	return UN2CGraphStateManager::Get().GraphHasTag(GraphGuid, Tag, Category);
}

const TArray<FN2CTaggedBlueprintGraph>& UN2CTagManager::GetAllTags() const
{
	// This is a bit tricky - we need to return a reference to a persistent array
	// We'll use a static cache that gets updated when needed
	static TArray<FN2CTaggedBlueprintGraph> CachedTags;
	CachedTags = UN2CGraphStateManager::Get().GetAllTagsLegacy();
	return CachedTags;
}

void UN2CTagManager::ClearAllTags()
{
	// Only clear tags, not translation/export state (one pass, one save, one OnTagsReloaded)
	UN2CGraphStateManager::Get().ClearAllTags();
}

bool UN2CTagManager::SaveTags()
{
	// Tags are auto-saved by the graph state manager
	return UN2CGraphStateManager::Get().SaveState();
}

bool UN2CTagManager::LoadTags()
{
	// Tags are auto-loaded by the graph state manager during initialization
	return UN2CGraphStateManager::Get().LoadState();
}
