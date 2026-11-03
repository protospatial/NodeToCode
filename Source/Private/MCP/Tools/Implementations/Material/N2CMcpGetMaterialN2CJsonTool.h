// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Core/N2CCapture.h"

/**
 * get-material-n2cjson (card 0453, decision 0009): a Material or Material Function as N2C JSON. By asset path it reads
 * the saved asset (target.workingCopy false), with no editor open; with no path it reads the working copy of the
 * Material Editor activated last (card 0463: FN2CBlueprintEditorTracker::GetFocusedMaterialAsset and WorkingCopyOf,
 * resolvedBy focus, workingCopy true, workingCopyPath) or fails with NO_FOCUSED_MATERIAL. Other
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

	/**
	 * The material the tools read (this tool and translate-material, card 0455): MaterialPath's saved asset, or with an
	 * empty path the focused Material Editor's working copy, captured within Depth. OutAsset is the asset (never the
	 * working copy) and OutTarget {assetPath, kind, resolvedBy, workingCopy, workingCopyPath}. False with OutError
	 * starting with its code: NO_FOCUSED_MATERIAL, ASSET_NOT_FOUND, UNSUPPORTED_ASSET_TYPE, CAPTURE_FAILED. Game thread.
	 */
	static bool ResolveAndCapture(const FString& MaterialPath, int32 Depth, UObject*& OutAsset, FN2CCapture& OutCapture,
		TSharedPtr<FJsonObject>& OutTarget, FString& OutError);

	/** "UNSUPPORTED_ASSET_TYPE: '<Path>' is a material asset, not a Blueprint: read it with get-material-n2cjson (materialPath)" */
	static FString NotABlueprintMessage(const FString& Path);
};
