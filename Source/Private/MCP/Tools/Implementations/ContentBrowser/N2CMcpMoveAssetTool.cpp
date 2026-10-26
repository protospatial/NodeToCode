// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpMoveAssetTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"

REGISTER_MCP_TOOL(FN2CMcpMoveAssetTool)

FMcpToolDefinition FN2CMcpMoveAssetTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("move-asset"),
		TEXT("Move or rename an asset to a new location in the content browser. Accepts both package paths (/Game/Folder/Asset) and object paths (/Game/Folder/Asset.Asset)")
	,

		N2CMcpToolsets::ContentBrowser

	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// sourcePath property
	TSharedPtr<FJsonObject> SourcePathProp = MakeShareable(new FJsonObject);
	SourcePathProp->SetStringField(TEXT("type"), TEXT("string"));
	SourcePathProp->SetStringField(TEXT("description"), TEXT("Path to the asset to move. Accepts both formats: '/Game/Folder/Asset' or '/Game/Folder/Asset.Asset'"));
	Properties->SetObjectField(TEXT("sourcePath"), SourcePathProp);

	// destinationPath property
	TSharedPtr<FJsonObject> DestinationPathProp = MakeShareable(new FJsonObject);
	DestinationPathProp->SetStringField(TEXT("type"), TEXT("string"));
	DestinationPathProp->SetStringField(TEXT("description"), TEXT("Destination directory path (e.g., '/Game/Blueprints/Characters')"));
	Properties->SetObjectField(TEXT("destinationPath"), DestinationPathProp);

	// newName property (optional)
	TSharedPtr<FJsonObject> NewNameProp = MakeShareable(new FJsonObject);
	NewNameProp->SetStringField(TEXT("type"), TEXT("string"));
	NewNameProp->SetStringField(TEXT("description"), TEXT("New name for the asset (optional, keeps original name if not provided)"));
	Properties->SetObjectField(TEXT("newName"), NewNameProp);

	// showNotification property
	TSharedPtr<FJsonObject> ShowNotificationProp = MakeShareable(new FJsonObject);
	ShowNotificationProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ShowNotificationProp->SetBoolField(TEXT("default"), true);
	ShowNotificationProp->SetStringField(TEXT("description"), TEXT("Show a notification after the move operation"));
	Properties->SetObjectField(TEXT("showNotification"), ShowNotificationProp);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("sourcePath"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("destinationPath"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpMoveAssetTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FString SourcePath;
		if (!Arguments->TryGetStringField(TEXT("sourcePath"), SourcePath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: sourcePath"));
		}

		FString DestinationPath;
		if (!Arguments->TryGetStringField(TEXT("destinationPath"), DestinationPath))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: destinationPath"));
		}

		FString NewName;
		Arguments->TryGetStringField(TEXT("newName"), NewName);

		bool bShowNotification = true;
		Arguments->TryGetBoolField(TEXT("showNotification"), bShowNotification);

		// Validate source asset
		FString ErrorMessage;
		if (!ValidateSourceAsset(SourcePath, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Convert source path to object path format if needed
		FString SourceObjectPath = SourcePath;
		if (!SourcePath.Contains(TEXT(".")))
		{
			FString AssetName = FPaths::GetBaseFilename(SourcePath);
			SourceObjectPath = SourcePath + TEXT(".") + AssetName;
		}

		// If no new name provided, extract from source path
		if (NewName.IsEmpty())
		{
			NewName = GetAssetNameFromPath(SourcePath);
		}

		// Validate destination and build full path
		FString FullDestinationPath;
		if (!ValidateDestinationPath(DestinationPath, NewName, FullDestinationPath, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Check if this is just a rename (same directory)
		FString SourceDir = FPaths::GetPath(SourcePath);
		if (SourceDir.Contains(TEXT(".")))
		{
			// Remove the object part if present
			SourceDir = FPaths::GetPath(SourceDir.Left(SourceDir.Find(TEXT("."))));
		}
		bool bIsRename = (SourceDir == DestinationPath);

		// Perform the move/rename
		FString NewPath;
		if (!MoveAsset(SourceObjectPath, FullDestinationPath, NewPath, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Show notification if requested
		if (bShowNotification)
		{
			// Extract clean path for notification (without object path suffix)
			FString CleanNewPath = NewPath;
			if (CleanNewPath.Contains(TEXT(".")))
			{
				CleanNewPath = CleanNewPath.Left(CleanNewPath.Find(TEXT(".")));
			}
			
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("NodeToCode", "AssetMoved", "Asset {0} to '{1}'"),
				bIsRename ? NSLOCTEXT("NodeToCode", "Renamed", "renamed") : NSLOCTEXT("NodeToCode", "Moved", "moved"),
				FText::FromString(CleanNewPath)
			));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		// Build success response
		bool bLeftRedirector = false; // We could check this in the future
		TSharedPtr<FJsonObject> Result = BuildSuccessResponse(SourcePath, NewPath, bLeftRedirector);

		// Convert result to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString);
	});
}

bool FN2CMcpMoveAssetTool::ValidateSourceAsset(const FString& AssetPath, FString& OutErrorMessage) const
{
	// Normalize the path
	FString NormalizedPath = FN2CMcpContentBrowserUtils::NormalizeContentPath(AssetPath);

	// Convert to object path if needed (add .AssetName if not present)
	FString ObjectPath = NormalizedPath;
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		// Extract asset name from path
		FString AssetName = FPaths::GetBaseFilename(NormalizedPath);
		ObjectPath = NormalizedPath + TEXT(".") + AssetName;
	}

	// Check if asset exists using asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (!AssetData.IsValid())
	{
		// Try the original path in case it was already an object path
		AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NormalizedPath));
		if (!AssetData.IsValid())
		{
			OutErrorMessage = FString::Printf(TEXT("Asset not found. Tried paths: %s and %s. Expected format: /Game/Folder/AssetName or /Game/Folder/AssetName.AssetName"), 
				*NormalizedPath, *ObjectPath);
			return false;
		}
	}

	// Check if asset is checked out by another user
	if (IsAssetCheckedOutByAnother(AssetData.PackageName.ToString()))
	{
		OutErrorMessage = FString::Printf(TEXT("Asset is checked out by another user: %s"), *AssetData.PackageName.ToString());
		return false;
	}

	return true;
}

bool FN2CMcpMoveAssetTool::ValidateDestinationPath(const FString& DestinationPath, const FString& NewName, 
	FString& OutFullPath, FString& OutErrorMessage) const
{
	// Normalize the destination directory path
	FString NormalizedDestPath = FN2CMcpContentBrowserUtils::NormalizeContentPath(DestinationPath);

	// Build the full destination package path
	FString PackagePath = NormalizedDestPath / NewName;
	
	// Build the full object path (package path + . + asset name)
	OutFullPath = PackagePath + TEXT(".") + NewName;

	// Check if destination already exists
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FAssetData ExistingAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(OutFullPath));
	if (ExistingAsset.IsValid())
	{
		OutErrorMessage = FString::Printf(TEXT("Asset already exists at destination: %s"), *PackagePath);
		return false;
	}

	// Only try to create directory if it doesn't exist
	// The error about "Path already exists" was misleading - it's not about the directory
	FString DirectoryError;
	if (!FN2CMcpContentBrowserUtils::DoesPathExist(NormalizedDestPath))
	{
		if (!FN2CMcpContentBrowserUtils::CreateContentFolder(NormalizedDestPath, DirectoryError))
		{
			OutErrorMessage = FString::Printf(TEXT("Failed to create destination directory: %s"), *DirectoryError);
			return false;
		}
	}

	return true;
}

