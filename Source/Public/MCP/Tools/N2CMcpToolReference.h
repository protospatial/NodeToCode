// Copyright (c) 2026 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolTypes.h"

/**
 * The MCP tool reference (card 0179): Context/mcp-server/tools.generated.json and tools.generated.md, written by the
 * N2CToolReference commandlet (Scripts/generate_tool_reference.py) from the tool definitions and declared toolsets,
 * never by hand. It lists every registered tool with its toolset, description, arguments (from the input schema),
 * annotations, the long-running flag and whether the default configuration serves it, plus the tool-search
 * meta-tools. The text is deterministic: tools sorted by name, toolsets in declaration order, LF line endings, no
 * timestamps or machine paths, so two runs give byte-identical files. NodeToCode.MCP.ToolReference fails while the
 * committed files differ from what the registry builds.
 */
namespace N2CMcpToolReference
{
	/** File names inside the output folder */
	inline const TCHAR* const JsonFileName = TEXT("tools.generated.json");
	inline const TCHAR* const MarkdownFileName = TEXT("tools.generated.md");

	/**
	 * The tools the reference documents: every tool in FN2CMcpToolRegistry, with the definition tools/list serves
	 * (GetDefinition() plus the Play In Editor note FN2CMcpToolManager::RegisterTool adds), sorted by name. The only place the reference reads tools (the tool catalog seam, E17, repoints this one function).
	 */
	NODETOCODE_API TArray<FMcpToolDefinition> CollectTools();

	/** The tool-search meta-tools (list_toolsets, describe_toolset, call_tool) as tools/list shows them, sorted by name */
	NODETOCODE_API TArray<FMcpToolDefinition> CollectMetaTools();

	/**
	 * Whether the shipped defaults serve the tool: its toolset is default-enabled, and it is not one of the Python
	 * execution tools, which also need the per-user Allow MCP Python Execution (off by default)
	 */
	NODETOCODE_API bool IsDefaultEnabled(const FMcpToolDefinition& Definition);

	/** tools.generated.json for these tools (UTF-8 text, LF line endings, ends with a newline) */
	NODETOCODE_API FString BuildJson(const TArray<FMcpToolDefinition>& Tools, const TArray<FMcpToolDefinition>& MetaTools);

	/** tools.generated.md for these tools */
	NODETOCODE_API FString BuildMarkdown(const TArray<FMcpToolDefinition>& Tools, const TArray<FMcpToolDefinition>& MetaTools);

	/**
	 * Where the committed reference lives: <checkout>/Context/mcp-server. The checkout is the folder that holds the
	 * plugin's Source folder, followed through a junction or symbolic link (build.ps1's hosts junction Source back to
	 * the checkout), so a host editor finds the repository's files. Empty when the plugin is not found.
	 */
	NODETOCODE_API FString DefaultOutputDir();
}
