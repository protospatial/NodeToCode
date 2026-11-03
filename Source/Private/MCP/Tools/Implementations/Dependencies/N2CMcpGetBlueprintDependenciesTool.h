// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * get-blueprint-dependencies (card 0319): the dependency index (FN2CDependencyIndex, card 0318) over asset paths and
 * content folders, by path, with no editor focus: nodes, typed edges, the dependencies-first order and the cycle
 * groups, as structured content (N2CMcpDependencies). toFile, or a result over N2CMcpBlueprintExport::MaxInlineTokens,
 * writes the JSON export under Saved/NodeToCode/Dependencies.
 */
class FN2CMcpGetBlueprintDependenciesTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/** Longest the index may take to build (it loads the Blueprints it indexes) */
	static constexpr float TimeoutSeconds = 300.0f;
	/** Most entries paths takes */
	static constexpr int32 MaxPaths = 100;
};
