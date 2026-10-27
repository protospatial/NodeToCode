// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/IN2CMcpTool.h"

/**
 * Base class for MCP tools providing common functionality
 * Tools should inherit from this class rather than implementing IN2CMcpTool directly
 */
class NODETOCODE_API FN2CMcpToolBase : public IN2CMcpTool
{
protected:
	/**
	 * Helper to execute logic on Game Thread if needed
	 * Handles thread synchronization and timeout logic. The logic runs as one tool call (FN2CMcpToolCallScope): the
	 * Blueprints it marks modified compile once when it returns, and their diagnostics are added to the result.
	 * @param Logic The function to execute that returns an FMcpToolCallResult
	 * @param TimeoutSeconds The timeout in seconds (default: 10)
	 * @return The result from the logic function or a timeout error
	 */
	FMcpToolCallResult ExecuteOnGameThread(TFunction<FMcpToolCallResult()> Logic, float TimeoutSeconds = 10.0f);
	
	/**
	 * Build a basic input schema for tool parameters
	 * @param Properties Map of property names to their JSON schema types (e.g., "message" -> "string")
	 * @param Required Array of property names that are required
	 * @param Descriptions Optional map of property names to their descriptions (sent as each property's description)
	 * @return A shared pointer to the constructed JSON schema object; with no properties it also sets
	 *         additionalProperties:false, so a client knows the tool takes no arguments
	 */
	static TSharedPtr<FJsonObject> BuildInputSchema(
		const TMap<FString, FString>& Properties,
		const TArray<FString>& Required = {},
		const TMap<FString, FString>& Descriptions = {});
	
	/**
	 * Create a simple object schema with no properties and additionalProperties:false
	 * Useful for tools that take no input parameters
	 * @return A shared pointer to an empty object schema
	 */
	static TSharedPtr<FJsonObject> BuildEmptyObjectSchema();
	
	/**
	 * Set all four behaviour hints of a tool definition (MCP ToolAnnotations), replacing any set before
	 * @param Definition The tool definition to modify
	 * @param bReadOnly The tool does not change the editor, project or files
	 * @param bDestructive A change it makes may delete or overwrite data (meaningful when not read-only)
	 * @param bIdempotent Repeating the call with the same arguments changes nothing more (meaningful when not read-only)
	 * @param bOpenWorld It reaches outside the editor and project (an LLM, the network, arbitrary code)
	 */
	static void SetAnnotations(FMcpToolDefinition& Definition, bool bReadOnly, bool bDestructive, bool bIdempotent, bool bOpenWorld);

	/** Set the behaviour hints of a tool definition from an annotations value, for example FMcpToolAnnotations::ReadOnly() */
	static void SetAnnotations(FMcpToolDefinition& Definition, const FMcpToolAnnotations& Annotations);

	/**
	 * Strict arguments (card 0194): tools/call refuses arguments the input schema does not declare, and the schema says
	 * so ("additionalProperties": false). Call it after Definition.InputSchema is complete.
	 */
	static void SetStrictArguments(FMcpToolDefinition& Definition);
};