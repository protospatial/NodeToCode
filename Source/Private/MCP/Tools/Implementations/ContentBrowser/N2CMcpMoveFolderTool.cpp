// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpMoveFolderTool.h"
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

REGISTER_MCP_TOOL(FN2CMcpMoveFolderTool)

FMcpToolDefinition FN2CMcpMoveFolderTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("move-folder"),
        TEXT("Move a folder and all its contents to a new location in the content browser")
    ,

    	N2CMcpToolsets::ContentBrowser

    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("sourcePath"), TEXT("string"));
    Properties.Add(TEXT("destinationPath"), TEXT("string"));
    Properties.Add(TEXT("showNotification"), TEXT("boolean"));

    TArray<FString> Required;
    Required.Add(TEXT("sourcePath"));
    Required.Add(TEXT("destinationPath"));

    Definition.InputSchema = BuildInputSchema(Properties, Required);

    return Definition;
}

FMcpToolCallResult FN2CMcpMoveFolderTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Extract parameters
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

        // Extract folder name from source path
        FString FolderName = FPaths::GetCleanFilename(NormalizedSourcePath);

        // Validate destination path
        FString FullDestinationPath;
        if (!ValidateDestinationPath(DestinationPath, FolderName, FullDestinationPath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Count assets before moving
        int32 AssetCount = CountAssetsInFolder(NormalizedSourcePath);

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Moving folder '%s' with %d assets to '%s'"), 
                *NormalizedSourcePath, AssetCount, *FullDestinationPath),
            EN2CLogSeverity::Info
        );

        // Move the folder and its contents
        FString MoveError;
        if (!MoveFolderContents(NormalizedSourcePath, FullDestinationPath, MoveError))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to move folder: %s"), *MoveError)
            );
        }

        // Show notification if requested
        if (bShowNotification)
        {
            FNotificationInfo Info(FText::Format(
                NSLOCTEXT("NodeToCode", "FolderMoved", "Moved folder '{0}' to '{1}' ({2} assets)"),
                FText::FromString(FolderName),
                FText::FromString(DestinationPath),
                FText::AsNumber(AssetCount)
            ));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }

        // Navigate to the new location
        FN2CMcpContentBrowserUtils::NavigateToPath(FullDestinationPath);

        // Build success response
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Folder moved successfully"));
        Result->SetStringField(TEXT("sourcePath"), NormalizedSourcePath);
        Result->SetStringField(TEXT("destinationPath"), FullDestinationPath);
        Result->SetNumberField(TEXT("assetsMoved"), AssetCount);
        Result->SetBoolField(TEXT("navigated"), true);

        // Add tips
        TArray<TSharedPtr<FJsonValue>> Tips;
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to explore the moved folder"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("All asset references are automatically updated"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("The original folder location has been removed"))));
        Result->SetArrayField(TEXT("tips"), Tips);

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

bool FN2CMcpMoveFolderTool::ValidateFolderPath(const FString& Path, FString& OutNormalizedPath, FString& OutErrorMessage) const
{
    if (Path.IsEmpty())
    {
        OutErrorMessage = TEXT("Folder path cannot be empty");
        return false;
    }

    // Normalize the path
    OutNormalizedPath = Path;
    OutNormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

    // Remove trailing slash if present
    if (OutNormalizedPath.EndsWith(TEXT("/")))
    {
        OutNormalizedPath = OutNormalizedPath.LeftChop(1);
    }

    // Only where MCP tools may write: /Game and the project's plugins (card 0182)
    if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(OutNormalizedPath, OutErrorMessage))
    {
        return false;
    }

    // Don't allow moving root folders (/Game, a plugin's /<PluginName>)
    if (FN2CMcpContentBrowserUtils::IsContentMountRoot(OutNormalizedPath))
    {
        OutErrorMessage = FString::Printf(TEXT("Cannot move the content root %s"), *OutNormalizedPath);
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

bool FN2CMcpMoveFolderTool::ValidateDestinationPath(const FString& DestinationPath, const FString& FolderName, 
    FString& OutFullPath, FString& OutErrorMessage) const
{
    if (DestinationPath.IsEmpty())
    {
        OutErrorMessage = TEXT("Destination path cannot be empty");
        return false;
    }

    // Normalize the destination path
    FString NormalizedDestPath = DestinationPath;
    NormalizedDestPath.ReplaceInline(TEXT("\\"), TEXT("/"));

    // Remove trailing slash if present
    if (NormalizedDestPath.EndsWith(TEXT("/")))
    {
        NormalizedDestPath = NormalizedDestPath.LeftChop(1);
    }

    // Only where MCP tools may write: /Game and the project's plugins (card 0182)
    if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(NormalizedDestPath, OutErrorMessage))
    {
        return false;
    }

    // Build the full destination path
    OutFullPath = NormalizedDestPath / FolderName;

    // Check if destination already exists
    if (FN2CMcpContentBrowserUtils::DoesPathExist(OutFullPath))
    {
        OutErrorMessage = FString::Printf(TEXT("Destination folder already exists: %s"), *OutFullPath);
        return false;
    }

    // Ensure destination parent directory exists
    FString CreationError;
    if (!FN2CMcpContentBrowserUtils::EnsureDirectoryExists(NormalizedDestPath, CreationError))
    {
        OutErrorMessage = FString::Printf(TEXT("Failed to ensure destination directory exists '%s': %s"), 
            *NormalizedDestPath, *CreationError);
        return false;
    }

    return true;
}

bool FN2CMcpMoveFolderTool::MoveFolderContents(const FString& SourcePath, const FString& DestinationPath, 
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

        // Move the asset
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
        FString::Printf(TEXT("Successfully moved %d assets from %s to %s"), 
            SuccessCount, *SourcePath, *DestinationPath),
        EN2CLogSeverity::Info
    );

    return true;
}

int32 FN2CMcpMoveFolderTool::CountAssetsInFolder(const FString& FolderPath) const
{
    TArray<FAssetData> Assets;
    CollectAllAssetsInFolder(FolderPath, Assets);
    return Assets.Num();
}

void FN2CMcpMoveFolderTool::CollectAllAssetsInFolder(const FString& FolderPath, TArray<FAssetData>& OutAssets) const
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