bool FN2CMcpMoveAssetTool::MoveAsset(const FString& SourcePath, const FString& DestinationPath, 
	FString& OutNewPath, FString& OutErrorMessage) const
{
	// Use EditorAssetSubsystem for the rename operation
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (!EditorAssetSubsystem)
	{
		OutErrorMessage = TEXT("Failed to get EditorAssetSubsystem");
		return false;
	}

	// Perform the rename (which is actually a move in UE terminology)
	bool bSuccess = EditorAssetSubsystem->RenameAsset(SourcePath, DestinationPath);
	
	if (bSuccess)
	{
		OutNewPath = DestinationPath;
		FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully moved asset from %s to %s"), *SourcePath, *DestinationPath), EN2CLogSeverity::Info);
		return true;
	}
	else
	{
		// Try to get more specific error information
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		IAssetTools& AssetTools = AssetToolsModule.Get();

		// Load the asset to get more info
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(SourcePath));

		if (AssetData.IsValid())
		{
			// Try alternative approach with IAssetTools
			TArray<FAssetRenameData> RenameData;
			FAssetRenameData& Rename = RenameData.AddDefaulted_GetRef();
			
			UObject* Asset = AssetData.GetAsset();
			if (Asset)
			{
				Rename.Asset = Asset;
				Rename.NewPackagePath = FPaths::GetPath(DestinationPath);
				Rename.NewName = FPaths::GetBaseFilename(DestinationPath);
				
				if (AssetTools.RenameAssets(RenameData))
				{
					OutNewPath = DestinationPath;
					return true;
				}
			}
		}

		OutErrorMessage = FString::Printf(TEXT("Failed to move asset from %s to %s. The asset may be in use or locked."), 
			*SourcePath, *DestinationPath);
		return false;
	}
}

