# 0005. MCP tool exposure: declared toolsets, settings profiles and an opt-in tool search

## Status

proposed (2026-10-25), provisional pending the owner's confirmation on card 0106

**Provisional:** the owner has not decided card 0106. Card 0165 applies the recommendation below so the unreachable
tools of dynamic discovery (15 of 65) and the `tools/list` that changed mid-session go away now. What the owner is
most likely to change sits in a few places: the declarations in `Source/Public/MCP/Tools/N2CMcpToolsets.h` (names,
descriptions, default-enabled), the defaults of `UN2CSettings::McpToolProfile` and `McpToolExposure`, and
`FN2CMcpToolConfig::ToolsetsForProfile` (what each profile enables). Choosing option 1 or 2 alone below is a matter of
hiding one setting; nothing else depends on both existing.

## Sources

- Kanban card 0106 "Decide how NodeToCode exposes its MCP tools: static profiles or list/describe/call meta-tools"
  (owner decision, open) and card 0165, which applies it; card 0174 (defer Epic-duplicating tools on 5.8), card 0308
  (5.8 FToolset adapter) and the Material cards of epic 0442 build on it.
- NodeToCode 2.0 review, 2026-10-22: next-12 (options), arch-arch-mcp-9 (declared toolsets), rec-ue58-toolsets-overlap-7
  and imp-mcp-tools-python-translation-resources-6 (tool search), dx-docs-release-6 and -7, mcp-transport-4,
  mcp-tools-bp-structure-15, mcp-tools-python-translation-resources-14 and -15. Evidence:
  `python -I Reviews/2026-10-22/kanban/lookup.py next-12 arch-arch-mcp-9 rec-ue58-toolsets-overlap-7 dx-docs-release-7`
  in the workspace.
- UE 5.8: `Engine/Plugins/Experimental/ModelContextProtocol/Source/ModelContextProtocolEditor/Private/ModelContextProtocolToolSearch.{h,cpp}`
  (`list_toolsets`, `describe_toolset`, `call_tool`), `ModelContextProtocolToolsetRegistryAdapter.cpp` (eager mode vs
  `UModelContextProtocolSettings::bEnableToolSearch`), `ToolsetRegistry/.../FunctionLibraryToolset.cpp`
  (`GetToolsetClassName`: toolsets are named `<Module>.<Class>`), and the EditorToolset classes (`BlueprintTools`,
  `AssetTools`, `MaterialTools`).

## Context

`FN2CMcpHttpServerManager::RegisterMcpTools` had three hand-written modes: Python script-only (7 tools), dynamic
discovery (only `assess-needed-tools`) and all tools (64, the default since Python script-only mode was turned off by
default). A tool's group was a free `Category` string typed in three places that disagreed: `assess-needed-tools`
advertised 9 hard-coded categories, the tools used 13, and `find-nodes-in-graph` had none, so in dynamic mode 15 tools
(compile, save, classes, components, all of Python, find-nodes-in-graph) could never be enabled. `assess-needed-tools`
rewrote one process-wide tool list, so a second client's tools changed underneath it, and since card 0113 the server
has no channel for `notifications/tools/list_changed`, so a client never learned the list changed at all. A list that
changes mid-session also invalidates a client's prompt cache: the tool definitions sit at the front of the prompt.

Constraints:

- **Client tool caps.** VS Code stops at 128 tools and Windsurf at 100, counted across every server the user has
  connected. NodeToCode alone lists 66; with Epic's server on 5.8, or a few other servers, a user goes over.
- **Claude Code** has its own tool search: it defers tool definitions and loads them on demand, so 66 directly listed
  tools cost it little, and directly listed tools need no extra round trip.
- **UE 5.8** has the same choice built in: an eager mode that registers every toolset's tools as MCP tools, and a
  tool-search mode (`bEnableToolSearch`) that lists only `list_toolsets`, `describe_toolset` and `call_tool`. Its
  toolsets are classes with a name and a description; a model in tool-search mode sees only those descriptions.
- **The E14 5.8 adapter** (card 0308) publishes NodeToCode into Epic's ToolsetRegistry as one `FToolset` per toolset,
  so it needs the groups declared once, with a name and a description, and every tool in one.
- **Prompt-cache stability** and multi-client use both want `tools/list` fixed for a session; MCP's 2026-07-28 draft
  moves away from per-connection tool lists as well.

Options (next-12):

1. **Static profiles.** Declared toolsets; a setting chooses which are served; `tools/list` lists their tools and is
   fixed from server start. Simple and cache-friendly, but a user over a client cap must hand-pick toolsets.
