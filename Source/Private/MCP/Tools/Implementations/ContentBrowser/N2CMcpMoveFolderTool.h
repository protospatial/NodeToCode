// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * @class FN2CMcpMoveFolderTool
 * @brief MCP tool for moving folders and their contents in the content browser
 *
 * Moves a folder with its assets and subfolders (empty ones too) to <destinationPath>/<folder name> through
 * FN2CMcpContentBrowserUtils::MoveFolder (UEditorAssetSubsystem::RenameDirectory), and reports what moved, what did
 * not and the redirectors left.
 */
class FN2CMcpMoveFolderTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    bool ValidateFolderPath(const FString& Path, FString& OutNormalizedPath, FString& OutErrorMessage) const;
    bool ValidateDestinationPath(const FString& DestinationPath, const FString& SourcePath, const FString& FolderName, FString& OutFullPath, FString& OutErrorMessage) const;
};