TSharedPtr<FJsonObject> FN2CMcpMoveAssetTool::BuildSuccessResponse(const FString& OldPath, const FString& NewPath, 
	bool bLeftRedirector) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Clean up paths for display (remove object path suffix if present)
	FString CleanOldPath = OldPath;
	if (CleanOldPath.Contains(TEXT(".")))
	{
		CleanOldPath = CleanOldPath.Left(CleanOldPath.Find(TEXT(".")));
	}
	
	FString CleanNewPath = NewPath;
	if (CleanNewPath.Contains(TEXT(".")))
	{
		CleanNewPath = CleanNewPath.Left(CleanNewPath.Find(TEXT(".")));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("oldPath"), CleanOldPath);
	Result->SetStringField(TEXT("newPath"), CleanNewPath);
	Result->SetStringField(TEXT("objectPath"), NewPath); // Include full object path for advanced users
	
	// Determine operation type
	FString OldDir = FPaths::GetPath(CleanOldPath);
	FString NewDir = FPaths::GetPath(CleanNewPath);
	Result->SetStringField(TEXT("operation"), OldDir == NewDir ? TEXT("rename") : TEXT("move"));
	Result->SetBoolField(TEXT("leftRedirector"), bLeftRedirector);

	// Add asset info
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NewPath));

	if (AssetData.IsValid())
	{
		TSharedPtr<FJsonObject> AssetInfo = MakeShareable(new FJsonObject);
		AssetInfo->SetStringField(TEXT("assetName"), AssetData.AssetName.ToString());
		AssetInfo->SetStringField(TEXT("assetClass"), AssetData.AssetClassPath.ToString());
		AssetInfo->SetStringField(TEXT("packageName"), AssetData.PackageName.ToString());
		Result->SetObjectField(TEXT("assetInfo"), AssetInfo);
	}

	// Add helpful next steps
	TArray<TSharedPtr<FJsonValue>> NextSteps;
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Update any references to the old path in your code"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to verify the asset at the new location"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Consider moving related assets to maintain organization"))));
	Result->SetArrayField(TEXT("nextSteps"), NextSteps);

	return Result;
}

FString FN2CMcpMoveAssetTool::GetAssetNameFromPath(const FString& AssetPath) const
{
	return FPaths::GetCleanFilename(AssetPath);
}

bool FN2CMcpMoveAssetTool::IsAssetCheckedOutByAnother(const FString& AssetPath) const
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	if (SourceControlModule.IsEnabled() && SourceControlModule.GetProvider().IsAvailable())
	{
		FSourceControlStatePtr SourceControlState = SourceControlModule.GetProvider().GetState(AssetPath, EStateCacheUsage::Use);
		if (SourceControlState.IsValid())
		{
			return SourceControlState->IsCheckedOutOther();
		}
	}
	return false;
}