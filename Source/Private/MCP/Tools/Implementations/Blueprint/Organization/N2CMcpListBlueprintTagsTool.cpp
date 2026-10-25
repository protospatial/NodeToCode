// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpListBlueprintTagsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpTagUtils.h"
#include "Core/N2CTagManager.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpListBlueprintTagsTool)

FMcpToolDefinition FN2CMcpListBlueprintTagsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("list-blueprint-tags"),
		TEXT("Lists tags that have been applied to Blueprint graphs. Can filter by graph GUID, tag name, or category."),
		TEXT("Blueprint Organization")
	);
	
	// Define input schema
	TMap<FString, FString> Properties;
	Properties.Add(TEXT("graphGuid"), TEXT("string"));
	Properties.Add(TEXT("tag"), TEXT("string"));
	Properties.Add(TEXT("category"), TEXT("string"));
	
	// All parameters are optional
	TArray<FString> Required;
	
	Definition.InputSchema = BuildInputSchema(Properties, Required);
	
	// Add read-only annotation
	AddReadOnlyAnnotation(Definition);
	
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
			// Get tags for specific graph
			Tags = UN2CTagManager::Get().GetTagsForGraph(GraphGuid);
		}
		else if (!TagFilter.IsEmpty())
		{
			// Get graphs with specific tag
			Tags = UN2CTagManager::Get().GetGraphsWithTag(TagFilter, CategoryFilter);
		}
		else if (!CategoryFilter.IsEmpty())
		{
			// Get tags in specific category
			Tags = UN2CTagManager::Get().GetTagsInCategory(CategoryFilter);
		}
		else
		{
			// Get all tags
			Tags = UN2CTagManager::Get().GetAllTags();
		}
		
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
		TArray<FString> UniqueTagNames = UN2CTagManager::Get().GetAllTagNames();
		TArray<TSharedPtr<FJsonValue>> TagNamesArray;
		for (const FString& TagName : UniqueTagNames)
		{
			TagNamesArray.Add(MakeShareable(new FJsonValueString(TagName)));
		}
		SummaryObject->SetArrayField(TEXT("uniqueTagNames"), TagNamesArray);
		
		// Get unique categories
		TArray<FString> UniqueCategories = UN2CTagManager::Get().GetAllCategories();
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