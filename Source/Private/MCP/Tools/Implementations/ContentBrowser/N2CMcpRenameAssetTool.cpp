// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRenameAssetTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
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

        // Validate source asset: names and paths after this come from the registry's asset (card 0284)
        FAssetData SourceAsset;
        FString ValidationError;
        if (!ValidateSourceAsset(SourcePath, SourceAsset, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Determine the destination; nothing is created yet
        FString DestinationFolder;
        FString NewAssetName;
        if (!ValidateDestination(SourceAsset, NewName, DestinationPath, DestinationFolder, NewAssetName, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Perform the rename operation
        UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
        if (!EditorAssetSubsystem)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to get EditorAssetSubsystem"));
        }

        const FString OldAssetName = SourceAsset.AssetName.ToString();
        const FString OldPackage = SourceAsset.PackageName.ToString();
        const FString SourceObjectPath = SourceAsset.GetObjectPathString();
        const FString DestObjectPath = DestinationFolder / NewAssetName + TEXT(".") + NewAssetName;

        // The destination folder, only now, and removed again if the rename fails
        FString CreatedFolder;
        FString FolderError;
        if (!FN2CMcpContentBrowserUtils::CreateFolderForMutation(DestinationFolder, CreatedFolder, FolderError))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to create destination directory '%s': %s"), *DestinationFolder, *FolderError));
        }

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Renaming asset from %s to %s"), *SourceObjectPath, *DestObjectPath),
            EN2CLogSeverity::Info
        );

        // Rename the asset, then read it back from the registry
        bool bRenamed = false;
        FString EngineReason;
        {
            FN2CMcpEngineErrorCapture Capture;
            bRenamed = EditorAssetSubsystem->RenameAsset(SourceObjectPath, DestObjectPath);
            EngineReason = Capture.GetReason();
        }
        FAssetData RenamedAsset;
        FString ReadBackError;
        if (bRenamed && !FN2CMcpContentBrowserUtils::ResolveAsset(DestObjectPath, RenamedAsset, ReadBackError))
        {
            bRenamed = false;
            EngineReason = TEXT("the engine reported success, but no asset is at the destination");
        }
        if (!bRenamed)
        {
            FN2CMcpContentBrowserUtils::RemoveFolderCreatedForMutation(CreatedFolder);
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to rename asset from %s to %s: %s"), *SourceObjectPath, *DestObjectPath,
                    EngineReason.IsEmpty() ? TEXT("the engine gave no reason (the asset may be in use or locked)") : *EngineReason)
            );
        }
        const FString NewPackage = RenamedAsset.PackageName.ToString();
        const FString NewDirectory = FPackageName::GetLongPackagePath(NewPackage);
        const bool bWasMoved = !FPackageName::GetLongPackagePath(OldPackage).Equals(NewDirectory, ESearchCase::IgnoreCase);

        // Show notification if requested
        if (bShowNotification)
        {
            FString NotificationText;
            if (!bWasMoved)
            {
                // Simple rename
                NotificationText = FString::Printf(TEXT("Renamed '%s' to '%s'"), *OldAssetName, *RenamedAsset.AssetName.ToString());
            }
            else
            {
                // Move and rename
                NotificationText = FString::Printf(TEXT("Moved '%s' to '%s'"), *OldAssetName, *NewDirectory);
            }

            FNotificationInfo Info(FText::FromString(NotificationText));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }

        // Navigate to the renamed asset; assetSelected says whether that worked
        const bool bSelected = FN2CMcpContentBrowserUtils::SelectAssetAtPath(RenamedAsset.GetObjectPathString());

        // Build success response from what the registry holds now
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Asset renamed successfully"));
        Result->SetStringField(TEXT("oldPath"), OldPackage);
        Result->SetStringField(TEXT("newPath"), NewPackage);
        Result->SetStringField(TEXT("oldName"), OldAssetName);
        Result->SetStringField(TEXT("newName"), RenamedAsset.AssetName.ToString());
        Result->SetBoolField(TEXT("assetSelected"), bSelected);
        Result->SetBoolField(TEXT("wasMoved"), bWasMoved);

        // Add tips
        TArray<TSharedPtr<FJsonValue>> Tips;
        // What the move left behind, as the registry holds it now (card 0281, review fs-24)
        if (FN2CMcpContentBrowserUtils::IsRedirectorAt(SourceObjectPath))
        {
            Tips.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("A redirector was left at %s: a referencing package could not be ")
                TEXT("updated or saved. Old references still resolve through it; to remove it, right-click its folder in the content browser ")
                TEXT("and choose Update Redirector References"), *OldPackage))));
        }
        else
        {
            Tips.Add(MakeShareable(new FJsonValueString(TEXT("No redirector was left: references saved in the project's assets point at the new path"))));
        }
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Paths written in C++, config or text files are not updated"))));
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

