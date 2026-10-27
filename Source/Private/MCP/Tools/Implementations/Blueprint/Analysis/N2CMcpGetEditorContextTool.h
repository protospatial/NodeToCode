// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * get-editor-context (card 0275): what the user is looking at, in one call: the focused Blueprint and graph, the nodes
 * selected in it, every open Blueprint editor with the graph it shows, Play In Editor state and the engine version.
 * Reads only (N2CMcpEditorContext::Build).
 */
class FN2CMcpGetEditorContextTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }
};
