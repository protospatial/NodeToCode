// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "Templates/ValueOrError.h"

/**
 * What the MCP server serves (decision 0005, card 0165): the toolsets whose tools it serves, and how tools/list shows
 * them. Read from Project Settings (Tool Profile, Custom Toolsets, Tool Exposure) when the server starts.
 */
struct NODETOCODE_API FN2CMcpToolConfig
{
	/** Names of the enabled toolsets (N2CMcpToolsets, case-insensitive) */
	TArray<FString> Toolsets;

	/** ListAllTools: tools/list holds every served tool. ToolSearch: only list_toolsets, describe_toolset, call_tool */
	EN2CMcpToolExposure Exposure = EN2CMcpToolExposure::ListAllTools;

	/**
	 * Leave out the tools Epic's Unreal MCP also has (N2CMcpEpicOverlap::Tools), so they are neither listed nor
	 * callable, in either exposure. Set from N2CMcpEpicOverlap::Decide when the server starts (card 0174).
	 */
	bool bLeaveOverlappingToUnrealMcp = false;

	/**
	 * The toolsets a profile enables: DefaultToolsets every toolset declared default-enabled, PythonScripting
	 * PythonTools, Custom the given names as they are.
	 */
	static TArray<FString> ToolsetsForProfile(EN2CMcpToolProfile Profile, const TArray<FString>& CustomToolsets);
};

/**
 * The MCP server's tools: the tools it serves and what tools/list shows of them.
 *
 * Configure picks the served tools from the registry by toolset and fixes tools/list for as long as the server runs
 * with that configuration, so a client sees the same tools/list for its whole session (no tools/list_changed, warm
 * prompt caches, nothing shared between clients changes underneath them). In tool-search exposure tools/list holds
 * three meta-tools named and shaped like UE 5.8's tool search: list_toolsets, describe_toolset and call_tool, which
 * reach every served tool. Calling them never changes tools/list.
 *
 * The server uses the singleton (Get); specs may build their own manager and protocol core.
 */
class NODETOCODE_API FN2CMcpToolManager
{
public:
	/** Names of the tool-search meta-tools (UE 5.8's names, ModelContextProtocolToolSearch.h) */
	static const TCHAR* const ListToolsetsName;
	static const TCHAR* const DescribeToolsetName;
	static const TCHAR* const CallToolName;

	/** Get the singleton instance */
	static FN2CMcpToolManager& Get();

	/** A manager that serves nothing until Configure or RegisterTool (the server uses Get) */
	FN2CMcpToolManager();
	~FN2CMcpToolManager() = default;

	FN2CMcpToolManager(const FN2CMcpToolManager&) = delete;
	FN2CMcpToolManager& operator=(const FN2CMcpToolManager&) = delete;

	/**
	 * Serves the tools of the registry whose toolset Config enables (a tool with no toolset is never served) and sets
	 * the exposure. Replaces whatever was served before; broadcasts OnToolListChanged when tools/list changes after an
	 * earlier Configure.
	 * @return the names in Config.Toolsets that no declared toolset has (they enable nothing)
	 */
	TArray<FString> Configure(const FN2CMcpToolConfig& Config);

	/** The configuration Configure applied last (the enabled toolsets resolved to their declared names) */
	FN2CMcpToolConfig GetConfig() const;

	/** The tools of the enabled toolsets Configure left to Unreal MCP (bLeaveOverlappingToUnrealMcp), sorted */
	TArray<FString> GetToolsLeftToUnrealMcp() const;

	/**
	 * For a tool name a client called but this server left to Unreal MCP: one sentence naming the Epic tools to use and
	 * the setting that serves it. Empty for any other name.
	 */
	FString LeftToUnrealMcpHint(const FString& ToolName) const;

	/**
	 * Serve one tool, in addition to the configured ones (specs do this for a tool their session does not serve)
	 * @param Tool The tool instance to register
	 * @return true if registration was successful
	 */
	bool RegisterTool(TSharedPtr<IN2CMcpTool> Tool);

	/**
	 * Stop serving a tool
	 * @param ToolName The name of the tool to unregister
	 * @return true if the tool was found and unregistered
	 */
	bool UnregisterTool(const FString& ToolName);

	/**
	 * The definitions tools/list returns, sorted by name (deterministic, so a client's prompt cache stays warm): every
	 * served tool, or in tool-search exposure the three meta-tools. They are the definitions cached at registration.
	 */
	TArray<FMcpToolDefinition> GetAllToolDefinitions() const;

