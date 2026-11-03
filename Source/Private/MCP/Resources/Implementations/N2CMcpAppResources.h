// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Resources/N2CMcpResourceTypes.h"

class FJsonObject;

/**
 * NodeToCode's MCP Apps (card 0213; MCP Apps extension, spec 2026-01-26): ui:// HTML resources a client that supports
 * MCP Apps (Claude Desktop, VS Code Copilot, Cursor, ChatGPT) renders in a sandboxed iframe next to a tool result.
 * - ui://nodetocode/translation-viewer: the translation of translate-focused-blueprint, syntax-highlighted, with its
 *   declaration and implementation tabs and the implementation notes.
 * - ui://nodetocode/graph-preview: the N2C JSON of get-focused-blueprint drawn as nodes and wires.
 * A tool names its app in its tools/list definition (_meta.ui.resourceUri); the result itself is unchanged, so a
 * client without MCP Apps (Claude Code among them) shows the text and structuredContent as before. The HTML is
 * Content/MCPApps/<name>.html in the plugin, self-contained (no remote script, style or font), read on each request.
 */
struct FN2CMcpAppResources
{
	static constexpr const TCHAR* TranslationViewerUri = TEXT("ui://nodetocode/translation-viewer");
	static constexpr const TCHAR* GraphPreviewUri = TEXT("ui://nodetocode/graph-preview");
	/** The MIME type MCP Apps requires for an app's HTML */
	static constexpr const TCHAR* MimeType = TEXT("text/html;profile=mcp-app");

	/** Both apps' resource definitions, for resources/list */
	static TArray<FMcpResourceDefinition> GetDefinitions();

	/** Content/MCPApps/<name>.html of the plugin for an app URI, absolute; empty for any other URI */
	static FString HtmlPath(const FString& Uri);

	/** The app's HTML with the MCP Apps MIME type; an error (not found) for an unknown URI or a missing file */
	static FMcpResourceContents Read(const FString& RequestedUri);

	/** The tool definition _meta entry that names an app: { "ui": { "resourceUri": Uri } } */
	static TSharedRef<FJsonObject> ToolMeta(const TCHAR* Uri);
};
