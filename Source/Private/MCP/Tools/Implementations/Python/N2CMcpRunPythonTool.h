// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool for executing Python scripts in Unreal Engine's Python environment.
 *
 * This tool provides access to the nodetocode Python module for Blueprint manipulation.
 * Scripts can set a 'result' variable to return structured data to the caller.
 *
 * Example usage:
 *   import nodetocode as n2c
 *   bp = n2c.get_focused_blueprint()
 *   result = {"blueprint": bp}
 */
class FN2CMcpRunPythonTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;

	// Python execution needs Game Thread for UE API access
	virtual bool RequiresGameThread() const override { return true; }

private:
	/** Runs the code and builds the answer; Execute adds the audit line (card 0424) */
	FMcpToolCallResult RunAndAnswer(const TSharedPtr<FJsonObject>& Arguments);

	/** Default timeout in seconds for Python execution */
	static constexpr float DefaultTimeoutSeconds = 60.0f;

	/** Maximum allowed timeout in seconds */
	static constexpr float MaxTimeoutSeconds = 300.0f;
};