	/**
	 * The cached definition of a served tool or meta-tool, found by name in O(1)
	 * @return The definition built when the tool was registered, or null when no tool of that name is served
	 */
	TSharedPtr<const FMcpToolDefinition> GetToolDefinition(const FString& ToolName) const;

	/**
	 * Execute a served tool or meta-tool by name (call_tool runs the tool it names)
	 * @param ToolName The name of the tool to execute
	 * @param Arguments The JSON arguments to pass to the tool
	 * @return The result of the tool execution
	 */
	FMcpToolCallResult ExecuteTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);

	/**
	 * Find a served tool or meta-tool by name
	 * @return The tool, or null when no tool of that name is served
	 */
	TSharedPtr<IN2CMcpTool> FindTool(const FString& ToolName) const;

	/** True when a tool of that name is served (or is a meta-tool) */
	bool IsToolRegistered(const FString& ToolName) const;

	/** True when tools/list holds a tool of that name: the only names a client may pass to tools/call */
	bool IsToolListed(const FString& ToolName) const;

	/** True for list_toolsets, describe_toolset and call_tool */
	static bool IsMetaTool(const FString& ToolName);

	/**
	 * list_toolsets: one line per enabled toolset that has a served tool, "- <Name>: <Description>" (UE 5.8's format)
	 */
	FString ListToolsets() const;

	/**
	 * describe_toolset: the toolset's name, description and its served tools as tools/list entries, as JSON text
	 * @return the JSON, or an error naming the valid toolsets when the name is unknown or not enabled
	 */
	TValueOrError<FString, FString> DescribeToolset(const FString& ToolsetName) const;

	/**
	 * Resolves call_tool's arguments ({toolset_name?, tool_name, arguments?}) to the served tool they name. The
	 * protocol core uses it to run a long-running tool on its own call (progress, cancellation); call_tool uses it for
	 * the rest.
	 * @param OutError the isError text when it fails (an unknown toolset names the valid ones)
	 */
	bool ResolveCallTool(const TSharedPtr<FJsonObject>& CallArguments, FString& OutToolName,
		TSharedPtr<FJsonObject>& OutArguments, FString& OutError) const;

	/** Stop serving every tool (the meta-tools stay) */
	void ClearAllTools();

	/**
	 * Broadcast when Configure changes tools/list after an earlier Configure (a server restart with new settings). The
	 * MCP protocol core turns it into a notifications/tools/list_changed for its clients; the tool manager knows
	 * nothing about transports.
	 */
	FSimpleMulticastDelegate& OnToolListChanged() { return ToolListChanged; }

private:
	/** One served tool and its definition, built once when it was registered */
	struct FRegisteredTool
	{
		TSharedPtr<IN2CMcpTool> Tool;
		TSharedPtr<const FMcpToolDefinition> Definition;
	};

	/** The served tool or meta-tool of that name (copied under the lock); an empty entry when there is none */
	FRegisteredTool FindEntry(const FString& ToolName) const;

	/** The enabled toolsets that have a served tool (call with ToolsLock held) */
	TArray<const FN2CMcpToolset*> ListedToolsetsLocked() const;

	/** The error text for a toolset name that is unknown or not enabled (call with ToolsLock held) */
	FString UnknownToolsetErrorLocked(const FString& ToolsetName) const;

	/** The served tools by name */
	TMap<FString, FRegisteredTool> RegisteredTools;

	/** list_toolsets, describe_toolset and call_tool, built once; listed only in tool-search exposure */
	TMap<FString, FRegisteredTool> MetaTools;

	/** The toolsets Configure enabled */
	TArray<const FN2CMcpToolset*> EnabledToolsets;

	/** The tools of the enabled toolsets left to Unreal MCP, and their toolsets */
	TMap<FString, const FN2CMcpToolset*> LeftToUnrealMcp;

	/** Whether the last Configure left the overlapping tools to Unreal MCP */
	bool bLeaveOverlappingToUnrealMcp = false;

	/** How tools/list shows the served tools */
	EN2CMcpToolExposure Exposure = EN2CMcpToolExposure::ListAllTools;

	/** Whether Configure ran (the first one never broadcasts) */
	bool bConfigured = false;

	/** Mutex for thread-safe access */
	mutable FCriticalSection ToolsLock;

	/** See OnToolListChanged */
	FSimpleMulticastDelegate ToolListChanged;
};
