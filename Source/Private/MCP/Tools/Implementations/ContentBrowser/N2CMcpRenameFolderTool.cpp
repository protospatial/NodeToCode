// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRenameFolderTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
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

        // A case-only rename: folder and package names are case-insensitive, so the folder would meet itself (card 0284)
        if (NewFolderPath.Equals(NormalizedSourcePath, ESearchCase::IgnoreCase))
        {
            return FMcpToolCallResult::CreateErrorResult(NewFolderPath.Equals(NormalizedSourcePath, ESearchCase::CaseSensitive)
                ? FString::Printf(TEXT("The folder is already named '%s'"), *NewName)
                : FString::Printf(TEXT("Case-only renames are not supported ('%s' to '%s'): folder names are case-insensitive. ")
                    TEXT("Rename to another name first, then to the new spelling."), *OldFolderName, *NewName));
        }

        // Check if destination already exists
        if (FN2CMcpContentBrowserUtils::DoesPathExist(NewFolderPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("A folder with the name '%s' already exists in '%s'"), 
                    *NewName, *ParentPath)
            );
        }

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Renaming folder '%s' to '%s'"), *NormalizedSourcePath, *NewName),
            EN2CLogSeverity::Info
        );

        // One UEditorAssetSubsystem::RenameDirectory call, read back from the asset registry (card 0281)
        FN2CMcpFolderMoveResult Move;
        if (!FN2CMcpContentBrowserUtils::MoveFolder(NormalizedSourcePath, NewFolderPath, Move))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to rename folder: %s"), *Move.Describe())
            );
        }

        // Show notification if requested
        if (bShowNotification)
        {
            FNotificationInfo Info(FText::Format(
                NSLOCTEXT("NodeToCode", "FolderRenamed", "Renamed folder '{0}' to '{1}' ({2} assets)"),
                FText::FromString(OldFolderName),
                FText::FromString(NewName),
                FText::AsNumber(Move.MovedAssets.Num())
            ));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }

        // Navigate to the renamed folder
        const bool bNavigated = FN2CMcpContentBrowserUtils::NavigateToPath(Move.Destination);

        // Build success response from what the registry holds now
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Folder renamed successfully"));
        Result->SetStringField(TEXT("oldPath"), Move.Source);
        Result->SetStringField(TEXT("newPath"), Move.Destination);
        Result->SetStringField(TEXT("oldName"), OldFolderName);
        Result->SetStringField(TEXT("newName"), NewName);
        Result->SetNumberField(TEXT("assetsRenamed"), Move.MovedAssets.Num());
        Result->SetBoolField(TEXT("navigated"), bNavigated);
        Move.WriteTo(*Result);

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

    // Don't allow renaming root folders (/Game, a plugin's /<PluginName>)
    if (FN2CMcpContentBrowserUtils::IsContentMountRoot(OutNormalizedPath))
    {
        OutErrorMessage = FString::Printf(TEXT("Cannot rename the content root %s"), *OutNormalizedPath);
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

    // Check for reserved names: the Windows device names exactly, so Components or Common are fine
    if (FN2CMcpContentBrowserUtils::IsReservedDeviceName(NewName))
    {
        OutErrorMessage = TEXT("Folder name is a reserved system name");
        return false;
    }

    return true;
}
