// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraphPin.h"

// Forward declarations
class UK2Node_FunctionResult;
class UEdGraph;
class UBlueprint;

/**
 * MCP tool that adds a new return value (output pin) to the currently focused Blueprint function.
 * This modifies the function signature by adding a new input pin to the UK2Node_FunctionResult node.
 * 
 * Key differences from AddFunctionInputPin:
 * - Works with UK2Node_FunctionResult instead of UK2Node_FunctionEntry
 * - Creates EGPD_Input pins (inputs to result node are outputs from function)
 * - May need to create a FunctionResult node if function is currently void
 * - No default values for return pins
 */
class FN2CMcpAddFunctionReturnPinTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }
};