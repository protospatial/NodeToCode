// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for setting the default value of a local function variable.
 * This modifies the FBPVariableDescription stored in the function entry node,
 * similar to editing the default value in the Blueprint editor's details panel.
 */
class FN2CMcpSetLocalFunctionVariableDefaultValue : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }
};