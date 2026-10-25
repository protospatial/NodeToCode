// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for creating a Set node for a local function variable in a Blueprint graph.
 * This creates a K2Node_VariableSet node that sets the value of a local variable at runtime.
 * Optionally sets the input pin's value on the created node.
 */
class FN2CMcpCreateSetLocalFunctionVariableNode : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }
    
};