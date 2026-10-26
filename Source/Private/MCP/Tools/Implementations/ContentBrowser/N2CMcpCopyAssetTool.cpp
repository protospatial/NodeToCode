// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCopyAssetTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"

REGISTER_MCP_TOOL(FN2CMcpCopyAssetTool)

FMcpToolDefinition FN2CMcpCopyAssetTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("copy-asset"),
        TEXT("Copy an asset to a new asset path in the content browser. The source may be under /Game/ or /Engine/; ")
        TEXT("the destination must be under /Game/. Missing destination folders are created. ")
        TEXT("An existing asset at the destination is never replaced: the copy fails."),
        N2CMcpToolsets::ContentBrowser
    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("sourcePath"), TEXT("string"));
    Properties.Add(TEXT("destinationPath"), TEXT("string"));
    Properties.Add(TEXT("overwriteExisting"), TEXT("boolean"));

    TArray<FString> Required;
    Required.Add(TEXT("sourcePath"));
    Required.Add(TEXT("destinationPath"));

    Definition.InputSchema = BuildInputSchema(Properties, Required);

    // What Execute does with each argument (BuildInputSchema writes only the types)
    const TSharedPtr<FJsonObject>* SchemaProperties = nullptr;
    if (Definition.InputSchema->TryGetObjectField(TEXT("properties"), SchemaProperties))
    {
        auto Describe = [SchemaProperties](const TCHAR* Name, const TCHAR* Description)
        {
            const TSharedPtr<FJsonObject>* Prop = nullptr;
            if ((*SchemaProperties)->TryGetObjectField(Name, Prop))
            {
                (*Prop)->SetStringField(TEXT("description"), Description);
            }
        };
        Describe(TEXT("sourcePath"), TEXT("Asset to copy: package path '/Game/Folder/Asset' or object path '/Game/Folder/Asset.Asset'"));
        Describe(TEXT("destinationPath"), TEXT("Package path of the new asset, including its name (e.g., '/Game/Folder/Asset_Copy'); the name may not contain spaces or dots"));
        Describe(TEXT("overwriteExisting"), TEXT("Default false: an existing destination asset is reported before copying. true skips that check, but the copy still fails, because it never replaces an existing asset"));
    }

    return Definition;
}

FMcpToolCallResult FN2CMcpCopyAssetTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
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

        bool bOverwriteExisting = false;
        Arguments->TryGetBoolField(TEXT("overwriteExisting"), bOverwriteExisting);

        // Validate source asset
        FString NormalizedSourcePath;
        FString ValidationError;
        if (!ValidateSourceAsset(SourcePath, NormalizedSourcePath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Validate destination path
        FString NormalizedDestPath;
        if (!ValidateDestinationPath(DestinationPath, NormalizedDestPath, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Check if destination already exists
        if (!bOverwriteExisting)
        {
            FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

            // Convert to object path to check if asset exists
            FString DestObjectPath = NormalizedDestPath;
            if (!NormalizedDestPath.Contains(TEXT(".")))
            {
                FString AssetName = FPaths::GetBaseFilename(NormalizedDestPath);
                DestObjectPath = NormalizedDestPath + TEXT(".") + AssetName;
            }

            FAssetData ExistingAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(DestObjectPath));
            if (ExistingAsset.IsValid())
            {
                return FMcpToolCallResult::CreateErrorResult(
                    FString::Printf(TEXT("Destination asset already exists: %s. Copy to another name, or rename or delete the existing asset first."), 
                        *NormalizedDestPath)
                );
            }
        }

        // Perform the copy operation
        UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
        if (!EditorAssetSubsystem)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to get EditorAssetSubsystem"));
        }

        // Convert paths to package paths for the API
        FString SourcePackagePath = ConvertToPackagePath(NormalizedSourcePath);
        FString DestPackagePath = ConvertToPackagePath(NormalizedDestPath);

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Copying asset from %s to %s"), *SourcePackagePath, *DestPackagePath),
            EN2CLogSeverity::Info
        );

        // Duplicate the asset
        UObject* DuplicatedAsset = EditorAssetSubsystem->DuplicateAsset(SourcePackagePath, DestPackagePath);
        if (!DuplicatedAsset)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to copy asset from %s to %s. Make sure the destination folder exists and you have write permissions."), 
                    *SourcePackagePath, *DestPackagePath)
            );
        }

        // Build success response
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Asset copied successfully"));
        Result->SetStringField(TEXT("sourcePath"), SourcePackagePath);
        Result->SetStringField(TEXT("destinationPath"), DestPackagePath);
        Result->SetStringField(TEXT("assetName"), DuplicatedAsset->GetName());
        Result->SetStringField(TEXT("assetClass"), DuplicatedAsset->GetClass()->GetName());

        // Add tips
        TArray<TSharedPtr<FJsonValue>> Tips;
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'move-asset' to move the original asset instead of copying"))));
        Tips.Add(MakeShareable(new FJsonValueString(TEXT("Use 'open-blueprint-asset' to open the copied Blueprint"))));
        Result->SetArrayField(TEXT("tips"), Tips);

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

bool FN2CMcpCopyAssetTool::ValidateSourceAsset(const FString& AssetPath, FString& OutNormalizedPath, FString& OutErrorMessage) const
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
            OutErrorMessage = FString::Printf(TEXT("Source asset not found. Tried paths: %s and %s. Expected format: /Game/Folder/AssetName or /Game/Folder/AssetName.AssetName"), 
                *OutNormalizedPath, *ObjectPath);
            return false;
        }
    }

    return true;
}

bool FN2CMcpCopyAssetTool::ValidateDestinationPath(const FString& DestinationPath, FString& OutNormalizedPath, FString& OutErrorMessage) const
{
    if (DestinationPath.IsEmpty())
    {
        OutErrorMessage = TEXT("Destination path cannot be empty");
        return false;
    }

    // Normalize the path
    OutNormalizedPath = DestinationPath;
    OutNormalizedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

    // Ensure it starts with /Game or plugin path (no /Engine for destination)
    if (!OutNormalizedPath.StartsWith(TEXT("/Game/")) && 
        !OutNormalizedPath.Contains(TEXT("/Plugins/")))
    {
        OutErrorMessage = TEXT("Destination path must start with /Game/ or a valid plugin path. Cannot copy to /Engine/");
        return false;
    }

    // Extract directory path
    FString DirectoryPath = FPaths::GetPath(OutNormalizedPath);
    if (DirectoryPath.IsEmpty())
    {
        OutErrorMessage = TEXT("Invalid destination path format");
        return false;
    }

    // Ensure destination directory exists
    FString CreationError;
    if (!FN2CMcpContentBrowserUtils::EnsureDirectoryExists(DirectoryPath, CreationError))
    {
        OutErrorMessage = FString::Printf(TEXT("Failed to ensure destination directory exists '%s': %s"), *DirectoryPath, *CreationError);
        return false;
    }

    // Validate asset name
    FString AssetName = FPaths::GetBaseFilename(OutNormalizedPath);
    if (AssetName.IsEmpty())
    {
        OutErrorMessage = TEXT("Destination asset name cannot be empty");
        return false;
    }

    // Check for invalid characters in asset name
    if (AssetName.Contains(TEXT(" ")) || AssetName.Contains(TEXT(".")) || AssetName.Contains(TEXT("/")))
    {
        OutErrorMessage = TEXT("Asset name cannot contain spaces, dots, or slashes");
        return false;
    }

    return true;
}

FString FN2CMcpCopyAssetTool::ConvertToPackagePath(const FString& AssetPath) const
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