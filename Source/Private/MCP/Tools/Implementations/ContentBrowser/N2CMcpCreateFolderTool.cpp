// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateFolderTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"

REGISTER_MCP_TOOL(FN2CMcpCreateFolderTool)

FMcpToolDefinition FN2CMcpCreateFolderTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("create-folder"),
        TEXT("Create a new folder in the content browser")
    ,

    	N2CMcpToolsets::ContentBrowser

    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("folderPath"), TEXT("string"));
    Properties.Add(TEXT("createParents"), TEXT("boolean"));

    TArray<FString> Required;
    Required.Add(TEXT("folderPath"));

    Definition.InputSchema = BuildInputSchema(Properties, Required);

    return Definition;
}

FMcpToolCallResult FN2CMcpCreateFolderTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Extract parameters
        FString FolderPath;
        if (!Arguments->TryGetStringField(TEXT("folderPath"), FolderPath))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: folderPath"));
        }

        bool bCreateParents = false;
        Arguments->TryGetBoolField(TEXT("createParents"), bCreateParents);

        // Validate folder path
        FString NormalizedPath;
        FString ValidationError;
        if (!ValidateFolderPath(FolderPath, NormalizedPath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Check if folder already exists
        if (FN2CMcpContentBrowserUtils::DoesPathExist(NormalizedPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Folder already exists: %s"), *NormalizedPath)
            );
        }

        // If createParents is false, ensure parent directory exists
        if (!bCreateParents)
        {
            FString ParentPath = FPaths::GetPath(NormalizedPath);
            if (!ParentPath.IsEmpty() && !FN2CMcpContentBrowserUtils::DoesPathExist(ParentPath))
            {
                return FMcpToolCallResult::CreateErrorResult(
                    FString::Printf(TEXT("Parent folder does not exist: %s. Set createParents=true to create missing parent folders."), *ParentPath)
                );
            }
        }

        // Create the folder
        FString CreationError;
        bool bSuccess = false;

        if (bCreateParents)
        {
            // Use EnsureDirectoryExists which creates all parent directories
            bSuccess = FN2CMcpContentBrowserUtils::EnsureDirectoryExists(NormalizedPath, CreationError);
        }
        else
        {
            // Use CreateContentFolder which only creates the specific folder
            bSuccess = FN2CMcpContentBrowserUtils::CreateContentFolder(NormalizedPath, CreationError);
        }

        if (!bSuccess)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to create folder '%s': %s"), *NormalizedPath, *CreationError)
            );
        }

        // Navigate to the newly created folder
        FN2CMcpContentBrowserUtils::NavigateToPath(NormalizedPath);

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Created folder: %s"), *NormalizedPath),
            EN2CLogSeverity::Info
        );

        // Build success response
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Folder created successfully"));
        Result->SetStringField(TEXT("folderPath"), NormalizedPath);
        Result->SetBoolField(TEXT("navigated"), true);

        // Add tips
        TArray<TSharedPtr<FJsonValue>> Tips;
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to list contents of the new folder"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'create-blueprint-class' to create a Blueprint in the new folder"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Set createParents=true to automatically create missing parent folders"))));
        Result->SetArrayField(TEXT("tips"), Tips);

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

bool FN2CMcpCreateFolderTool::ValidateFolderPath(const FString& Path, FString& OutNormalizedPath, FString& OutErrorMessage) const
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
        OutErrorMessage = TEXT("Folder path must start with /Game/ or a valid plugin path. Cannot create folders in /Engine/");
        return false;
    }

    // Remove trailing slash if present
    if (OutNormalizedPath.EndsWith(TEXT("/")))
    {
        OutNormalizedPath = OutNormalizedPath.LeftChop(1);
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

    // Validate folder name (last component)
    FString FolderName = FPaths::GetCleanFilename(OutNormalizedPath);
    if (FolderName.IsEmpty())
    {
        OutErrorMessage = TEXT("Folder name cannot be empty");
        return false;
    }

    // Check for reserved names
    TArray<FString> ReservedNames = { TEXT("CON"), TEXT("PRN"), TEXT("AUX"), TEXT("NUL") };
    if (ReservedNames.Contains(FolderName.ToUpper()))
    {
        OutErrorMessage = TEXT("Folder name is a reserved system name");
        return false;
    }

    // Check folder name doesn't contain dots (to avoid confusion with assets)
    if (FolderName.Contains(TEXT(".")))
    {
        OutErrorMessage = TEXT("Folder names cannot contain dots");
        return false;
    }

    return true;
}