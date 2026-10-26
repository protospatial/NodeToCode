// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCopyAssetTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"

REGISTER_MCP_TOOL(FN2CMcpCopyAssetTool)

FMcpToolDefinition FN2CMcpCopyAssetTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("copy-asset"),
        TEXT("Copy an asset to a new asset path in the content browser. The source may be under any mounted root (/Game/, /Engine/, a plugin's /<PluginName>/); ")
        TEXT("the destination must be under /Game/ or a project plugin's content. Missing destination folders are created. ")
        TEXT("An existing asset at the destination is never replaced: the copy is refused."),
        N2CMcpToolsets::ContentBrowser
    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("sourcePath"), TEXT("string"));
    Properties.Add(TEXT("destinationPath"), TEXT("string"));

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
        Describe(TEXT("destinationPath"), TEXT("Package path of the new asset, including its name (e.g., '/Game/Folder/Asset_Copy'); the name may not contain '.', '/', '\\' or spaces"));
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

        // Validate source asset
        FAssetData SourceAsset;
        FString ValidationError;
        if (!FN2CMcpContentBrowserUtils::ResolveAsset(SourcePath, SourceAsset, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("sourcePath: ") + ValidationError);
        }

        // Validate destination path, name and that nothing is there; nothing is created yet
        FString DestinationFolder;
        FString DestinationPackage;
        if (!ValidateDestinationPath(DestinationPath, DestinationFolder, DestinationPackage, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }

        // Perform the copy operation
        UEditorAssetSubsystem* EditorAssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
        if (!EditorAssetSubsystem)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to get EditorAssetSubsystem"));
        }

        const FString SourceObjectPath = SourceAsset.GetObjectPathString();
        const FString DestObjectPath = DestinationPackage + TEXT(".") + FPackageName::GetShortName(DestinationPackage);

        // The destination folder, only now, and removed again if the copy fails (card 0284)
        FString CreatedFolder;
        FString FolderError;
        if (!FN2CMcpContentBrowserUtils::CreateFolderForMutation(DestinationFolder, CreatedFolder, FolderError))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to create destination directory '%s': %s"), *DestinationFolder, *FolderError));
        }

        // Log the operation
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Copying asset from %s to %s"), *SourceObjectPath, *DestObjectPath),
            EN2CLogSeverity::Info
        );

        // Duplicate the asset, then read the copy back from the registry
        UObject* DuplicatedAsset = nullptr;
        FString EngineReason;
        {
            FN2CMcpEngineErrorCapture Capture;
            DuplicatedAsset = EditorAssetSubsystem->DuplicateAsset(SourceObjectPath, DestObjectPath);
            EngineReason = Capture.GetReason();
        }
        FAssetData CopiedAsset;
        FString ReadBackError;
        if (DuplicatedAsset && !FN2CMcpContentBrowserUtils::ResolveAsset(DestObjectPath, CopiedAsset, ReadBackError))
        {
            DuplicatedAsset = nullptr;
            EngineReason = TEXT("the engine reported success, but no asset is at the destination");
        }
        if (!DuplicatedAsset)
        {
            FN2CMcpContentBrowserUtils::RemoveFolderCreatedForMutation(CreatedFolder);
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to copy asset from %s to %s: %s"), *SourceObjectPath, *DestObjectPath,
                    EngineReason.IsEmpty() ? TEXT("the engine gave no reason") : *EngineReason)
            );
        }

        // Build success response
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), TEXT("Asset copied successfully"));
        Result->SetStringField(TEXT("sourcePath"), SourceAsset.PackageName.ToString());
        Result->SetStringField(TEXT("destinationPath"), CopiedAsset.PackageName.ToString());
        Result->SetStringField(TEXT("assetName"), CopiedAsset.AssetName.ToString());
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

bool FN2CMcpCopyAssetTool::ValidateDestinationPath(const FString& DestinationPath, FString& OutFolder, FString& OutPackage, FString& OutErrorMessage) const
{
    if (DestinationPath.IsEmpty())
    {
        OutErrorMessage = TEXT("Destination path cannot be empty");
        return false;
    }

    // Normalize the path
    OutPackage = FN2CMcpContentBrowserUtils::NormalizeContentPath(DestinationPath);

    // Only where MCP tools may write: /Game and the project's plugins (card 0182)
    if (!FN2CMcpContentBrowserUtils::IsWritableContentPath(OutPackage, OutErrorMessage))
    {
        return false;
    }

    // Extract directory path
    OutFolder = FPaths::GetPath(OutPackage);
    if (OutFolder.IsEmpty() || !FN2CMcpContentBrowserUtils::ValidateContentPath(OutFolder, OutErrorMessage))
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid destination path '%s': %s"), *DestinationPath,
            OutErrorMessage.IsEmpty() ? TEXT("give the new asset's package path, e.g. /Game/Folder/BP_Copy") : *OutErrorMessage);
        return false;
    }

    // Validate asset name, as given, before anything is created
    if (!FN2CMcpContentBrowserUtils::ValidateNewAssetName(FPaths::GetCleanFilename(OutPackage), OutErrorMessage))
    {
        return false;
    }
    FText Reason;
    if (!FPackageName::IsValidLongPackageName(OutPackage, /*bIncludeReadOnlyRoots*/ false, &Reason))
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid destination '%s': %s"), *OutPackage, *Reason.ToString());
        return false;
    }

    // Check if destination already exists: the copy never replaces it (UEditorAssetSubsystem::DuplicateAsset refuses)
    if (FN2CMcpContentBrowserUtils::DoesAssetPackageExist(OutPackage))
    {
        OutErrorMessage = FString::Printf(TEXT("Destination asset already exists: %s. Copy to another name, or rename or delete the existing asset first."),
            *OutPackage);
        return false;
    }

    return true;
}
