// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool that returns the list of available programming languages that NodeToCode can translate Blueprints into.
 * It lists the rows of the target-language table (Core/N2CTargetLanguage.h) in a format suitable for AI assistants.
 */
class FN2CMcpGetAvailableTranslationTargetsTool : public FN2CMcpToolBase
{
public:
    // IN2CMcpTool interface
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    
    // This tool doesn't require Game Thread since it only accesses static enum data and settings
    virtual bool RequiresGameThread() const override { return false; }

};