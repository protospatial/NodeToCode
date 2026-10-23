// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpReadContentBrowserPathTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpReadContentBrowserPathTool)

FMcpToolDefinition FN2CMcpReadContentBrowserPathTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("read-content-browser-path"),
		TEXT("Returns blueprint assets and folders at the specified path in the content browser")
	,
		TEXT("Content Browser")
	);
	
	// Define input schema
	TSharedPtr<FJsonObject> InputSchema = MakeShareable(new FJsonObject);
	InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	
	// Define properties
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// path property
	TSharedPtr<FJsonObject> PathProp = MakeShareable(new FJsonObject);
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Content browser path to read (e.g., '/Game/Blueprints')"));
	Properties->SetObjectField(TEXT("path"), PathProp);
	
	// page property
	TSharedPtr<FJsonObject> PageProp = MakeShareable(new FJsonObject);
	PageProp->SetStringField(TEXT("type"), TEXT("integer"));
	PageProp->SetStringField(TEXT("description"), TEXT("Page number for pagination (1-based)"));
	PageProp->SetNumberField(TEXT("default"), 1);
	PageProp->SetNumberField(TEXT("minimum"), 1);
	Properties->SetObjectField(TEXT("page"), PageProp);
	
	// page_size property
	TSharedPtr<FJsonObject> PageSizeProp = MakeShareable(new FJsonObject);
	PageSizeProp->SetStringField(TEXT("type"), TEXT("integer"));
	PageSizeProp->SetStringField(TEXT("description"), TEXT("Number of items per page"));
	PageSizeProp->SetNumberField(TEXT("default"), 50);
	PageSizeProp->SetNumberField(TEXT("minimum"), 1);
	PageSizeProp->SetNumberField(TEXT("maximum"), 200);
	Properties->SetObjectField(TEXT("page_size"), PageSizeProp);
	
	// filter_type property
	TSharedPtr<FJsonObject> FilterTypeProp = MakeShareable(new FJsonObject);
	FilterTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	FilterTypeProp->SetStringField(TEXT("description"), TEXT("Filter by asset type"));
	TArray<TSharedPtr<FJsonValue>> FilterTypeEnum;
	FilterTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("All"))));
	FilterTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("Blueprint"))));
	FilterTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("Material"))));
	FilterTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("Texture"))));
	FilterTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("StaticMesh"))));
	FilterTypeEnum.Add(MakeShareable(new FJsonValueString(TEXT("Folder"))));
	FilterTypeProp->SetArrayField(TEXT("enum"), FilterTypeEnum);
	FilterTypeProp->SetStringField(TEXT("default"), TEXT("All"));
	Properties->SetObjectField(TEXT("filter_type"), FilterTypeProp);
	
	// filter_name property
	TSharedPtr<FJsonObject> FilterNameProp = MakeShareable(new FJsonObject);
	FilterNameProp->SetStringField(TEXT("type"), TEXT("string"));
	FilterNameProp->SetStringField(TEXT("description"), TEXT("Filter by name contains (case-insensitive)"));
	FilterNameProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("filter_name"), FilterNameProp);
	
	// sync_browser property
	TSharedPtr<FJsonObject> SyncBrowserProp = MakeShareable(new FJsonObject);
	SyncBrowserProp->SetStringField(TEXT("type"), TEXT("boolean"));
	SyncBrowserProp->SetStringField(TEXT("description"), TEXT("Whether to sync the primary content browser to this path"));
	SyncBrowserProp->SetBoolField(TEXT("default"), true);
	Properties->SetObjectField(TEXT("sync_browser"), SyncBrowserProp);
	
	InputSchema->SetObjectField(TEXT("properties"), Properties);
	
	// Define required fields
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShareable(new FJsonValueString(TEXT("path"))));
	InputSchema->SetArrayField(TEXT("required"), Required);
	
	Definition.InputSchema = InputSchema;
	
	// Add read-only annotation
	AddReadOnlyAnnotation(Definition);
	
	return Definition;
}

