// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * list-blueprint-graphs (card 0157): every graph of a Blueprint by asset path, with no Blueprint editor open: name,
 * GraphGuid, kind, node count and whether the Blueprint editor shows it read-only.
 */
class FN2CMcpListBlueprintGraphsTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }
};
