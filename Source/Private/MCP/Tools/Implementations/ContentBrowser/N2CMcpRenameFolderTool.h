// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * @class FN2CMcpRenameFolderTool
 * @brief MCP tool for renaming folders in the content browser
 * 
 * Renames a folder with its assets and subfolders (empty ones too) through FN2CMcpContentBrowserUtils::MoveFolder
 * (UEditorAssetSubsystem::RenameDirectory), and reports what moved, what did not and the redirectors left.
 * 
 * @example
 * {
 *   "sourcePath": "/Game/OldFolderName",
 *   "newName": "NewFolderName"
 * }
 * 
 * @note The folder will be renamed in place within its parent directory
 */
class FN2CMcpRenameFolderTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Validates the source folder path
     * @param Path The folder path to validate
     * @param OutNormalizedPath The normalized path
     * @param OutErrorMessage Error message if validation fails
     * @return True if validation succeeds
     */
    bool ValidateFolderPath(const FString& Path, FString& OutNormalizedPath, FString& OutErrorMessage) const;

    /**
     * Validates the new folder name
     * @param NewName The new folder name
     * @param OutErrorMessage Error message if validation fails
     * @return True if validation succeeds
     */
    bool ValidateNewName(const FString& NewName, FString& OutErrorMessage) const;
};