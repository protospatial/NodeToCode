// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpOpenContentBrowserPathTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpOpenContentBrowserPathTool)

FMcpToolDefinition FN2CMcpOpenContentBrowserPathTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("open-content-browser-path"),
		TEXT("Opens a specified path in the focused content browser, allowing navigation of the project structure")
	,
		N2CMcpToolsets::ContentBrowser
	);
	
	// Define input schema
	TSharedPtr<FJsonObject> InputSchema = MakeShareable(new FJsonObject);
	InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	
	// Define properties
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// path property
	TSharedPtr<FJsonObject> PathProp = MakeShareable(new FJsonObject);
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Content browser path to navigate to (e.g., '/Game/Blueprints')"));
	Properties->SetObjectField(TEXT("path"), PathProp);
	
	// select_item property
	TSharedPtr<FJsonObject> SelectItemProp = MakeShareable(new FJsonObject);
	SelectItemProp->SetStringField(TEXT("type"), TEXT("string"));
	SelectItemProp->SetStringField(TEXT("description"), TEXT("Optional: Specific item to select after navigation"));
	SelectItemProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("select_item"), SelectItemProp);
	
	// create_if_missing property
	TSharedPtr<FJsonObject> CreateIfMissingProp = MakeShareable(new FJsonObject);
	CreateIfMissingProp->SetStringField(TEXT("type"), TEXT("boolean"));
	CreateIfMissingProp->SetStringField(TEXT("description"), TEXT("Whether to create the folder if it doesn't exist; only under /Game or a project plugin's content"));
	CreateIfMissingProp->SetBoolField(TEXT("default"), false);
	Properties->SetObjectField(TEXT("create_if_missing"), CreateIfMissingProp);
	
	InputSchema->SetObjectField(TEXT("properties"), Properties);
	
	// Define required fields
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShareable(new FJsonValueString(TEXT("path"))));
	InputSchema->SetArrayField(TEXT("required"), Required);
	
	Definition.InputSchema = InputSchema;
	
	// Not read-only, but it navigates the Content Browser, changing no asset: it keeps working during Play In Editor (card 0170)
	Definition.bAllowedDuringPlaySession = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpOpenContentBrowserPathTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Execute on Game Thread since we need to interact with the content browser
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FString Path;
		if (!Arguments->TryGetStringField(TEXT("path"), Path))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: path"));
		}
		
		FString SelectItem;
		Arguments->TryGetStringField(TEXT("select_item"), SelectItem);
		
		bool bCreateIfMissing = false;
		Arguments->TryGetBoolField(TEXT("create_if_missing"), bCreateIfMissing);
		
		// Normalize the path
		Path = FN2CMcpContentBrowserUtils::NormalizeContentPath(Path);
		
		// Validate the path
		FString ValidationError;
		if (!FN2CMcpContentBrowserUtils::ValidateContentPath(Path, ValidationError))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("open-content-browser-path: Invalid path - %s"), *ValidationError));
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Invalid path: %s"), *ValidationError));
		}
		
		// Check if path exists
		bool bCreatedFolder = false;
		if (!FN2CMcpContentBrowserUtils::DoesPathExist(Path))
		{
			if (bCreateIfMissing)
			{
				// Creating the folder is a write: only where MCP tools may write, /Game and the project's plugins (card 0182)
				FString CreateError;
				if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(Path, CreateError))
				{
					return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Path does not exist and was not created: %s"), *CreateError));
				}
				if (!FN2CMcpContentBrowserUtils::CreateContentFolder(Path, CreateError))
				{
					FN2CLogger::Get().LogError(FString::Printf(TEXT("open-content-browser-path: Failed to create folder - %s"), *CreateError));
					return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Failed to create folder: %s"), *CreateError));
				}
				bCreatedFolder = true;
				FN2CLogger::Get().Log(FString::Printf(TEXT("open-content-browser-path: Created folder at %s"), *Path), EN2CLogSeverity::Info);
			}
			else
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("open-content-browser-path: Path does not exist - %s"), *Path));
				return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Path does not exist: %s"), *Path));
			}
		}
		
		// Navigate to the path
		bool bNavigationSuccess = FN2CMcpContentBrowserUtils::NavigateToPath(Path);
		
		// Select specific item if requested
		FString SelectedItem;
		if (bNavigationSuccess && !SelectItem.IsEmpty())
		{
			// Construct full asset path
			FString AssetPath = Path / SelectItem;
			
			if (FN2CMcpContentBrowserUtils::SelectAssetAtPath(AssetPath))
			{
				SelectedItem = SelectItem;
				FN2CLogger::Get().Log(FString::Printf(TEXT("open-content-browser-path: Selected asset %s"), *SelectItem), EN2CLogSeverity::Info);
			}
			else
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("open-content-browser-path: Could not select asset %s"), *SelectItem));
				// Don't fail the operation, just log the warning
			}
		}
		
		// Create result JSON
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), bNavigationSuccess);
		ResultObject->SetStringField(TEXT("navigated_path"), Path);
		ResultObject->SetStringField(TEXT("selected_item"), SelectedItem);
		ResultObject->SetBoolField(TEXT("created_folder"), bCreatedFolder);
		
		// Also include the current selected paths for context
		TArray<FString> CurrentSelectedPaths;
		if (FN2CMcpContentBrowserUtils::GetSelectedPaths(CurrentSelectedPaths))
		{
			TArray<TSharedPtr<FJsonValue>> SelectedPathsArray;
			for (const FString& SelectedPath : CurrentSelectedPaths)
			{
				SelectedPathsArray.Add(MakeShareable(new FJsonValueString(SelectedPath)));
			}
			ResultObject->SetArrayField(TEXT("current_selected_paths"), SelectedPathsArray);
		}
		
		// Serialize result
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		if (!FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize result"));
		}
		
		FString SuccessMessage = bCreatedFolder ? 
			FString::Printf(TEXT("Created folder and navigated to %s"), *Path) :
			FString::Printf(TEXT("Navigated to %s"), *Path);
			
		FN2CLogger::Get().Log(FString::Printf(TEXT("open-content-browser-path tool: %s"), *SuccessMessage), EN2CLogSeverity::Info);
		
		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}