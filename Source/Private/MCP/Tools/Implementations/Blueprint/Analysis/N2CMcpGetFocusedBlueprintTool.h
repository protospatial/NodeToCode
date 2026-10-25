// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

struct FN2CCapture;

/**
 * MCP tool for retrieving the currently focused Blueprint graph as N2CJSON
 */
class FN2CMcpGetFocusedBlueprintTool : public FN2CMcpToolBase
{
public:
	/** Get the tool definition */
	virtual FMcpToolDefinition GetDefinition() const override;
	
	/** Execute the tool */
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	
	/** This tool requires Game Thread execution for accessing Editor APIs */
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * The tool's result for a capture: the N2C JSON with each node's and pin's "id" replaced by "ids" {"short", "guid"}
	 * (pins also "name"), flows keeping the short IDs, and the capture's top-level "guids" table (graph GUIDs, node ID
	 * -> NodeGuid, pin ID -> {node, guid}), indented. Empty when the capture cannot be serialized.
	 * N2CMcpBlueprintExport::ToResultJson(Capture, false): get-blueprint-n2cjson writes the same document (card 0157).
	 */
	static FString BuildResultJson(const FN2CCapture& Capture);
};