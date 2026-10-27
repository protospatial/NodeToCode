// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpListBlueprintTagsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpTagUtils.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpListBlueprintTagsTool)

FMcpToolDefinition FN2CMcpListBlueprintTagsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("list-blueprint-tags"),
		TEXT("Lists tags that have been applied to Blueprint graphs. Filters by graph GUID, tag name and category, all optional; every filter given applies (case-insensitive tag and category)."),
		N2CMcpToolsets::BlueprintTag
	);
	
	// Define input schema
	TMap<FString, FString> Properties;
	Properties.Add(TEXT("graphGuid"), TEXT("string"));
	Properties.Add(TEXT("tag"), TEXT("string"));
	Properties.Add(TEXT("category"), TEXT("string"));
	
	// All parameters are optional
	TArray<FString> Required;
	
	Definition.InputSchema = BuildInputSchema(Properties, Required);
	
	// Read-only: reads editor or project state and changes nothing
	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
	
	// Results carry Blueprint text (tag names and categories users typed): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpListBlueprintTagsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Execute on Game Thread to access tag manager
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Extract optional filter parameters
		FString GraphGuidString;
		FGuid GraphGuid;
		bool bFilterByGraph = false;
		
		if (Arguments->TryGetStringField(TEXT("graphGuid"), GraphGuidString) && !GraphGuidString.IsEmpty())
		{
			FString GuidError;
			if (FN2CMcpTagUtils::ValidateAndParseGuid(GraphGuidString, GraphGuid, GuidError))
			{
				bFilterByGraph = true;
			}
			else
			{
				return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Invalid graphGuid format: %s"), *GuidError));
			}
		}
		
		FString TagFilter;
		Arguments->TryGetStringField(TEXT("tag"), TagFilter);
		
		FString CategoryFilter;
		Arguments->TryGetStringField(TEXT("category"), CategoryFilter);
		
		// Get tags based on filters
		TArray<FN2CTaggedBlueprintGraph> Tags;
		
		if (bFilterByGraph)
		{
			// Get tags for specific graph: in every Blueprint that has it (a duplicated Blueprint keeps its graph GUIDs,
			// and each row names its Blueprint)
			for (const FN2CGraphKey& Owner : UN2CGraphStateManager::Get().FindKeysForGuid(GraphGuid))
			{
				Tags.Append(UN2CGraphStateManager::Get().GetTaggedGraphs(Owner));
			}
		}
		else if (!TagFilter.IsEmpty())
		{
			// Get graphs with specific tag
			Tags = UN2CGraphStateManager::Get().FindTaggedGraphs(TagFilter, CategoryFilter);
		}
		else if (!CategoryFilter.IsEmpty())
		{
			// Get tags in specific category
			Tags = UN2CGraphStateManager::Get().FindTaggedGraphsInCategory(CategoryFilter);
		}
		else
		{
			// Get all tags
			Tags = UN2CGraphStateManager::Get().GetAllTags();
		}

		// Every filter given applies together (card 0294): a graphGuid list is narrowed by tag and category too, so
		// appliedFilters below names only filters that shaped the result
		Tags.RemoveAll([&TagFilter, &CategoryFilter](const FN2CTaggedBlueprintGraph& Entry)
		{
			return (!TagFilter.IsEmpty() && !Entry.Tag.Equals(TagFilter, ESearchCase::IgnoreCase))
				|| (!CategoryFilter.IsEmpty() && !Entry.Category.Equals(CategoryFilter, ESearchCase::IgnoreCase));
		});
		
		// Create response JSON
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		
		// Add tags array
		TArray<TSharedPtr<FJsonValue>> TagsArray;
		for (const FN2CTaggedBlueprintGraph& Tag : Tags)
		{
			TSharedPtr<FJsonObject> TagObject = FN2CMcpTagUtils::TagToJsonObject(Tag);
			TagsArray.Add(MakeShareable(new FJsonValueObject(TagObject)));
		}
		
		ResultObject->SetArrayField(TEXT("tags"), TagsArray);
		ResultObject->SetNumberField(TEXT("count"), Tags.Num());
		
		// Add summary of unique values
		TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
		
		// Get unique tag names
		TArray<FString> UniqueTagNames = UN2CGraphStateManager::Get().GetAllTagNames();
		TArray<TSharedPtr<FJsonValue>> TagNamesArray;
		for (const FString& TagName : UniqueTagNames)
		{
			TagNamesArray.Add(MakeShareable(new FJsonValueString(TagName)));
		}
		SummaryObject->SetArrayField(TEXT("uniqueTagNames"), TagNamesArray);
		
		// Get unique categories
		TArray<FString> UniqueCategories = UN2CGraphStateManager::Get().GetAllCategories();
		TArray<TSharedPtr<FJsonValue>> CategoriesArray;
		for (const FString& Category : UniqueCategories)
		{
			CategoriesArray.Add(MakeShareable(new FJsonValueString(Category)));
		}
		SummaryObject->SetArrayField(TEXT("uniqueCategories"), CategoriesArray);
		
		ResultObject->SetObjectField(TEXT("summary"), SummaryObject);
		
		// Add filter info
		TSharedPtr<FJsonObject> FilterObject = MakeShareable(new FJsonObject);
		if (bFilterByGraph)
		{
			FilterObject->SetStringField(TEXT("graphGuid"), GraphGuidString);
		}
		if (!TagFilter.IsEmpty())
		{
			FilterObject->SetStringField(TEXT("tag"), TagFilter);
		}
		if (!CategoryFilter.IsEmpty())
		{
			FilterObject->SetStringField(TEXT("category"), CategoryFilter);
		}
		ResultObject->SetObjectField(TEXT("appliedFilters"), FilterObject);
		
		// Convert to JSON string
		FString JsonString;
		if (!FN2CMcpTagUtils::SerializeToJsonString(ResultObject, JsonString))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize response"));
		}
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("list-blueprint-tags tool: Found %d tags"), Tags.Num()), 
			EN2CLogSeverity::Info);
		
		return FMcpToolCallResult::CreateTextResult(JsonString);
	});
}