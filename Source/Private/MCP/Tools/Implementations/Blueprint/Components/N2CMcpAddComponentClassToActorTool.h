// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for adding a component of a specified class to a Blueprint actor
 */
class FN2CMcpAddComponentClassToActorTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Builds a number property for JSON schema
     * @param Description The description of the property
     * @param DefaultValue The default value (0.0 if not specified)
     * @return JSON object representing the property
     */
    TSharedPtr<FJsonObject> BuildNumberProperty(const FString& Description, double DefaultValue = 0.0) const;
};