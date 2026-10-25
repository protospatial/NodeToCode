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
	 * @return A shared pointer to the constructed JSON schema object
	 */
	static TSharedPtr<FJsonObject> BuildInputSchema(
		const TMap<FString, FString>& Properties,
		const TArray<FString>& Required = {});
	
	/**
	 * Create a simple object schema with no properties
	 * Useful for tools that take no input parameters
	 * @return A shared pointer to an empty object schema
	 */
	static TSharedPtr<FJsonObject> BuildEmptyObjectSchema();
	
	/**
	 * Add read-only annotation to a tool definition
	 * @param Definition The tool definition to modify
	 */
	static void AddReadOnlyAnnotation(FMcpToolDefinition& Definition);
};