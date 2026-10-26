// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * export-n2cjson-batch (card 0169): many graphs as N2C JSON in one call, one file per graph, by asset path or by tag,
 * with no Blueprint editor open. Each file holds the document get-blueprint-n2cjson returns for that graph; the files
 * go to one folder under Saved/NodeToCode/Exports and the result lists them with the targets that failed.
 */
class FN2CMcpExportN2CJsonBatchTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/** Graphs one call may export */
	static constexpr int32 MaxGraphs = 200;

	/**
	 * Checks outDir: a relative folder under the export folder, one to four parts of letters, digits, '_', '-' and '.'
	 * (no '.' or '..' part). Returns the folder, or "" with OutError (INVALID_ARGUMENT) when it is not one.
	 */
	static FString ValidateOutDir(const FString& OutDir, FString& OutError);
};
