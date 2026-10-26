// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * translate-batch (card 0169): translates many graphs, by asset path or by tag, with no Blueprint editor open. A
 * long-running tool: InvokeAsync checks the overrides and the targets, starts an FN2CTranslateBatchCall (one
 * UN2CBatchTranslationOrchestrator batch with the call's provider, model and language) and returns its future;
 * progress and cancellation go through the call context. Each translation is saved under the translation output
 * folder (BatchTranslation_<time>/<graph>) and recorded for the translation viewer.
 */
class FN2CMcpTranslateBatchTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments) override;

	/** Graphs one call may translate (each is one LLM request) */
	static constexpr int32 MaxGraphs = 50;
};
