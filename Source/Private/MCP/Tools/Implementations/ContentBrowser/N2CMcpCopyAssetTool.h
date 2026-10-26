// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for copying assets to new locations in the content browser.
 * Supports both package paths (/Game/Folder/Asset) and object paths (/Game/Folder/Asset.Asset).
 */
class FN2CMcpCopyAssetTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /** The destination package path and its folder, validated (write policy, name, nothing there yet); creates nothing */
    bool ValidateDestinationPath(const FString& DestinationPath, FString& OutFolder, FString& OutPackage, FString& OutErrorMessage) const;
};