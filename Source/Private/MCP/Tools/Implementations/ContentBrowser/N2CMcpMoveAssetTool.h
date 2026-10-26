// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

struct FAssetData;

/**
 * MCP tool for moving/renaming assets within the content browser.
 * Supports both single asset moves and batch operations.
 */
class FN2CMcpMoveAssetTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	/**
	 * Validates the source asset path (write policy, registry, source control) and resolves the asset
	 */
	bool ValidateSourceAsset(const FString& AssetPath, FAssetData& OutAsset, FString& OutErrorMessage) const;

	/**
	 * Validates the destination folder and builds the new package path (AssetName is already validated); creates nothing
	 */
	bool ValidateDestinationPath(const FString& DestinationPath, const FString& AssetName, const FAssetData& SourceAsset,
		FString& OutFolder, FString& OutPackage, FString& OutErrorMessage) const;

	/**
	 * Creates the destination folder, moves the asset and reads it back from the registry; on failure removes the
	 * folder it created and gives the engine's reason
	 */
	bool MoveAsset(const FAssetData& SourceAsset, const FString& DestinationFolder, const FString& DestinationObjectPath,
		FAssetData& OutMovedAsset, FString& OutErrorMessage) const;

	/**
	 * Builds the success response with move details, from the moved asset as the registry holds it
	 */
	TSharedPtr<FJsonObject> BuildSuccessResponse(const FString& OldPackage, const FAssetData& MovedAsset, bool bIsRename,
		bool bLeftRedirector) const;
};
