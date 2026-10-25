// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * get-blueprint-n2cjson (card 0157): any Blueprint's graphs as N2C JSON by asset path, with no Blueprint editor open.
 * One graph (graphName or graphGuid) or every graph; a document over N2CMcpBlueprintExport::MaxInlineTokens, or any
 * document with toFile, is written under Saved/NodeToCode/Exports and the result names the file.
 */
class FN2CMcpGetBlueprintN2CJsonTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }
};
