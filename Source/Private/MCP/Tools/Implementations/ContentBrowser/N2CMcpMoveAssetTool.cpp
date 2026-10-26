// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpMoveAssetTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
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
	NewNameProp->SetStringField(TEXT("description"), TEXT("New name for the asset, a name only: no '.', '/', '\\' or spaces (optional, keeps the asset's name if not provided)"));
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

		// Validate source asset: every name and path after this comes from the registry's asset, never from the raw
		// sourcePath (card 0284: an object path used to give the name "A.A" and the destination /Game/Dest/A.A.A.A)
		FString ErrorMessage;
		FAssetData SourceAsset;
		if (!ValidateSourceAsset(SourcePath, SourceAsset, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// The asset's name, or newName checked as given before any path is built with it
		const FString AssetName = NewName.IsEmpty() ? SourceAsset.AssetName.ToString() : NewName;
		if (!FN2CMcpContentBrowserUtils::ValidateNewAssetName(AssetName, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Validate destination and build the new package path; nothing is created yet
		FString DestinationFolder;
		FString DestinationPackage;
		if (!ValidateDestinationPath(DestinationPath, AssetName, SourceAsset, DestinationFolder, DestinationPackage, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}

		// Perform the move/rename
		const FString OldPackage = SourceAsset.PackageName.ToString();
		FAssetData MovedAsset;
		if (!MoveAsset(SourceAsset, DestinationFolder, DestinationPackage + TEXT(".") + AssetName, MovedAsset, ErrorMessage))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
		}
		const FString NewPackage = MovedAsset.PackageName.ToString();
		const bool bIsRename = FPackageName::GetLongPackagePath(OldPackage).Equals(FPackageName::GetLongPackagePath(NewPackage), ESearchCase::IgnoreCase);

		// Show notification if requested
		if (bShowNotification)
		{
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("NodeToCode", "AssetMoved", "Asset {0} to '{1}'"),
				bIsRename ? NSLOCTEXT("NodeToCode", "Renamed", "renamed") : NSLOCTEXT("NodeToCode", "Moved", "moved"),
				FText::FromString(NewPackage)
			));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}

		// Build success response from what the registry holds now
		// A redirector at the old object path, as the registry holds it now (card 0281, review fs-24)
		const bool bLeftRedirector = FN2CMcpContentBrowserUtils::IsRedirectorAt(SourceAsset.GetObjectPathString());
		TSharedPtr<FJsonObject> Result = BuildSuccessResponse(OldPackage, MovedAsset, bIsRename, bLeftRedirector);

		// Convert result to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString);
	});
}

bool FN2CMcpMoveAssetTool::ValidateSourceAsset(const FString& AssetPath, FAssetData& OutAsset, FString& OutErrorMessage) const
{
	// A move rewrites the source package too: only where MCP tools may write, /Game and the project's plugins
	// (card 0182)
	if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(FN2CMcpContentBrowserUtils::NormalizeContentPath(AssetPath), OutErrorMessage))
	{
		return false;
	}

	// The asset, by package path or object path
	if (!FN2CMcpContentBrowserUtils::ResolveAsset(AssetPath, OutAsset, OutErrorMessage))
	{
		return false;
	}

	// Check if asset is checked out by another user (by its file name: card 0284)
	if (FN2CMcpContentBrowserUtils::IsCheckedOutByAnother(OutAsset.PackageName.ToString()))
	{
		OutErrorMessage = FString::Printf(TEXT("Asset is checked out by another user: %s"), *OutAsset.PackageName.ToString());
		return false;
	}

	return true;
}

bool FN2CMcpMoveAssetTool::ValidateDestinationPath(const FString& DestinationPath, const FString& AssetName, const FAssetData& SourceAsset,
	FString& OutFolder, FString& OutPackage, FString& OutErrorMessage) const
{
	// Normalize the destination directory path
	OutFolder = FN2CMcpContentBrowserUtils::NormalizeContentPath(DestinationPath);

	// Only where MCP tools may write: /Game and the project's plugins (card 0182); and a valid folder path
	if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(OutFolder, OutErrorMessage)
		|| !FN2CMcpContentBrowserUtils::ValidateContentPath(OutFolder, OutErrorMessage))
	{
		return false;
	}

	// The full destination package path
	OutPackage = OutFolder / AssetName;
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(OutPackage, /*bIncludeReadOnlyRoots*/ false, &Reason))
	{
		OutErrorMessage = FString::Printf(TEXT("Invalid destination '%s': %s"), *OutPackage, *Reason.ToString());
		return false;
	}

	// The same asset, or the same path in another case: package names are case-insensitive, so a case-only rename
	// would meet the asset itself
	const FString SourcePackage = SourceAsset.PackageName.ToString();
	if (OutPackage.Equals(SourcePackage, ESearchCase::CaseSensitive))
	{
		OutErrorMessage = FString::Printf(TEXT("The asset is already at %s"), *SourcePackage);
		return false;
	}
	if (OutPackage.Equals(SourcePackage, ESearchCase::IgnoreCase))
	{
		OutErrorMessage = FString::Printf(TEXT("Case-only renames are not supported (%s to %s): package names are case-insensitive. ")
			TEXT("Rename to another name first, then to the new spelling."), *SourcePackage, *OutPackage);
		return false;
	}

	// Check if destination already exists
	if (FN2CMcpContentBrowserUtils::DoesAssetPackageExist(OutPackage))
	{
		OutErrorMessage = FString::Printf(TEXT("Asset already exists at destination: %s"), *OutPackage);
		return false;
	}

	return true;
}

