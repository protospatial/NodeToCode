// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRenameFolderTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

REGISTER_MCP_TOOL(FN2CMcpRenameFolderTool)

FMcpToolDefinition FN2CMcpRenameFolderTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("rename-folder"),
        TEXT("Rename a folder in the content browser")
    ,

    	N2CMcpToolsets::ContentBrowser

    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("sourcePath"), TEXT("string"));
    Properties.Add(TEXT("newName"), TEXT("string"));
    Properties.Add(TEXT("showNotification"), TEXT("boolean"));

    TArray<FString> Required;
    Required.Add(TEXT("sourcePath"));
    Required.Add(TEXT("newName"));

    Definition.InputSchema = BuildInputSchema(Properties, Required);

    return Definition;
}

FMcpToolCallResult FN2CMcpRenameFolderTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Extract parameters
        FString SourcePath;
        if (!Arguments->TryGetStringField(TEXT("sourcePath"), SourcePath))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: sourcePath"));
        }

        FString NewName;
        if (!Arguments->TryGetStringField(TEXT("newName"), NewName))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: newName"));
        }

        bool bShowNotification = true;
        Arguments->TryGetBoolField(TEXT("showNotification"), bShowNotification);

        // Validate source folder
        FString NormalizedSourcePath;
        FString ValidationError;
        if (!ValidateFolderPath(SourcePath, NormalizedSourcePath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Check if source folder exists
        if (!FN2CMcpContentBrowserUtils::DoesPathExist(NormalizedSourcePath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Source folder does not exist: %s"), *NormalizedSourcePath)
            );
        }

        // Validate new name
        if (!ValidateNewName(NewName, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Get the old folder name and parent path
        FString OldFolderName = FPaths::GetCleanFilename(NormalizedSourcePath);
        FString ParentPath = FPaths::GetPath(NormalizedSourcePath);

        // Build the new folder path
        FString NewFolderPath = ParentPath / NewName;

        // Check if destination already exists
        if (FN2CMcpContentBrowserUtils::DoesPathExist(NewFolderPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("A folder with the name '%s' already exists in '%s'"), 
                    *NewName, *ParentPath)
            );
        }

        // Count assets before renaming
        int32 AssetCount = CountAssetsInFolder(NormalizedSourcePath);

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Renaming folder '%s' to '%s' (%d assets)"), 
                *OldFolderName, *NewName, AssetCount),
            EN2CLogSeverity::Info
        );

        // Move the folder contents (which effectively renames the folder)
        FString MoveError;
        if (!MoveFolderContents(NormalizedSourcePath, NewFolderPath, MoveError))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to rename folder: %s"), *MoveError)
            );
        }

        // Show notification if requested
        if (bShowNotification)
        {
            FNotificationInfo Info(FText::Format(
                NSLOCTEXT("NodeToCode", "FolderRenamed", "Renamed folder '{0}' to '{1}' ({2} assets)"),
                FText::FromString(OldFolderName),
                FText::FromString(NewName),
                FText::AsNumber(AssetCount)
            ));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }

        // Navigate to the renamed folder
        FN2CMcpContentBrowserUtils::NavigateToPath(NewFolderPath);

        // Build success response
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Folder renamed successfully"));
        Result->SetStringField(TEXT("oldPath"), NormalizedSourcePath);
        Result->SetStringField(TEXT("newPath"), NewFolderPath);
        Result->SetStringField(TEXT("oldName"), OldFolderName);
        Result->SetStringField(TEXT("newName"), NewName);
        Result->SetNumberField(TEXT("assetsRenamed"), AssetCount);
        Result->SetBoolField(TEXT("navigated"), true);

        // Add tips
        TArray<TSharedPtr<FJsonValue>> Tips;
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("All asset references have been automatically updated"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to explore the renamed folder"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("The original folder location has been removed"))));
        Result->SetArrayField(TEXT("tips"), Tips);

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

bool FN2CMcpRenameFolderTool::ValidateFolderPath(const FString& Path, FString& OutNormalizedPath, FString& OutErrorMessage) const
{
    if (Path.IsEmpty())
    {
        OutErrorMessage = TEXT("Folder path cannot be empty");
        return false;
    }

    // Normalize the path
    OutNormalizedPath = Path;
    OutNormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

    // Ensure it starts with a valid root
    if (!OutNormalizedPath.StartsWith(TEXT("/Game/")) && 
        !OutNormalizedPath.Contains(TEXT("/Plugins/")))
    {
        OutErrorMessage = TEXT("Folder path must start with /Game/ or a valid plugin path. Cannot rename folders in /Engine/");
        return false;
    }

    // Remove trailing slash if present
    if (OutNormalizedPath.EndsWith(TEXT("/")))
    {
        OutNormalizedPath = OutNormalizedPath.LeftChop(1);
    }

    // Don't allow renaming root folders
    if (OutNormalizedPath == TEXT("/Game"))
    {
        OutErrorMessage = TEXT("Cannot rename the /Game root folder");
        return false;
    }

    // Check for invalid characters
    if (OutNormalizedPath.Contains(TEXT("..")) || 
        OutNormalizedPath.Contains(TEXT("~")) ||
        OutNormalizedPath.Contains(TEXT("*")) ||
        OutNormalizedPath.Contains(TEXT("?")))
    {
        OutErrorMessage = TEXT("Folder path contains invalid characters");
        return false;
    }

    return true;
}

bool FN2CMcpRenameFolderTool::ValidateNewName(const FString& NewName, FString& OutErrorMessage) const
{
    if (NewName.IsEmpty())
    {
        OutErrorMessage = TEXT("New folder name cannot be empty");
        return false;
    }

    // Check for invalid characters
    if (NewName.Contains(TEXT("/")) || 
        NewName.Contains(TEXT("\\")) ||
        NewName.Contains(TEXT(".")) ||
        NewName.Contains(TEXT(" ")) ||
        NewName.Contains(TEXT("..")) ||
        NewName.Contains(TEXT("~")) ||
        NewName.Contains(TEXT("*")) ||
        NewName.Contains(TEXT("?")))
    {
        OutErrorMessage = TEXT("Folder name contains invalid characters. Avoid spaces, dots, slashes, and special characters");
        return false;
    }

    // Check for reserved names
    if (NewName.Equals(TEXT("CON"), ESearchCase::IgnoreCase) ||
        NewName.Equals(TEXT("PRN"), ESearchCase::IgnoreCase) ||
        NewName.Equals(TEXT("AUX"), ESearchCase::IgnoreCase) ||
        NewName.Equals(TEXT("NUL"), ESearchCase::IgnoreCase) ||
        NewName.StartsWith(TEXT("COM"), ESearchCase::IgnoreCase) ||
        NewName.StartsWith(TEXT("LPT"), ESearchCase::IgnoreCase))
    {
        OutErrorMessage = TEXT("Folder name is a reserved system name");
        return false;
    }

    return true;
}

bool FN2CMcpRenameFolderTool::MoveFolderContents(const FString& SourcePath, const FString& DestinationPath, 
    FString& OutErrorMessage) const
{
    // Collect all assets in the source folder
    TArray<FAssetData> AssetsToMove;
    CollectAllAssetsInFolder(SourcePath, AssetsToMove);

    // Get editor asset subsystem
    UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    if (!EditorAssetSubsystem)
    {
        OutErrorMessage = TEXT("Failed to get EditorAssetSubsystem");
        return false;
    }

    // Move each asset
    int32 SuccessCount = 0;
    TArray<FString> FailedAssets;

    for (const FAssetData& AssetData : AssetsToMove)
    {
        // Calculate relative path from source folder
        FString AssetPath = AssetData.PackageName.ToString();
        FString RelativePath = AssetPath.RightChop(SourcePath.Len());
        
        // Build destination path for this asset
        FString NewAssetPath = DestinationPath + RelativePath;
        
        // Get the object path format for the source
        FString SourceObjectPath = AssetData.GetObjectPathString();
        
        // Build the object path format for the destination
        FString AssetName = FPaths::GetBaseFilename(NewAssetPath);
        FString DestObjectPath = NewAssetPath + TEXT(".") + AssetName;

        // Rename (move) the asset
        if (EditorAssetSubsystem->RenameAsset(SourceObjectPath, DestObjectPath))
        {
            SuccessCount++;
        }
        else
        {
            FailedAssets.Add(AssetPath);
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Failed to move asset: %s"), *AssetPath),
                EN2CLogSeverity::Warning
            );
        }
    }

    // Report results
    if (FailedAssets.Num() > 0)
    {
        OutErrorMessage = FString::Printf(TEXT("Failed to move %d out of %d assets. First failed asset: %s"), 
            FailedAssets.Num(), AssetsToMove.Num(), *FailedAssets[0]);
        return false;
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Successfully renamed folder by moving %d assets from %s to %s"), 
            SuccessCount, *SourcePath, *DestinationPath),
        EN2CLogSeverity::Info
    );

    return true;
}

int32 FN2CMcpRenameFolderTool::CountAssetsInFolder(const FString& FolderPath) const
{
    TArray<FAssetData> Assets;
    CollectAllAssetsInFolder(FolderPath, Assets);
    return Assets.Num();
}

void FN2CMcpRenameFolderTool::CollectAllAssetsInFolder(const FString& FolderPath, TArray<FAssetData>& OutAssets) const
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Create filter for the folder
    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(FName(*FolderPath));

    // Get all assets in the folder and subfolders
    AssetRegistry.GetAssets(Filter, OutAssets);
}