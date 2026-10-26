// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRenameAssetTool.h"
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

REGISTER_MCP_TOOL(FN2CMcpRenameAssetTool)

FMcpToolDefinition FN2CMcpRenameAssetTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("rename-asset"),
        TEXT("Rename an asset or move it to a new location"),
        N2CMcpToolsets::ContentBrowser
    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("sourcePath"), TEXT("string"));
    Properties.Add(TEXT("newName"), TEXT("string"));
    Properties.Add(TEXT("destinationPath"), TEXT("string"));
    Properties.Add(TEXT("showNotification"), TEXT("boolean"));

    TArray<FString> Required;
    Required.Add(TEXT("sourcePath"));
    // Either newName or destinationPath is required, but not both

    Definition.InputSchema = BuildInputSchema(Properties, Required);

    return Definition;
}

FMcpToolCallResult FN2CMcpRenameAssetTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
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
        Arguments->TryGetStringField(TEXT("newName"), NewName);

        FString DestinationPath;
        Arguments->TryGetStringField(TEXT("destinationPath"), DestinationPath);

        // Validate that either newName or destinationPath is provided, but not both
        if (NewName.IsEmpty() && DestinationPath.IsEmpty())
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("Either 'newName' or 'destinationPath' must be provided")
            );
        }

        if (!NewName.IsEmpty() && !DestinationPath.IsEmpty())
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("Cannot provide both 'newName' and 'destinationPath'. Use 'newName' for simple rename or 'destinationPath' for move+rename")
            );
        }

        bool bShowNotification = true;
        Arguments->TryGetBoolField(TEXT("showNotification"), bShowNotification);

        // Validate source asset
        FString NormalizedSourcePath;
        FString ValidationError;
        if (!ValidateSourceAsset(SourcePath, NormalizedSourcePath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Determine the destination
        FString FinalDestinationPath;
        FString NewNameOrPath = !NewName.IsEmpty() ? NewName : DestinationPath;
        if (!ValidateDestination(NormalizedSourcePath, NewNameOrPath, FinalDestinationPath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Get the old asset name for comparison
        FString OldAssetName = FPaths::GetBaseFilename(NormalizedSourcePath);
        FString NewAssetName = FPaths::GetBaseFilename(FinalDestinationPath);

        // Perform the rename operation
        UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
        if (!EditorAssetSubsystem)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to get EditorAssetSubsystem"));
        }

        // Convert paths to object path format for the API
        FString SourceObjectPath = NormalizedSourcePath;
        if (!NormalizedSourcePath.Contains(TEXT(".")))
        {
            SourceObjectPath = NormalizedSourcePath + TEXT(".") + OldAssetName;
        }

        FString DestObjectPath = FinalDestinationPath;
        if (!FinalDestinationPath.Contains(TEXT(".")))
        {
            DestObjectPath = FinalDestinationPath + TEXT(".") + NewAssetName;
        }

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Renaming asset from %s to %s"), *SourceObjectPath, *DestObjectPath),
            EN2CLogSeverity::Info
        );

        // Rename the asset
        if (!EditorAssetSubsystem->RenameAsset(SourceObjectPath, DestObjectPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to rename asset from %s to %s. Make sure the destination is valid and you have write permissions."), 
                    *SourceObjectPath, *DestObjectPath)
            );
        }

        // Show notification if requested
        if (bShowNotification)
        {
            FString NotificationText;
            if (!NewName.IsEmpty())
            {
                // Simple rename
                NotificationText = FString::Printf(TEXT("Renamed '%s' to '%s'"), *OldAssetName, *NewAssetName);
            }
            else
            {
                // Move and rename
                FString DestDirectory = FPaths::GetPath(FinalDestinationPath);
                NotificationText = FString::Printf(TEXT("Moved '%s' to '%s'"), *OldAssetName, *DestDirectory);
            }

            FNotificationInfo Info(FText::FromString(NotificationText));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }

        // Navigate to the renamed asset
        FN2CMcpContentBrowserUtils::SelectAssetAtPath(FinalDestinationPath);

        // Build success response
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Asset renamed successfully"));
        Result->SetStringField(TEXT("oldPath"), NormalizedSourcePath);
        Result->SetStringField(TEXT("newPath"), FinalDestinationPath);
        Result->SetStringField(TEXT("oldName"), OldAssetName);
        Result->SetStringField(TEXT("newName"), NewAssetName);
        Result->SetBoolField(TEXT("assetSelected"), true);

        // Determine if it was a move or just a rename
        FString OldDirectory = FPaths::GetPath(NormalizedSourcePath);
        FString NewDirectory = FPaths::GetPath(FinalDestinationPath);
        bool bWasMoved = (OldDirectory != NewDirectory);
        Result->SetBoolField(TEXT("wasMoved"), bWasMoved);

        // Add tips
        TArray<TSharedPtr<FJsonValue>> Tips;
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("All references to this asset have been automatically updated"))));
        if (bWasMoved)
        {
            Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to explore the new location"))));
        }
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("If this is a Blueprint, use 'open-blueprint' to open it"))));
        Result->SetArrayField(TEXT("tips"), Tips);

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

