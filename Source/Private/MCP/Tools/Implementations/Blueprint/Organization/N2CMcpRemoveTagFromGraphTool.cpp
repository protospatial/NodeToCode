// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRemoveTagFromGraphTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpTagUtils.h"
#include "Core/N2CTagManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Dom/JsonObject.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpRemoveTagFromGraphTool)

FMcpToolDefinition FN2CMcpRemoveTagFromGraphTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("remove-tag-from-graph"),
		TEXT("Removes a specific tag from a Blueprint graph by its GUID and tag name"),
		N2CMcpToolsets::BlueprintTag
	);
	
	// Define input schema
	TMap<FString, FString> Properties;
	Properties.Add(TEXT("graphGuid"), TEXT("string"));
	Properties.Add(TEXT("tag"), TEXT("string"));
	
	TArray<FString> Required;
	Required.Add(TEXT("graphGuid"));
	Required.Add(TEXT("tag"));
	
	Definition.InputSchema = BuildInputSchema(Properties, Required);
	
	return Definition;
}

FMcpToolCallResult FN2CMcpRemoveTagFromGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Execute on Game Thread since we need to access the tag manager
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Extract and validate graphGuid parameter
		FString GraphGuidString;
		if (!Arguments->TryGetStringField(TEXT("graphGuid"), GraphGuidString))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: graphGuid"));
		}
		
		FGuid GraphGuid;
		FString GuidError;
		if (!FN2CMcpTagUtils::ValidateAndParseGuid(GraphGuidString, GraphGuid, GuidError))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("remove-tag-from-graph tool: Invalid GUID - %s"), *GuidError));
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Invalid graph GUID format: %s"), *GuidError));
		}
		
		// Extract tag parameter
		FString Tag;
		if (!Arguments->TryGetStringField(TEXT("tag"), Tag) || Tag.IsEmpty())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: tag"));
		}
		
		// A duplicated Blueprint keeps its graph GUIDs: a GUID in several Blueprints cannot say which to change
		const TArray<FN2CGraphKey> Owners = UN2CGraphStateManager::Get().FindKeysForGuid(GraphGuid);
		if (Owners.Num() > 1)
		{
			TArray<FString> Paths;
			for (const FN2CGraphKey& Owner : Owners)
			{
				Paths.Add(Owner.Blueprint.ToString());
			}
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("Graph %s has tags in %d Blueprints (copies keep their graph GUIDs): %s. Remove the tag from the Tag Manager or the graph's overlay instead."),
				*GraphGuidString, Owners.Num(), *FString::Join(Paths, TEXT(", "))));
		}

		// Get the current tag count for the graph before removal
		TArray<FN2CTaggedBlueprintGraph> CurrentTags = UN2CTagManager::Get().GetTagsForGraph(GraphGuid);
		int32 InitialTagCount = CurrentTags.Num();
		
		// Remove the tag(s) using the tag manager
		FN2CTaggedBlueprintGraph RemovedTag;
		int32 RemovedCount = UN2CTagManager::Get().RemoveTagByName(GraphGuid, Tag, RemovedTag);
		
		// Create response based on result
		bool bSuccess = true; // Always true for idempotent operation
		FString Message;
		
		if (RemovedCount == 0)
		{
			// Tag not found - this is still success (idempotent)
			Message = FString::Printf(TEXT("Tag '%s' was not found on graph %s (no action taken)"), 
				*Tag, *GraphGuidString);
			
			FN2CLogger::Get().Log(FString::Printf(TEXT("remove-tag-from-graph tool: %s"), *Message), 
				EN2CLogSeverity::Info);
		}
		else if (RemovedCount == 1)
		{
			Message = FString::Printf(TEXT("Tag '%s' removed from graph"), *Tag);
			
			FN2CLogger::Get().Log(FString::Printf(TEXT("remove-tag-from-graph tool: %s"), *Message), 
				EN2CLogSeverity::Info);
		}
		else
		{
			Message = FString::Printf(TEXT("Removed %d instances of tag '%s' from graph"), RemovedCount, *Tag);
			
			FN2CLogger::Get().Log(FString::Printf(TEXT("remove-tag-from-graph tool: %s"), *Message), 
				EN2CLogSeverity::Info);
		}
		
		// Build response JSON
		TSharedPtr<FJsonObject> ResultObject = FN2CMcpTagUtils::CreateBaseResponse(bSuccess, Message);
		
		// Add removed tag info if a tag was removed
		if (RemovedCount > 0)
		{
			TSharedPtr<FJsonObject> RemovedTagObject = MakeShareable(new FJsonObject);
			RemovedTagObject->SetStringField(TEXT("tag"), RemovedTag.Tag);
			RemovedTagObject->SetStringField(TEXT("category"), RemovedTag.Category);
			RemovedTagObject->SetStringField(TEXT("graphGuid"), GraphGuidString);
			
			ResultObject->SetObjectField(TEXT("removedTag"), RemovedTagObject);
		}
		
		// Add remaining tags count
		int32 RemainingTags = InitialTagCount - RemovedCount;
		ResultObject->SetNumberField(TEXT("remainingTags"), RemainingTags);
		
		// Serialize and return
		FString JsonString;
		if (!FN2CMcpTagUtils::SerializeToJsonString(ResultObject, JsonString))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize response"));
		}
		
		return FMcpToolCallResult::CreateTextResult(JsonString);
	});
}