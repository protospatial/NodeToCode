// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * create-migration-plan (card 0323): a reviewable Blueprint-to-C++ migration plan from the dependency index
 * (FN2CMigrationPlan), written to Saved/NodeToCode/Migration/<plan-id>/plan.json and plan.md. It changes nothing else.
 */
class FN2CMcpCreateMigrationPlanTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/** Longest a plan may take (it loads every Blueprint it plans and captures each graph for its token estimate) */
	static constexpr float TimeoutSeconds = 600.0f;
	/** Most entries paths and exclude take */
	static constexpr int32 MaxPaths = 100;
};
