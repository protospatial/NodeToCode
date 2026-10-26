// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for listing the files and folders directly inside a directory of the project (or of the translation output
 * directory when it is outside the project). Paths go through FN2CPathUtils::ResolveAgentReadPath; the listing comes
 * in pages (pageSize, cursor) and can be filtered by a wildcard.
 */
class FN2CMcpReadPathTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    
    // This tool needs to access file system operations
    virtual bool RequiresGameThread() const override { return true; }

    /** Entries per page when pageSize is left out */
    static constexpr int32 DefaultPageSize = 200;

    /** The largest pageSize a client may ask for */
    static constexpr int32 MaxPageSize = 1000;
};