// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Subsystems/N2CTagSubsystem.h"
#include "Core/N2CTagManager.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Utils/N2CLogger.h"
#include "Editor.h"

void UN2CTagSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	// Subscribe to tag manager events
	UN2CTagManager& TagManager = UN2CTagManager::Get();
	TagManager.OnBlueprintTagAdded.AddUObject(this, &UN2CTagSubsystem::HandleTagAdded);
	TagManager.OnBlueprintTagRemoved.AddUObject(this, &UN2CTagSubsystem::HandleTagRemoved);
	// Many tags changed at once (Clear All, bulk removal, migration, reload): same as a fresh load for listeners
	TagManager.OnTagsReloaded.AddUObject(this, &UN2CTagSubsystem::RefreshTags);
	
	FN2CLogger::Get().Log(TEXT("N2C Tag Subsystem initialized"), EN2CLogSeverity::Info);
	
	// Fire initial load event
	OnTagsLoaded.Broadcast();
}

void UN2CTagSubsystem::Deinitialize()
{
	// Unsubscribe from tag manager events (it may already be shut down when the editor exits)
	if (UN2CTagManager* TagManager = UN2CTagManager::GetIfExists())
	{
		TagManager->OnBlueprintTagAdded.RemoveAll(this);
		TagManager->OnBlueprintTagRemoved.RemoveAll(this);
		TagManager->OnTagsReloaded.RemoveAll(this);
	}
	
	FN2CLogger::Get().Log(TEXT("N2C Tag Subsystem deinitialized"), EN2CLogSeverity::Info);
	
	Super::Deinitialize();
}

UN2CTagSubsystem* UN2CTagSubsystem::Get()
{
	if (GEditor)
	{
		return GEditor->GetEditorSubsystem<UN2CTagSubsystem>();
	}
	return nullptr;
}

void UN2CTagSubsystem::GetFocusedGraphInfo(
	bool& bIsValid,
	FString& GraphGuid,
	FString& GraphName,
	FString& BlueprintPath)
{
	bIsValid = false;
	GraphGuid.Empty();
	GraphName.Empty();
	BlueprintPath.Empty();
	
	// Get the focused graph and owning Blueprint
	UBlueprint* OwningBlueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	
	if (FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
	{
		if (FocusedGraph && FocusedGraph->GraphGuid.IsValid())
		{
			bIsValid = true;
			GraphGuid = FocusedGraph->GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
			GraphName = FocusedGraph->GetFName().ToString();
			
			if (OwningBlueprint)
			{
				FSoftObjectPath Path(OwningBlueprint);
				BlueprintPath = Path.ToString();
			}
		}
	}
}

void UN2CTagSubsystem::RefreshTags()
{
	// Simply broadcast the loaded event to trigger UI refresh
	OnTagsLoaded.Broadcast();
	
	FN2CLogger::Get().Log(TEXT("Tag refresh requested"), EN2CLogSeverity::Info);
}

void UN2CTagSubsystem::HandleTagAdded(const FN2CTaggedBlueprintGraph& TaggedGraph)
{
	// Convert to Blueprint-friendly struct and broadcast
	FN2CTagInfo TagInfo = FN2CTagInfo::FromTaggedGraph(TaggedGraph);
	OnBlueprintTagAdded.Broadcast(TagInfo);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Broadcasting tag added event: %s"), *TaggedGraph.Tag), 
		EN2CLogSeverity::Debug);
}

void UN2CTagSubsystem::HandleTagRemoved(const FGuid& GraphGuid, const FString& Tag)
{
	// Broadcast with string GUID
	FString GraphGuidString = GraphGuid.ToString(EGuidFormats::DigitsWithHyphens);
	OnBlueprintTagRemoved.Broadcast(GraphGuidString, Tag);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Broadcasting tag removed event: %s"), *Tag), 
		EN2CLogSeverity::Debug);
}