// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * get-material-n2cjson (card 0453, decision 0009): a Material or Material Function as N2C JSON by asset path, read from
 * the saved asset (never a Material Editor's working copy: target.workingCopy false), with no editor open. Other
 * material assets (instances, layers, blends) are UNSUPPORTED_ASSET_TYPE, as are Blueprints (get-blueprint-n2cjson
 * reads those). A document over N2CMcpBlueprintExport::MaxInlineTokens, or any with toFile, is written under
 * Saved/NodeToCode/Exports.
 */
class FN2CMcpGetMaterialN2CJsonTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * The UMaterial or UMaterialFunction-like asset at Path (/Engine/BasicShapes/BasicShapeMaterial or the object path),
	 * or null with OutError ASSET_NOT_FOUND. Any class is returned; capture refuses the unsupported ones. Game thread.
	 */
	static UObject* LoadMaterialAsset(const FString& Path, FString& OutError);

	/**
	 * True when Path names an asset of a material class (UMaterialInterface or UMaterialFunctionInterface and their
	 * subclasses): get-blueprint-n2cjson names this tool for it
	 */
	static bool IsMaterialAssetPath(const FString& Path);

	/** "UNSUPPORTED_ASSET_TYPE: '<Path>' is a material asset, not a Blueprint: read it with get-material-n2cjson (materialPath)" */
	static FString NotABlueprintMessage(const FString& Path);
};
