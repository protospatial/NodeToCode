// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

struct FAssetData;

/**
 * @class FN2CMcpRenameAssetTool
 * @brief MCP tool for renaming assets in the content browser
 * 
 * This tool allows renaming an asset to a new name within the same directory
 * or moving it to a different location (rename with path change).
 * 
 * @example Rename asset within same directory:
 * {
 *   "sourcePath": "/Game/Blueprints/OldName",
 *   "newName": "NewName"
 * }
 * 
 * @example Move and rename asset:
 * {
 *   "sourcePath": "/Game/Blueprints/OldName",
 *   "destinationPath": "/Game/NewFolder/NewName"
 * }
 */
class FN2CMcpRenameAssetTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Validates the source asset path (write policy) and resolves the asset from the registry
     * @param AssetPath The asset path to validate, package or object path
     * @param OutAsset The asset
     * @param OutErrorMessage Error message if validation fails
     * @return True if validation succeeds
     */
    bool ValidateSourceAsset(const FString& AssetPath, FAssetData& OutAsset, FString& OutErrorMessage) const;

    /**
     * Validates the new name or the full destination path, and that nothing is there yet; creates nothing
     * @param SourceAsset The asset to rename
     * @param NewName The new name (or empty)
     * @param DestinationPath The full destination package path (or empty)
     * @param OutFolder The destination folder
     * @param OutAssetName The new asset name, validated
     * @param OutErrorMessage Error message if validation fails
     * @return True if validation succeeds
     */
    bool ValidateDestination(const FAssetData& SourceAsset, const FString& NewName, const FString& DestinationPath,
        FString& OutFolder, FString& OutAssetName, FString& OutErrorMessage) const;
};
