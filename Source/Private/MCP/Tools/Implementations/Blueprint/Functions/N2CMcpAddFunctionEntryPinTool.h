// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraphPin.h"

// Forward declarations
class UK2Node_FunctionEntry;
class UEdGraph;
class UBlueprint;

/**
 * MCP tool that adds a new input parameter (pin) to the currently focused Blueprint function.
 * This modifies the function signature by adding a new output pin to the UK2Node_FunctionEntry node.
 */
class FN2CMcpAddFunctionEntryPinTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }
};