FMcpToolCallResult FN2CMcpReadContentBrowserPathTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Execute on Game Thread since we need to interact with content browser subsystems
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// 1. Parse and validate arguments
		FString Path;
		int32 Page = 1;
		int32 PageSize = 50;
		FString FilterType = TEXT("All");
		FString FilterName;
		bool bSyncBrowser = true;
		
		if (!ParseArguments(Arguments, Path, Page, PageSize, FilterType, FilterName, bSyncBrowser))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Invalid arguments"));
		}
		
		// 2. Normalize and validate path
		Path = FN2CMcpContentBrowserUtils::NormalizeContentPath(Path);
		
		FString ValidationError;
		if (!FN2CMcpContentBrowserUtils::ValidateContentPath(Path, ValidationError))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("read-content-browser-path: Invalid path - %s"), *ValidationError));
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Invalid path: %s"), *ValidationError));
		}
		
		// 3. Check if path exists
		if (!FN2CMcpContentBrowserUtils::DoesPathExist(Path))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("read-content-browser-path: Path does not exist - %s"), *Path));
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Path does not exist: %s"), *Path));
		}
		
		// 4. Enumerate items at path as JSON
		TArray<TSharedPtr<FJsonObject>> AllJsonItems;
		bool bIncludeFolders = FilterType.Equals(TEXT("All")) || FilterType.Equals(TEXT("Folder"));
		bool bIncludeFiles = !FilterType.Equals(TEXT("Folder"));
		
		if (!FN2CMcpContentBrowserUtils::EnumerateItemsAtPathAsJson(Path, bIncludeFolders, bIncludeFiles, AllJsonItems))
		{
			FN2CLogger::Get().LogError(FString::Printf(TEXT("read-content-browser-path: Failed to enumerate items at %s"), *Path));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to enumerate items"));
		}
		
		// 5. Apply type filter
		TArray<TSharedPtr<FJsonObject>> TypeFilteredItems;
		FN2CMcpContentBrowserUtils::FilterJsonItemsByType(AllJsonItems, FilterType, TypeFilteredItems);
		
		// 6. Apply name filter
		TArray<TSharedPtr<FJsonObject>> FullyFilteredItems;
		FN2CMcpContentBrowserUtils::FilterJsonItemsByName(TypeFilteredItems, FilterName, FullyFilteredItems);
		
		// 7. Apply pagination
		int32 TotalCount = FullyFilteredItems.Num();
		int32 StartIndex = 0;
		int32 EndIndex = 0;
		bool bHasMore = false;
		
		if (!FN2CMcpContentBrowserUtils::CalculatePagination(TotalCount, Page, PageSize, StartIndex, EndIndex, bHasMore))
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("Page %d is out of range: %d items at page_size %d"), Page, TotalCount, PageSize));
		}
		
		// 8. Sync content browser if requested
		if (bSyncBrowser)
		{
			FN2CMcpContentBrowserUtils::NavigateToPath(Path);
		}
		
		// 9. Build result JSON
		TSharedPtr<FJsonObject> ResultJson = BuildResultJson(
			FullyFilteredItems,
			StartIndex,
			EndIndex,
			TotalCount,
			Page,
			PageSize,
			bHasMore
		);
		
		// 10. Serialize result
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		if (!FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize result"));
		}
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("read-content-browser-path: Found %d items at %s (showing %d-%d)"), 
			TotalCount, *Path, StartIndex + 1, EndIndex), EN2CLogSeverity::Info);
		
		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}

bool FN2CMcpReadContentBrowserPathTool::ParseArguments(const TSharedPtr<FJsonObject>& Arguments,
	FString& OutPath,
	int32& OutPage,
	int32& OutPageSize,
	FString& OutFilterType,
	FString& OutFilterName,
	bool& OutSyncBrowser) const
{
	// Required: path
	if (!Arguments->TryGetStringField(TEXT("path"), OutPath))
	{
		FN2CLogger::Get().LogWarning(TEXT("read-content-browser-path: Missing required parameter 'path'"));
		return false;
	}
	
	// Optional: page (default 1)
	if (!Arguments->TryGetNumberField(TEXT("page"), OutPage))
	{
		OutPage = 1;
	}
	else if (OutPage < 1)
	{
		FN2CLogger::Get().LogWarning(TEXT("read-content-browser-path: Page must be >= 1"));
		return false;
	}
	
	// Optional: page_size (default 50, max 200)
	if (!Arguments->TryGetNumberField(TEXT("page_size"), OutPageSize))
	{
		OutPageSize = 50;
	}
	else if (OutPageSize < 1 || OutPageSize > 200)
	{
		FN2CLogger::Get().LogWarning(TEXT("read-content-browser-path: Page size must be between 1 and 200"));
		return false;
	}
	
	// Optional: filter_type (default "All")
	if (!Arguments->TryGetStringField(TEXT("filter_type"), OutFilterType))
	{
		OutFilterType = TEXT("All");
	}
	
	// Optional: filter_name (default empty)
	if (!Arguments->TryGetStringField(TEXT("filter_name"), OutFilterName))
	{
		OutFilterName = TEXT("");
	}
	
	// Optional: sync_browser (default true)
	if (!Arguments->TryGetBoolField(TEXT("sync_browser"), OutSyncBrowser))
	{
		OutSyncBrowser = true;
	}
	
	return true;
}

TSharedPtr<FJsonObject> FN2CMcpReadContentBrowserPathTool::BuildResultJson(
	const TArray<TSharedPtr<FJsonObject>>& Items,
	int32 StartIndex,
	int32 EndIndex,
	int32 TotalCount,
	int32 Page,
	int32 PageSize,
	bool bHasMore) const
{
	TSharedPtr<FJsonObject> ResultJson = MakeShareable(new FJsonObject);
	
	// Build items array
	TArray<TSharedPtr<FJsonValue>> ItemsArray;
	for (int32 i = StartIndex; i < EndIndex && i < Items.Num(); ++i)
	{
		if (Items[i].IsValid())
		{
			ItemsArray.Add(MakeShareable(new FJsonValueObject(Items[i])));
		}
	}
	
	// Set result fields
	ResultJson->SetArrayField(TEXT("items"), ItemsArray);
	ResultJson->SetNumberField(TEXT("total_count"), TotalCount);
	ResultJson->SetNumberField(TEXT("page"), Page);
	ResultJson->SetNumberField(TEXT("page_size"), PageSize);
	ResultJson->SetBoolField(TEXT("has_more"), bHasMore);
	
	return ResultJson;
}