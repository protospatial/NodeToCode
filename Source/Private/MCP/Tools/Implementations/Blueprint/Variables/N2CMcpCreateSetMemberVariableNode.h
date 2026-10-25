// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"


/**
 * MCP tool for creating a Set node for a member variable in the focused blueprint graph.
 * 
 * This tool creates a K2Node_VariableSet node that can be used to assign a value to
 * a member variable in a Blueprint. The created node will have:
 * - An execution input pin (when called from an execution context)
 * - An execution output pin (when called from an execution context)
 * - An input pin for the value to set (matching the variable's type)
 * - An output pin for the variable's value (same as the input)
 * 
 * Example usage:
 * {
 *   "variableName": "Health",
 *   "defaultValue": "100.0",
 *   "location": { "x": 400, "y": 200 }
 * }
 * 
 * The returned nodeId is the node GUID: connect-pins wires the node with it (card 0155).
 */
class FN2CMcpCreateSetMemberVariableNode : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

};