2. **Meta-tools only.** `tools/list` is always `list_toolsets`, `describe_toolset`, `call_tool` (Epic's names and
   shapes). Three tools for any cap and stable, but every call takes extra round trips, the model sees no tool schema
   until it asks, and clients with their own tool search (Claude Code) lose direct calls for nothing.
3. **Both.** Declared toolsets, a profile setting that picks the served toolsets, and an exposure setting: list every
   served tool (default) or tool search through the three meta-tools. The same shape as UE 5.8 (eager or tool search).

## Decision

Option 3, provisionally. The questions of card 0106:

1. **Profiles, meta-tools or both.** Both. **Tool Profile** (`McpToolProfile`: `DefaultToolsets`, `PythonScripting`,
   `Custom` with **Custom Toolsets**) picks the served toolsets; **Tool Exposure** (`McpToolExposure`: `ListAllTools`
   or `ToolSearch`) picks how `tools/list` shows them. `FN2CMcpToolManager::Configure` applies both once when the
   server starts; `tools/list` never changes while the server runs, in either exposure, and no tool call changes it.
   In tool search, `call_tool` runs a long-running tool on its own request (progress, cancellation), an unknown or
   disabled toolset is an `isError` naming the valid toolsets, and `tools/call` accepts only the listed names.
2. **Toolset names.** Declared once, in `N2CMcpToolsets.h`, PascalCase ending in `Tools` like Epic's toolset classes:
   `BlueprintDiscoveryTools`, `BlueprintGraphTools`, `BlueprintFunctionTools`, `BlueprintVariableTools`,
   `BlueprintTagTools`, `BlueprintCompileTools`, `BlueprintClassTools`, `BlueprintComponentTools`,
   `ContentBrowserTools`, `FileSystemTools`, `PythonTools`, `TranslationTools`. Each keeps one of the old categories,
   so no tool changed group, except `find-nodes-in-graph`, which joins `BlueprintGraphTools`. On UE 5.8 the adapter
   publishes them as `NodeToCode.<Name>`, the way Epic qualifies `<Module>.<Class>`; NodeToCode's own server uses the
   short names. The meta-tools are named exactly as Epic's (`list_toolsets`, `describe_toolset`, `call_tool`, with
   `toolset_name`, `tool_name`, `arguments`), and `list_toolsets` prints Epic's `- <Name>: <Description>` lines.
3. **Default for new installs.** Default toolsets (every toolset) with List every tool: 66 tools, 63 without Allow MCP
   Python Execution, so `get-focused-blueprint` and `translate-focused-blueprint` are reachable on a fresh install,
   Claude Code gets direct tools, and the count is under both caps on its own. Tool search is the documented answer
   for a capped client with several servers. Card 0174 turns the Epic-duplicating toolsets' default off on 5.8 through
   the declarations' default-enabled flag, which lowers the default count there.
4. **assess-needed-tools and Enable Dynamic Tool Discovery.** Both removed. `assess-needed-tools` is gone (a client
   that calls it gets "Unknown tool"; tool search replaces it). The two old switches are no longer properties; a
   config that saved them loads mapped, in memory, on every load (`UN2CSettings::MigrateLegacyMcpToolModes`):
   `bEnablePythonScriptOnlyMode=True` becomes Python scripting only, `bEnableDynamicToolDiscovery=True` becomes Tool
   search (not when script-only was also on: it took precedence and listed the Python tools directly). A saved new key
   wins. Nothing is written to the config on load; the old keys go when Project Settings next saves the section.
   `SettingsVersion` is unchanged.
5. **Materials (epic 0442 note).** Not declared yet: an empty toolset would show in `list_toolsets` with nothing in
   it and fail the "every toolset has a tool" check. The Material cards add `MaterialTools` (get-material-n2cjson,
   translate-material) to `N2CMcpToolsets.h` with their first tool, which costs one declaration.

## Consequences

- Adding a tool means naming its toolset by identity (`FMcpToolDefinition(Name, Description, N2CMcpToolsets::X)`);
  a tool with none is never served and fails `NodeToCode.Registry`. `FMcpToolDefinition::Category` is gone; `_meta`
  carries `nodetocode/toolset` instead of `nodetocode/category`.
- A smoke mode per exposure: `Tests/smoke/expected.json` counts `default` 66, `tool_search` 3, `python_only` 7.
- Clients that relied on `assess-needed-tools` must switch to tool search; it never worked with a standard client
  (no list-changed notification), so few can have.
- With Epic's MCPClientToolset on 5.8 (card 0141) NodeToCode should stay on List every tool: Epic's client lists tools
  once and would otherwise see only the meta-tools, nested behind its own `call_tool`.
- If the owner picks option 1, hide Tool Exposure and drop the meta-tools; if option 2, default Tool Exposure to Tool
  search and drop List every tool. Either way a new record supersedes this one; the declarations stay.