bool FN2CMcpRenameAssetTool::ValidateSourceAsset(const FString& AssetPath, FString& OutNormalizedPath, FString& OutErrorMessage) const
{
    if (AssetPath.IsEmpty())
    {
        OutErrorMessage = TEXT("Source asset path cannot be empty");
        return false;
    }

    // Normalize the path
    OutNormalizedPath = AssetPath;
    OutNormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

    // Ensure it starts with /Game or /Engine or plugin path
    if (!OutNormalizedPath.StartsWith(TEXT("/Game/")) && 
        !OutNormalizedPath.StartsWith(TEXT("/Engine/")) &&
        !OutNormalizedPath.Contains(TEXT("/Plugins/")))
    {
        OutErrorMessage = TEXT("Asset path must start with /Game/, /Engine/, or a valid plugin path");
        return false;
    }

    // Cannot rename /Engine assets
    if (OutNormalizedPath.StartsWith(TEXT("/Engine/")))
    {
        OutErrorMessage = TEXT("Cannot rename Engine assets. Only /Game/ and plugin assets can be renamed");
        return false;
    }

    // Check if asset exists using AssetRegistry
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Try both package path and object path formats
    FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(OutNormalizedPath));
    if (!AssetData.IsValid())
    {
        // Try with object path format
        FString ObjectPath = OutNormalizedPath;
        if (!OutNormalizedPath.Contains(TEXT(".")))
        {
            FString AssetName = FPaths::GetBaseFilename(OutNormalizedPath);
            ObjectPath = OutNormalizedPath + TEXT(".") + AssetName;
        }
        
        AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
        if (!AssetData.IsValid())
        {
            OutErrorMessage = FString::Printf(TEXT("Source asset not found. Tried paths: %s and %s"), 
                *OutNormalizedPath, *ObjectPath);
            return false;
        }
    }

    return true;
}

bool FN2CMcpRenameAssetTool::ValidateDestination(const FString& SourcePath, const FString& NewNameOrPath, 
    FString& OutDestinationPath, FString& OutErrorMessage) const
{
    if (NewNameOrPath.IsEmpty())
    {
        OutErrorMessage = TEXT("New name or destination path cannot be empty");
        return false;
    }

    // Check if it's a simple name or a full path
    if (NewNameOrPath.Contains(TEXT("/")))
    {
        // It's a full path
        OutDestinationPath = NewNameOrPath;
        OutDestinationPath.ReplaceInline(TEXT("\\"), TEXT("/"));

        // Ensure it starts with /Game or plugin path (no /Engine for destination)
        if (!OutDestinationPath.StartsWith(TEXT("/Game/")) && 
            !OutDestinationPath.Contains(TEXT("/Plugins/")))
        {
            OutErrorMessage = TEXT("Destination path must start with /Game/ or a valid plugin path. Cannot rename to /Engine/");
            return false;
        }

        // Extract directory path
        FString DirectoryPath = FPaths::GetPath(OutDestinationPath);
        if (DirectoryPath.IsEmpty())
        {
            OutErrorMessage = TEXT("Invalid destination path format");
            return false;
        }

        // Ensure destination directory exists
        FString CreationError;
        if (!FN2CMcpContentBrowserUtils::EnsureDirectoryExists(DirectoryPath, CreationError))
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to ensure destination directory exists '%s': %s"), 
                *DirectoryPath, *CreationError);
            return false;
        }
    }
    else
    {
        // It's just a new name, keep the same directory
        FString SourceDirectory = FPaths::GetPath(SourcePath);
        OutDestinationPath = SourceDirectory / NewNameOrPath;
    }

    // Validate the new asset name
    FString NewAssetName = FPaths::GetBaseFilename(OutDestinationPath);
    if (NewAssetName.IsEmpty())
    {
        OutErrorMessage = TEXT("Destination asset name cannot be empty");
        return false;
    }

    // Check for invalid characters in asset name
    if (NewAssetName.Contains(TEXT(" ")) || NewAssetName.Contains(TEXT(".")) || 
        NewAssetName.Contains(TEXT("/")) || NewAssetName.Contains(TEXT("\\")))
    {
        OutErrorMessage = TEXT("Asset name cannot contain spaces, dots, or slashes");
        return false;
    }

    // Check if it would result in the same path (no-op)
    if (FPaths::ConvertRelativePathToFull(SourcePath) == FPaths::ConvertRelativePathToFull(OutDestinationPath))
    {
        OutErrorMessage = TEXT("Source and destination paths are the same");
        return false;
    }

    // Check if destination already exists
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    // Convert to object path to check if asset exists
    FString DestObjectPath = OutDestinationPath;
    if (!OutDestinationPath.Contains(TEXT(".")))
    {
        DestObjectPath = OutDestinationPath + TEXT(".") + NewAssetName;
    }

    FAssetData ExistingAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(DestObjectPath));
    if (ExistingAsset.IsValid())
    {
        OutErrorMessage = FString::Printf(TEXT("An asset already exists at the destination: %s"), 
            *OutDestinationPath);
        return false;
    }

    return true;
}

FString FN2CMcpRenameAssetTool::ConvertToPackagePath(const FString& AssetPath) const
{
    // If it's already a package path (no dot), return as is
    if (!AssetPath.Contains(TEXT(".")))
    {
        return AssetPath;
    }

    // Extract package path from object path
    int32 DotIndex;
    if (AssetPath.FindChar(TEXT('.'), DotIndex))
    {
        return AssetPath.Left(DotIndex);
    }

    return AssetPath;
}