bool FN2CMcpRenameAssetTool::ValidateSourceAsset(const FString& AssetPath, FAssetData& OutAsset, FString& OutErrorMessage) const
{
    if (AssetPath.IsEmpty())
    {
        OutErrorMessage = TEXT("Source asset path cannot be empty");
        return false;
    }

    // A rename rewrites the source package too: only where MCP tools may write, /Game and the project's plugins
    // (card 0182)
    if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(FN2CMcpContentBrowserUtils::NormalizeContentPath(AssetPath), OutErrorMessage))
    {
        return false;
    }

    // The asset, by package path or object path
    return FN2CMcpContentBrowserUtils::ResolveAsset(AssetPath, OutAsset, OutErrorMessage);
}

bool FN2CMcpRenameAssetTool::ValidateDestination(const FAssetData& SourceAsset, const FString& NewName, const FString& DestinationPath,
    FString& OutFolder, FString& OutAssetName, FString& OutErrorMessage) const
{
    const FString SourcePackage = SourceAsset.PackageName.ToString();
    if (!NewName.IsEmpty())
    {
        // A new name in the same folder, checked as given (card 0284: 'BP_New.Final' used to rename the asset to 'Final')
        OutFolder = FPackageName::GetLongPackagePath(SourcePackage);
        OutAssetName = NewName;
    }
    else
    {
        // A full package path including the name; '/Game/A/BP_New.BP_New' (object form) is the same asset path
        const FString Destination = FN2CMcpContentBrowserUtils::NormalizeContentPath(DestinationPath);

        // Only where MCP tools may write: /Game and the project's plugins (card 0182)
        if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(Destination, OutErrorMessage))
        {
            return false;
        }

        OutFolder = FPaths::GetPath(Destination);
        OutAssetName = FPaths::GetCleanFilename(Destination);
        int32 DotIndex = INDEX_NONE;
        if (OutAssetName.FindChar(TEXT('.'), DotIndex)
            && OutAssetName.Mid(DotIndex + 1).Equals(OutAssetName.Left(DotIndex), ESearchCase::CaseSensitive))
        {
            OutAssetName.LeftInline(DotIndex);
        }
        if (OutFolder.IsEmpty() || !FN2CMcpContentBrowserUtils::ValidateContentPath(OutFolder, OutErrorMessage))
        {
            OutErrorMessage = FString::Printf(TEXT("Invalid destination path '%s': %s"), *DestinationPath,
                OutErrorMessage.IsEmpty() ? TEXT("give the full package path, e.g. /Game/Folder/NewName") : *OutErrorMessage);
            return false;
        }
    }

    // Validate the new asset name before any path is built with it
    if (!FN2CMcpContentBrowserUtils::ValidateNewAssetName(OutAssetName, OutErrorMessage))
    {
        return false;
    }

    const FString DestinationPackage = OutFolder / OutAssetName;
    FText Reason;
    if (!FPackageName::IsValidLongPackageName(DestinationPackage, /*bIncludeReadOnlyRoots*/ false, &Reason))
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid destination '%s': %s"), *DestinationPackage, *Reason.ToString());
        return false;
    }

    // The same path (no-op), or the same path in another case: package names are case-insensitive, so a case-only
    // rename would meet the asset itself
    if (DestinationPackage.Equals(SourcePackage, ESearchCase::CaseSensitive))
    {
        OutErrorMessage = TEXT("Source and destination paths are the same");
        return false;
    }
    if (DestinationPackage.Equals(SourcePackage, ESearchCase::IgnoreCase))
    {
        OutErrorMessage = FString::Printf(TEXT("Case-only renames are not supported (%s to %s): package names are case-insensitive. ")
            TEXT("Rename to another name first, then to the new spelling."), *SourcePackage, *DestinationPackage);
        return false;
    }

    // Check if destination already exists
    if (FN2CMcpContentBrowserUtils::DoesAssetPackageExist(DestinationPackage))
    {
        OutErrorMessage = FString::Printf(TEXT("An asset already exists at the destination: %s"), *DestinationPackage);
        return false;
    }

    return true;
}
