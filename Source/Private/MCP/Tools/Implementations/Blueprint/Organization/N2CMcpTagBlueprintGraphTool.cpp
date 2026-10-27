// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpTagBlueprintGraphTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateTypes.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "MCP/Utils/N2CMcpTagUtils.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpTagBlueprintGraphTool)

FMcpToolDefinition FN2CMcpTagBlueprintGraphTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("tag-blueprint-graph"),
		TEXT("Tags a Blueprint graph (the one blueprintPath plus graphName or graphGuid name, else the focused graph) with a name and category for organization and tracking"),
		N2CMcpToolsets::BlueprintTag
	);
	
	// Define input schema
	TMap<FString, FString> Properties;
	Properties.Add(TEXT("tag"), TEXT("string"));
	Properties.Add(TEXT("category"), TEXT("string"));
	Properties.Add(TEXT("description"), TEXT("string"));
	
	TArray<FString> Required;
	Required.Add(TEXT("tag")); // Only tag is required
	
	Definition.InputSchema = BuildInputSchema(Properties, Required);
	FN2CGraphTarget::AddSchemaProperties(Definition);
	
	return Definition;
}

FMcpToolCallResult FN2CMcpTagBlueprintGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Execute on Game Thread since we need to access editor state
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Extract parameters
		FString Tag;
		if (!Arguments->TryGetStringField(TEXT("tag"), Tag) || Tag.IsEmpty())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: tag"));
		}
		
		FString Category;
		Arguments->TryGetStringField(TEXT("category"), Category);
		Category = FN2CTagEntry::NormalizeCategory(Category); // an explicit "" means Default, not "any category"
		
		FString Description;
		Arguments->TryGetStringField(TEXT("description"), Description);
		
		// The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0274). Tagging does not edit
		// the graph, so a read-only graph can be tagged
		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ false);
		if (!Target.IsValid())
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("tag-blueprint-graph tool failed: %s"), *Target.Error));
			return FMcpToolCallResult::CreateErrorResult(Target.Error);
		}
		UBlueprint* OwningBlueprint = Target.Blueprint;
		UEdGraph* FocusedGraph = Target.Graph;
		
		// Validate graph has a valid GUID
		if (!FocusedGraph->GraphGuid.IsValid())
		{
			FN2CLogger::Get().LogWarning(TEXT("tag-blueprint-graph tool failed: Current graph does not have a valid GUID"));
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Current graph is not valid for tagging")));
		}
		
		// Create the tagged graph struct
		FSoftObjectPath BlueprintPath(OwningBlueprint);
		FN2CTaggedBlueprintGraph TaggedGraph(
			Tag,
			Category,
			Description,
			FocusedGraph->GraphGuid,
			FocusedGraph->GetFName().ToString(),
			BlueprintPath
		);
		
		// Add the tag using the tag manager
		bool bSuccess = UN2CGraphStateManager::Get().AddTag(TaggedGraph);
		
		if (!bSuccess)
		{
			FN2CLogger::Get().LogError(TEXT("Failed to add tag to tag manager"));
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to persist tag")));
		}
		
		// Create success response
		FString Message = FString::Printf(TEXT("Successfully tagged %s with '%s'"), 
			*FocusedGraph->GetFName().ToString(), *Tag);
		
		TSharedPtr<FJsonObject> ResultObject = FN2CMcpTagUtils::CreateBaseResponse(true, Message);
		
		// Use the utility to convert the tag to JSON
		TSharedPtr<FJsonObject> TaggedGraphObject = FN2CMcpTagUtils::TagToJsonObject(TaggedGraph);
		ResultObject->SetObjectField(TEXT("taggedGraph"), TaggedGraphObject);
		
		// Convert to JSON string
		FString JsonString;
		if (!FN2CMcpTagUtils::SerializeToJsonString(ResultObject, JsonString))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize response"));
		}
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("tag-blueprint-graph tool: %s"), *Message), 
			EN2CLogSeverity::Info);
		
		return Target.Finish(FMcpToolCallResult::CreateTextResult(JsonString));
	});
}