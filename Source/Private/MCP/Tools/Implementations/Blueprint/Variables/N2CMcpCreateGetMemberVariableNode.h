// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"


/**
 * MCP tool for creating a Get node for a member variable in the focused blueprint graph.
 * 
 * This tool creates a K2Node_VariableGet node that can be used to read a value from
 * a member variable in a Blueprint. The created node will have:
 * - An output pin for the variable's value (matching the variable's type)
 * 
 * Unlike Set nodes, Get nodes do not have execution pins unless they are used
 * in a pure function context where they might affect execution flow.
 * 
 * Example usage:
 * {
 *   "variableName": "Health",
 *   "location": { "x": 400, "y": 200 }
 * }
 * 
 * The returned nodeId is the node GUID: connect-pins wires the node with it (card 0155).
 */
class FN2CMcpCreateGetMemberVariableNode : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

};