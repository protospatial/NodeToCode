// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/IN2CMcpTool.h"

/**
 * Manager for MCP tools registration and execution
 * Singleton pattern to manage all available MCP tools
 */
class NODETOCODE_API FN2CMcpToolManager
{
public:
	/** Get the singleton instance */
	static FN2CMcpToolManager& Get();

	/** Destructor */
	~FN2CMcpToolManager() = default;

	/**
	 * Register a new tool with the manager
	 * @param Tool The tool instance to register
	 * @return true if registration was successful
	 */
	bool RegisterTool(TSharedPtr<IN2CMcpTool> Tool);

	/**
	 * Unregister a tool by name
	 * @param ToolName The name of the tool to unregister
	 * @return true if the tool was found and unregistered
	 */
	bool UnregisterTool(const FString& ToolName);

	/**
	 * Get a tool definition by name
	 * @param ToolName The name of the tool
	 * @return The tool definition, or nullptr if not found
	 */
	const FMcpToolDefinition* GetToolDefinition(const FString& ToolName) const;

	/**
	 * Get all registered tool definitions
	 * @return Array of all tool definitions
	 */
	TArray<FMcpToolDefinition> GetAllToolDefinitions() const;

	/**
	 * Execute a tool by name
	 * @param ToolName The name of the tool to execute
	 * @param Arguments The JSON arguments to pass to the tool
	 * @return The result of the tool execution
	 */
	FMcpToolCallResult ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);

	/**
	 * Find a registered tool by name
	 * @param ToolName The name of the tool
	 * @return The tool, or null when no tool of that name is registered
	 */
	TSharedPtr<IN2CMcpTool> FindTool(const FString& ToolName) const;

	/**
	 * Check if a tool is registered
	 * @param ToolName The name of the tool
	 * @return true if the tool is registered
	 */
	bool IsToolRegistered(const FString& ToolName) const;

	/**
	 * Updates the set of active tools based on a list of categories.
	 * @param Categories The list of category names to enable.
	 */
	void UpdateActiveTools(const TArray<FString>& Categories);

	/** Sets the active toolset to the default (only assess-needed-tools). */
	void SetDefaultToolSet();
	
	/** Register all available tools except assess-needed-tools */
	void RegisterAllToolsExceptAssess();

	/**
	 * Register only specific tools by name (for Python script-only mode).
	 * This clears existing tools and registers only the specified ones.
	 * @param ToolNames Array of tool names to register
	 */
	void RegisterToolsByName(const TArray<FString>& ToolNames);

	/** Clear all registered tools */
	void ClearAllTools();

	/**
	 * Broadcast after UpdateActiveTools changes the active tool set. The MCP protocol core turns it into a
	 * notifications/tools/list_changed for its clients; the tool manager knows nothing about transports.
	 */
	FSimpleMulticastDelegate& OnToolListChanged() { return ToolListChanged; }

private:
	/** Private constructor for singleton */
	FN2CMcpToolManager() = default;

	/** Prevent copying */
	FN2CMcpToolManager(const FN2CMcpToolManager&) = delete;
	FN2CMcpToolManager& operator=(const FN2CMcpToolManager&) = delete;

	/** Map of tool names to their entries */
	TMap<FString, TSharedPtr<IN2CMcpTool>> RegisteredTools;

	/** Mutex for thread-safe access */
	mutable FCriticalSection ToolsLock;

	/** See OnToolListChanged */
	FSimpleMulticastDelegate ToolListChanged;
};