// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for reading text files in the project folder (and the translation output directory when it is outside the
 * project). Paths go through FN2CPathUtils::ResolveAgentReadPath; only text extensions are served, binary content is
 * refused, and a call returns at most MaxContentBytes of whole lines (offset and limit select a line range).
 */
class FN2CMcpReadFileTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    
    // This tool needs to access file system operations
    virtual bool RequiresGameThread() const override { return true; }
    
private:
    /** The most UTF-8 bytes of content one call returns (500 KB); a larger file is read in parts */
    static constexpr int64 MaxContentBytes = 500 * 1024;
};