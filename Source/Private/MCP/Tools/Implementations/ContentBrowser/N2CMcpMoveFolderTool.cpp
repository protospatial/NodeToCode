// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpMoveFolderTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
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

        // Validate destination path; nothing is created yet
        FString FullDestinationPath;
        if (!ValidateDestinationPath(DestinationPath, NormalizedSourcePath, FolderName, FullDestinationPath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Moving folder '%s' to '%s'"), *NormalizedSourcePath, *FullDestinationPath),
            EN2CLogSeverity::Info
        );

        // One UEditorAssetSubsystem::RenameDirectory call, read back from the asset registry (card 0281)
        FN2CMcpFolderMoveResult Move;
        if (!FN2CMcpContentBrowserUtils::MoveFolder(NormalizedSourcePath, FullDestinationPath, Move))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to move folder: %s"), *Move.Describe())
            );
        }

        // Show notification if requested
        if (bShowNotification)
        {
            FNotificationInfo Info(FText::Format(
                NSLOCTEXT("NodeToCode", "FolderMoved", "Moved folder '{0}' to '{1}' ({2} assets)"),
                FText::FromString(FolderName),
                FText::FromString(Move.Destination),
                FText::AsNumber(Move.MovedAssets.Num())
            ));
            Info.ExpireDuration = 3.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
        }

        // Navigate to the new location
        const bool bNavigated = FN2CMcpContentBrowserUtils::NavigateToPath(Move.Destination);

        // Build success response from what the registry holds now
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Folder moved successfully"));
        Result->SetStringField(TEXT("sourcePath"), Move.Source);
        Result->SetStringField(TEXT("destinationPath"), Move.Destination);
        Result->SetNumberField(TEXT("assetsMoved"), Move.MovedAssets.Num());
        Result->SetBoolField(TEXT("navigated"), bNavigated);
        Move.WriteTo(*Result);

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

bool FN2CMcpMoveFolderTool::ValidateDestinationPath(const FString& DestinationPath, const FString& SourcePath, const FString& FolderName,
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

    // Only where MCP tools may write: /Game and the project's plugins (card 0182); and a valid folder path
    if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(NormalizedDestPath, OutErrorMessage)
        || !FN2CMcpContentBrowserUtils::ValidateContentPath(NormalizedDestPath, OutErrorMessage))
    {
        return false;
    }

    // Not into the folder itself or one of its subfolders, in any case (card 0281, review fs-9)
    if (FN2CMcpContentBrowserUtils::IsSameOrInsideFolder(NormalizedDestPath, SourcePath))
    {
        OutErrorMessage = FString::Printf(TEXT("Cannot move %s into itself or one of its own subfolders (%s)"), *SourcePath, *NormalizedDestPath);
        return false;
    }

    // Build the full destination path
    OutFullPath = NormalizedDestPath / FolderName;

    // Already there: the folder's parent is the destination
    if (OutFullPath.Equals(SourcePath, ESearchCase::IgnoreCase))
    {
        OutErrorMessage = FString::Printf(TEXT("%s is already in %s"), *SourcePath, *NormalizedDestPath);
        return false;
    }

    // Check if destination already exists
    if (FN2CMcpContentBrowserUtils::DoesPathExist(OutFullPath))
    {
        OutErrorMessage = FString::Printf(TEXT("Destination folder already exists: %s"), *OutFullPath);
        return false;
    }

    return true;
}
