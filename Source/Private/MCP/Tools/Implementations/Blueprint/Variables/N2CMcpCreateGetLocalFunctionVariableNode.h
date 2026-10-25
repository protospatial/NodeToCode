// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for creating a Get node for a local function variable in a Blueprint graph.
 * This creates a K2Node_VariableGet node that reads the value of a local variable at runtime.
 * 
 * The tool requires the currently focused graph to be a function graph (not an event graph
 * or construction script). The created node will have an output pin matching the variable's type.
 * 
 * Example usage:
 * {
 *   "variableName": "TempCounter",
 *   "x": 200,
 *   "y": 150
 * }
 * 
 * The returned nodeId is the node GUID: connect-pins wires the node with it (card 0155).
 */
class FN2CMcpCreateGetLocalFunctionVariableNode : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }
    
};