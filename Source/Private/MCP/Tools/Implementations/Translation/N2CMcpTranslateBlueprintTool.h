// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "MCP/Tools/N2CMcpToolBase.h"
#include "N2CTranslateBlueprintCall.h"

/**
 * translate-focused-blueprint: a long-running tool. InvokeAsync checks the overrides, starts an
 * FN2CTranslateBlueprintCall on the game thread and returns its future; progress and cancellation go through the
 * call context. It has no synchronous Execute (inside a JSON-RPC batch the tool manager refuses it).
 */
class FN2CMcpTranslateBlueprintTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments) override;

    /** Tests only: calls send their request through InStarter instead of translating the focused Blueprint; unset restores it */
    static void SetRequestStarterForTesting(FN2CTranslateBlueprintCall::FRequestStarter InStarter);
};