bool FN2CMcpMoveAssetTool::MoveAsset(const FAssetData& SourceAsset, const FString& DestinationFolder, const FString& DestinationObjectPath,
	FAssetData& OutMovedAsset, FString& OutErrorMessage) const
{
	// Use EditorAssetSubsystem for the rename operation: it validates the destination and keeps modal dialogs away
	// (GIsRunningUnattendedScript). There is no IAssetTools fallback (card 0284): it retried without either.
	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (!EditorAssetSubsystem)
	{
		OutErrorMessage = TEXT("Failed to get EditorAssetSubsystem");
		return false;
	}

	// The destination folder, only now, and removed again if the move fails
	FString CreatedFolder;
	FString FolderError;
	if (!FN2CMcpContentBrowserUtils::CreateFolderForMutation(DestinationFolder, CreatedFolder, FolderError))
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to create destination directory: %s"), *FolderError);
		return false;
	}

	// Perform the rename (which is actually a move in UE terminology)
	const FString SourceObjectPath = SourceAsset.GetObjectPathString();
	bool bSuccess = false;
	FString EngineReason;
	{
		FN2CMcpEngineErrorCapture Capture;
		bSuccess = EditorAssetSubsystem->RenameAsset(SourceObjectPath, DestinationObjectPath);
		EngineReason = Capture.GetReason();
	}

	// What the registry holds at the destination now
	FString ReadBackError;
	if (bSuccess && !FN2CMcpContentBrowserUtils::ResolveAsset(DestinationObjectPath, OutMovedAsset, ReadBackError))
	{
		bSuccess = false;
		EngineReason = TEXT("the engine reported success, but no asset is at the destination");
	}

	if (!bSuccess)
	{
		FN2CMcpContentBrowserUtils::RemoveFolderCreatedForMutation(CreatedFolder);
		OutErrorMessage = FString::Printf(TEXT("Failed to move asset from %s to %s: %s"), *SourceObjectPath, *DestinationObjectPath,
			EngineReason.IsEmpty() ? TEXT("the engine gave no reason (the asset may be in use or locked)") : *EngineReason);
		return false;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully moved asset from %s to %s"), *SourceObjectPath, *OutMovedAsset.GetObjectPathString()), EN2CLogSeverity::Info);
	return true;
}

TSharedPtr<FJsonObject> FN2CMcpMoveAssetTool::BuildSuccessResponse(const FString& OldPackage, const FAssetData& MovedAsset, bool bIsRename,
	bool bLeftRedirector) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("oldPath"), OldPackage);
	Result->SetStringField(TEXT("newPath"), MovedAsset.PackageName.ToString());
	Result->SetStringField(TEXT("objectPath"), MovedAsset.GetObjectPathString()); // Include full object path for advanced users

	// Determine operation type
	Result->SetStringField(TEXT("operation"), bIsRename ? TEXT("rename") : TEXT("move"));
	Result->SetBoolField(TEXT("leftRedirector"), bLeftRedirector);

	// Add asset info
	TSharedPtr<FJsonObject> AssetInfo = MakeShareable(new FJsonObject);
	AssetInfo->SetStringField(TEXT("assetName"), MovedAsset.AssetName.ToString());
	AssetInfo->SetStringField(TEXT("assetClass"), MovedAsset.AssetClassPath.ToString());
	AssetInfo->SetStringField(TEXT("packageName"), MovedAsset.PackageName.ToString());
	Result->SetObjectField(TEXT("assetInfo"), AssetInfo);

	// Add helpful next steps
	TArray<TSharedPtr<FJsonValue>> NextSteps;
	if (bLeftRedirector)
	{
		NextSteps.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("A redirector was left at %s: a referencing package could not be ")
			TEXT("updated or saved. Old references still resolve through it; to remove it, right-click its folder in the content browser and ")
			TEXT("choose Update Redirector References"), *OldPackage))));
	}
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Paths written in C++, config or text files are not updated: change any that name the old path"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to verify the asset at the new location"))));
	NextSteps.Add(MakeShareable(new FJsonValueString(TEXT("Consider moving related assets to maintain organization"))));
	Result->SetArrayField(TEXT("nextSteps"), NextSteps);

	return Result;
}
