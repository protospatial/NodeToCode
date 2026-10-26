# NodeToCode MCP Tools

This directory holds the MCP (Model Context Protocol) tool framework's implementation files and the tool
implementations of the NodeToCode plugin. The framework is interface-based with automatic registration, so a new
tool needs no change to existing code.

## Directory Structure

```
Source/Public/MCP/Tools/          - Framework headers
├── IN2CMcpTool.h                 - Tool interface (GetDefinition, Execute, InvokeAsync, RequiresGameThread)
├── N2CToolCatalog.h              - The tool catalog: Describe and Invoke, the seam every caller uses
├── N2CToolCallContext.h          - Progress and cancellation for one long-running call
├── N2CMcpToolBase.h              - Base class for all tools (schema and game-thread helpers)
├── N2CMcpToolRegistry.h          - Auto-registration (REGISTER_MCP_TOOL)
├── N2CMcpToolManager.h           - The registered (active) tool set and tool execution
└── N2CMcpToolTypes.h             - FMcpToolDefinition, FMcpToolCallResult

Source/Private/MCP/Tools/         - This directory
├── AGENTS.md                     - This documentation
├── N2CToolCatalog.cpp
├── N2CToolPresentation.cpp       - The presentation table: every tool's title, hints and anthropic/* _meta (card 0205)
├── N2CToolCallContext.cpp
├── N2CMcpToolBase.cpp
├── N2CMcpToolRegistry.cpp
├── N2CMcpToolManager.cpp
├── N2CMcpToolTypes.cpp
├── N2CMcpFunctionGuidUtils.h/.cpp - Function graph GUID helpers shared by the Blueprint tools
└── Implementations/              - Tool implementations by folder
    ├── Blueprint/                - Blueprint tools, one subfolder per area (AGENTS.md in each)
    │   ├── Analysis/  Classes/  Compilation/  Components/
    │   └── Functions/  Graph/  Organization/  Variables/
    ├── Translation/              - Code translation tools
    ├── FileSystem/               - File and directory reads
    ├── ContentBrowser/           - Content Browser navigation and asset operations
    └── Python/                   - run-python and the script library tools
```

`Blueprint/`, each of its subfolders, `Translation/`, `FileSystem/` and `ContentBrowser/` have their own `AGENTS.md`;
`Python/` is described in `Context/python-scripting/AGENTS-python-scripting.md`. The toolsets are declared in
`Source/Public/MCP/Tools/N2CMcpToolsets.h` and the tool-search meta-tools live in `N2CMcpToolManager.cpp` (below).

## Framework Overview

### Core Components

1. **`IN2CMcpTool`** - The interface that all MCP tools must implement
   - Defines the contract for tool definition and execution
   - `Execute` for quick tools; `InvokeAsync` (a `TFuture` plus an `FN2CToolCallContext`) for long-running ones

2. **`FN2CMcpToolBase`** - Base class providing common functionality
   - Implements helper methods for schema building
   - Provides Game Thread execution utilities
   - Handles common error scenarios

3. **`FN2CMcpToolRegistry`** - Static registry with automatic registration
   - Uses C++ static initialization for tool discovery
   - No manual registration required
   - Thread-safe tool lookup

4. **`FN2CMcpToolManager`** - Runtime tool management
   - Serves the tools of the enabled toolsets and fixes what `tools/list` returns, once at server start (below)
   - Holds the tool-search meta-tools; `ExecuteTool` runs a tool through the catalog and returns MCP content

5. **`FN2CToolCatalog`** - The one seam every caller of a tool goes through (below)
   - `Describe(Filter)` and `Invoke(Name, Arguments, Context)`, transport-neutral
   - `tools/list`, `tools/call`, `call_tool` and long-running calls (`FN2CToolAsyncTaskManager`) all use it

### The tool catalog (card 0339)

`FN2CToolCatalog` (`Source/Public/MCP/Tools/N2CToolCatalog.h`) is how anything calls a NodeToCode tool. The MCP
protocol core uses it for `tools/list` and `tools/call`; a UE 5.8 ToolsetRegistry, Python or test adapter uses the
same two calls. `FN2CToolCatalog::Get()` is the catalog over the server's tool manager; a spec builds one over its own
`FN2CMcpToolManager` (which can take its own tool source instead of the registry).

- **`Describe(Filter)`** returns `FN2CToolDescriptor`s sorted by name: `Name`, `Title`, `Description`, `Toolset`,
  `InputSchema`, `OutputSchema`, `Hints` (read-only, destructive, idempotent, open world), `ExecutionPolicy`
  (`Immediate` or `LongRunning`) and the server-side flags. `FN2CToolFilter::Listed()` is what `tools/list` holds,
  `Served()` every served tool; a filter can also keep one toolset or the read-only tools. `Find(Name)` is one
  descriptor in O(1). `tools/list` sends `ToMcpDefinition()` of each, which is exactly the definition cached at
  registration, so the tool reference and `tools/list` do not change.
- **`Invoke(Name, Arguments, Context)`** returns a `TFuture<FN2CToolOutcome>`: a JSON `Value` or a typed `Error`
  (`UnknownTool`, `InvalidArgument` with the argument in `Field`, `PlaySessionActive`, `NotRunnableHere`, `Cancelled`,
  `ToolFailed`, `Internal`), never MCP content. `ToValueString()` gives it as one plain string (UE 5.8's
  ToolsetRegistry wraps that with `MakeTextResult`). An `Immediate` tool runs before `Invoke` returns; a `LongRunning`
  one starts and sets the future later, with progress and cancellation through the `FN2CToolCallContext`.
  `InvokeNow` is for callers that need the outcome at once (a JSON-RPC batch, `call_tool`) and refuses a long-running
  tool with the batch error.
- **One pipeline** (`FN2CToolCatalog::InvokeTool`): the Play In Editor rule, then the input schema's `required`
  arguments (absent or null is missing: `INVALID_ARGUMENT: Missing required parameter: <name>`, and the tool does not
  run), then the tool by its execution policy, then the untrusted-data mark. A tool's own argument checks stay; the
  catalog only checks presence.
- **Legacy shim:** every `IN2CMcpTool` is reached with no change. `FN2CToolOutcome::FromCallToolResult` reads the value
  (`structuredContent`, else the text, parsed when it is one JSON object or array) or the error from the tool's
  `FMcpToolCallResult` and keeps that result, so `ToCallToolResult()` (the MCP adapter) sends exactly what the tool
  built. A native outcome (`MakeValue`, `MakeError`) becomes a text result, with `structuredContent` for an object.
- **Locking:** the manager's `Configure` builds the new tool set without its lock and swaps it in under it, and no
  tool code (`GetDefinition`, `Execute`, `InvokeAsync`) runs under the lock, so a tool may call `Configure` while it
  runs. A `tools/list` change goes out through `FN2CMcpToolManager::OnToolListChanged`, which the protocol core
  subscribes to. `NodeToCode.Tools.Catalog` checks all of this.

### Registration System

The `REGISTER_MCP_TOOL` macro enables automatic tool registration:

```cpp
// In your tool's .cpp file
REGISTER_MCP_TOOL(FN2CMcpYourToolName)
```

This macro:
- Creates a static initializer that runs before main()
- Adds the tool class to the global registry (`FN2CMcpToolRegistry`)
- Makes the tool available to the tool profiles; it is served when its toolset is enabled

## Tool Profiles, Exposure and Toolsets

Decision record `.agents/decisions/0005-mcp-tool-exposure-toolsets-and-tool-search.md` (proposed, card 0106; applied by
card 0165). When the MCP server starts, `FN2CMcpHttpServerManager::RegisterMcpTools()` reads three settings in Project
Settings > Plugins > Node to Code > MCP Server (`UN2CSettings`, project config `DefaultNodeToCode.ini`) and calls
`FN2CMcpToolManager::Configure` once. `tools/list` then stays the same for as long as the server runs: no tool call
changes it, every client sees the same list, and the server never sends `notifications/tools/list_changed`.

| Setting | Values | Default |
|---|---|---|
| **Tool Profile** (`McpToolProfile`) | `DefaultToolsets` (every toolset declared default-enabled), `PythonScripting` (`PythonTools` only), `Custom` (the names in Custom Toolsets) | `DefaultToolsets` |
| **Custom Toolsets** (`McpCustomToolsets`) | toolset names, offered from the declarations; an unknown name logs a warning listing the valid ones | empty |
| **Tool Exposure** (`McpToolExposure`) | `ListAllTools` (`tools/list` holds every served tool), `ToolSearch` (`tools/list` holds only `list_toolsets`, `describe_toolset` and `call_tool`) | `ListAllTools` |
| **Overlapping Tools** (`McpOverlappingTools`) | `Automatic`, `LeaveToUnrealMcp`, `AlwaysServe`: whether the 34 tools in `N2CMcpEpicOverlap::Tools` are left to Unreal MCP on UE 5.8 (below) | `Automatic` |

| Profile | Exposure | `tools/list` | count |
|---|---|---|---|
| Default toolsets (the shipped default) | List every tool | every tool, `get-focused-blueprint` and `translate-focused-blueprint` included | 66 |
| Default toolsets | Tool search | `call_tool`, `describe_toolset`, `list_toolsets` | 3 |
| Python scripting only | List every tool | `run-python`, `list-python-scripts`, `search-python-scripts`, `get-python-script`, `get-script-functions`, `save-python-script`, `delete-python-script` | 7 |

- **Python execution switch:** `FN2CMcpToolManager::RegisterTool` skips `run-python`, `save-python-script` and
  `delete-python-script` (`UN2CUserSettings::IsPythonExecutionTool`) unless the per-user **Allow MCP Python
  Execution** is on (`UN2CUserSettings::IsMcpPythonExecutionAllowed()`, default off; only the user's own Saved/Config
  value counts, card 0423). With it off (the default) the counts are 63 and 4 instead of 66 and 7, `PythonTools`
  still holds the four script library readers, and `call_tool` cannot reach the skipped tools either.
- **Restart required:** the settings are read only when the server starts (editor start, or the server restart a
  Server Port change causes); Project Settings shows a restart notice when one changes.
- **Settings saved before card 0165:** `UN2CSettings::MigrateLegacyMcpToolModes` (run from `MigrateSavedSettings` on
  every load, in memory) maps `bEnablePythonScriptOnlyMode=True` to Tool Profile = Python scripting only and
  `bEnableDynamicToolDiscovery=True` to Tool Exposure = Tool search (not when script-only mode was on too: it took
  precedence and listed the Python tools directly). A Tool Profile or Tool Exposure key already saved wins. The old
  keys stay in the file until Project Settings saves the section. `assess-needed-tools` is gone: a client that calls
  it gets "Unknown tool".
- **Tools Unreal MCP also has (card 0174):** on UE 5.8+ with the `ModelContextProtocol` and `EditorToolset` plugins
  enabled, `N2CMcpEpicOverlap::Decide` leaves the tools of `N2CMcpEpicOverlap::Tools` (`Source/Public/MCP/Tools/N2CMcpEpicOverlap.h`,
  each with its Epic counterpart) out of `Configure`: `Automatic` for the Default toolsets profile when
  `N2CMcpEpicOverlap::bLeaveToUnrealMcpByDefault` is true (owner decision 0089, provisionally option b),
  `LeaveToUnrealMcp` for every profile, `AlwaysServe` never. Never on 5.6/5.7. A new tool that Epic's EditorToolset
  also has goes in that table and in `wiki/developers/NodeToCode-and-Epic-Tool-Mapping.md` (pytest
  `test_epic_overlap_mapping.py` compares them); `NodeToCode.MCP.EpicOverlap` checks the table names registered tools.
- **Checked by:** `build.ps1 -Smoke` starts one editor per mode with Python execution allowed and compares
  `tools/list` with `Tests/smoke/expected.json` (`default` 66, `tool_search` 3, `python_only` 7). The `tool_search`
  mode is set with the old `bEnableDynamicToolDiscovery=True` key, so it also checks the migration, and it calls every
  meta-tool and checks `tools/list` is unchanged. `NodeToCode.MCP.Toolsets` and `NodeToCode.Registry` cover the same
  in specs. A card that adds or removes a tool updates those counts and the tables in this file.

### Toolsets

Every toolset is declared once, in `Source/Public/MCP/Tools/N2CMcpToolsets.h` (`N2CMcpToolsets`, with a name, a
description and whether the default profile enables it), and every tool names one by identity as the third argument
of its `FMcpToolDefinition`: `FMcpToolDefinition(TEXT("compile-blueprint"), TEXT("..."), N2CMcpToolsets::BlueprintCompile)`.
A tool with no toolset is never served (`NodeToCode.Registry` fails on one), and a toolset with no tool fails the
same spec. `tools/list` sends the toolset as `_meta["nodetocode/toolset"]`. Names are PascalCase and end in `Tools`,
like Epic's toolset classes on UE 5.8 (`BlueprintTools`, `MaterialTools`); a 5.8 registry adapter publishes them as
`NodeToCode.<Name>`. The toolsets at HEAD, all enabled by the default profile:

| Toolset | Tools | Folder |
|---|---|---|
| `BlueprintDiscoveryTools` | `get-focused-blueprint`, `get-blueprint-n2cjson`, `list-blueprint-graphs`, `export-n2cjson-batch`, `get-open-blueprint-editors`, `list-blueprint-functions`, `list-overridable-functions`, `search-blueprint-nodes` | `Blueprint/Analysis/`, `Blueprint/Graph/` |
| `BlueprintGraphTools` | `add-bp-node-to-active-graph`, `connect-pins`, `create-comment-node`, `delete-blueprint-node`, `set-input-pin-value`, `find-nodes-in-graph` | `Blueprint/Graph/` |
| `BlueprintFunctionTools` | `create-blueprint-function`, `delete-blueprint-function`, `open-blueprint-function`, `add-function-input-pin`, `add-function-return-pin`, `remove-function-entry-pin`, `remove-function-return-pin` | `Blueprint/Functions/` |
| `BlueprintVariableTools` | `create-variable`, `create-local-variable`, `search-variable-types`, `get-blueprint-member-variables`, `get-blueprint-function-local-variables`, `create-get-member-variable-node`, `create-set-member-variable-node`, `create-get-local-function-variable-node`, `create-set-local-function-variable-node`, `set-member-variable-default-value`, `set-local-function-variable-default-value` | `Blueprint/Variables/` |
| `BlueprintTagTools` | `tag-blueprint-graph`, `list-blueprint-tags`, `remove-tag-from-graph` | `Blueprint/Organization/` |
| `BlueprintCompileTools` | `compile-blueprint`, `save-blueprint` | `Blueprint/Compilation/` |
| `BlueprintClassTools` | `search-blueprint-classes`, `create-blueprint-class` | `Blueprint/Classes/` |
| `BlueprintComponentTools` | `list-components-in-actor`, `add-component-class-to-actor`, `delete-component-in-actor` | `Blueprint/Components/` |
| `ContentBrowserTools` | `open-content-browser-path`, `read-content-browser-path`, `search-content-browser`, `open-blueprint-asset`, `create-folder`, `move-folder`, `rename-folder`, `move-asset`, `copy-asset`, `rename-asset` | `ContentBrowser/` |
| `FileSystemTools` | `read-path`, `read-file` | `FileSystem/` |
| `PythonTools` | `run-python`, `list-python-scripts`, `search-python-scripts`, `get-python-script`, `get-script-functions`, `save-python-script`, `delete-python-script` | `Python/` |
| `TranslationTools` | `translate-focused-blueprint`, `translate-batch`, `get-available-llm-providers`, `get-available-translation-targets`, `get-translation-output-directory` | `Translation/` |

That is 66 tools in 12 toolsets. A new toolset is a new `inline constexpr FN2CMcpToolset` in `N2CMcpToolsets.h`, added
to `N2CMcpToolsets::All`; write its description for a model that sees nothing else of it (the domain and the kinds of
operation, 1-3 sentences, no tool names). The Material graph cards (epic 0442) add `MaterialTools` there with their
first tool.

### Tool search: list_toolsets, describe_toolset, call_tool

With Tool Exposure = Tool search, `tools/list` holds three meta-tools named and shaped like UE 5.8's tool search
(`ModelContextProtocolToolSearch.h`), built by `FN2CMcpToolManager` itself (not registry tools, no toolset), each sent
with `_meta["anthropic/alwaysLoad"]`. They reach exactly the tools the profile serves and never change `tools/list`.

| Meta-tool | Arguments | Result |
|---|---|---|
| `list_toolsets` | none | one line per enabled toolset with a served tool: `- <Name>: <Description>` (Epic's format) |
| `describe_toolset` | `toolset_name` | JSON text `{"name", "description", "tools": [<tools/list entries>]}` |
| `call_tool` | `tool_name`, optional `toolset_name`, optional `arguments` (object) | the named tool's own result |

- An unknown toolset name, or one the profile does not enable, is an `isError` result naming the valid toolsets; a
  tool outside the named toolset is an `isError` listing that toolset's tools. `call_tool` refuses the meta-tools.
- `call_tool` on a long-running tool (`translate-focused-blueprint`) runs it on the same request as a direct call
  would (`FN2CMcpProtocol` resolves it with `FN2CMcpToolManager::ResolveCallTool`): progress with the call's
  `_meta.progressToken`, `notifications/cancelled` stops it.
- `tools/call` accepts only names `tools/list` holds (`FN2CMcpToolManager::IsToolListed`): in tool search the other
  tools answer -32602 "Unknown tool" and are reached through `call_tool`.

```bash
# After the initialize in "Calling the Server with curl", with Tool Exposure = Tool search
curl -X POST http://127.0.0.1:27000/mcp -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"describe_toolset","arguments":{"toolset_name":"BlueprintCompileTools"}}}'

curl -X POST http://127.0.0.1:27000/mcp -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"call_tool","arguments":{"toolset_name":"BlueprintCompileTools","tool_name":"compile-blueprint","arguments":{}}}}'
```

## Frozen tool families (dec-5)

### Status: frozen, bug fixes only (dec-5)

Decision record [0007](../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md) (proposed; owner decision card 0140 applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b)). NodeToCode's
Blueprint and asset CRUD tools take bug fixes only: on UE 5.8 Epic's EditorToolset is the better default for CRUD
(the server leaves the 34 overlapping tools to Unreal MCP, card 0174), and on 5.6 and 5.7 these tools stay as
they are, fixed but not grown.

**What is frozen** is read from the code, not from a hand list:

1. Every tool of these toolsets, as `N2CMcpToolsets.h` declares them: `BlueprintGraphTools` (`Blueprint/Graph/`),
   `BlueprintFunctionTools` (`Blueprint/Functions/`), `BlueprintVariableTools` (`Blueprint/Variables/`),
   `BlueprintCompileTools` (`Blueprint/Compilation/`), `BlueprintClassTools` (`Blueprint/Classes/`),
   `BlueprintComponentTools` (`Blueprint/Components/`) and `ContentBrowserTools` (`ContentBrowser/`). Read-only tools
   in them are frozen too (`find-nodes-in-graph`, `search-variable-types`, `list-components-in-actor`).
2. Every tool in `N2CMcpEpicOverlap::Tools` (`Source/Public/MCP/Tools/N2CMcpEpicOverlap.h`), whatever its toolset.
   Outside the toolsets above that is `list-blueprint-functions`, `list-overridable-functions` and
   `search-blueprint-nodes` (`BlueprintDiscoveryTools`). A tool added to that table later is frozen from then on.
3. `UN2CStructUtilsLibrary` (`Source/Private/BlueprintLibraries/N2CStructUtilsLibrary.cpp`), outside MCP.

**Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except point 2:
`get-focused-blueprint`, `get-blueprint-n2cjson`, `list-blueprint-graphs`, `export-n2cjson-batch`,
`get-open-blueprint-editors`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and `PythonTools`.
Material read and translate tools (epic 0442) are comprehension or translation; a Material edit family would be new
CRUD and is out of scope.

**A bug fix** is a change that fixes a crash or hang (a modal dialog in an unattended call counts), data loss,
Blueprint or project corruption (wrong pin types, lost flags, orphaned or renamed graphs, content written outside the
project), or a result that misreports what happened (success for an edit that did not happen, wrong counts,
duplicates, a wrong type in a read). Also accepted, since they add no capability: explicit targeting arguments on
existing tools (review next-6; `blueprintPath`, `graphName` and `graphGuid`, card 0152, and card 0274 for the rest),
security hardening of what a tool may write, building on the supported engines, tests, docs that match the code,
and removing dead code.

**Not accepted:** a new tool in a frozen toolset, a new tool that Epic's EditorToolset also has, a new argument,
mode or result field that adds capability, and moving a frozen tool into an unfrozen toolset to get around this.
On UE 5.8 such work belongs to Epic's EditorToolset; on 5.6 and 5.7 it waits for the owner. New comprehension,
export or translation work goes in the unfrozen toolsets.

**Fix cards still owed in frozen families** (backlog when this was written; the disabled known-defect cases of
`N2CMcpContentTools.spec.cpp`, `N2CMcpStructureTools.spec.cpp` and `N2CMcpVariableTools.spec.cpp` name some):

| Family | Cards |
|---|---|
| `BlueprintGraphTools` | 0276 (connect-pins replace and conversion links), 0278 (set-input-pin-value verdict), 0279 (delete-blueprint-node), 0295 (notification brushes, dead code) |
| `BlueprintFunctionTools` | 0280 (create- and delete-blueprint-function touch unrelated graphs), 0295 |
| `BlueprintVariableTools` | 0283 (member defaults through the CDO), 0291 (search-variable-types), 0295, 0274 (targets) |
| `BlueprintCompileTools` | 0274 (targets); 0290 is done |
| `BlueprintClassTools` | 0288 (search-blueprint-classes duplicates and counts) |
| `BlueprintComponentTools` | 0287 (list-components-in-actor duplicates and type filter), 0274 (targets) |
| `ContentBrowserTools` | 0286 (open-blueprint-asset on unknown graphs); 0182, 0281, 0284 and 0285 are done |
| Epic-overlap tools in `BlueprintDiscoveryTools` | 0289 (list-blueprint-functions and list-overridable-functions) |
| `UN2CStructUtilsLibrary` | 0282 (unnamed fields, deleted fields) |

Card 0292 (class, soft and interface references in the variable tools) is new capability on a frozen family and
waits for the owner. Each frozen folder's `AGENTS.md` repeats its status under "Status: frozen, bug fixes only
(dec-5)".

## Long-Running Tools and Async Execution

NodeToCode runs long-running tools (LLM translation) on the game thread without blocking it and answers the client's
`tools/call` on that same POST, with an ordinary CallToolResult, once the call finishes. A long-running tool gets an
`FN2CToolCallContext` (`Source/Public/MCP/Tools/N2CToolCallContext.h`) for progress and cancellation and returns its
result as a `TFuture`; `FN2CToolAsyncTaskManager` runs the call and knows nothing about any one tool.

### How Async Tools Work

1. **Tool Declaration**: Mark a tool as long-running by setting `bIsLongRunning = true` in its definition, and
   override `InvokeAsync` instead of `Execute`
2. **Same-POST response**: the protocol core (`FN2CMcpProtocol::Handle`, which the HTTP transport's
   `FN2CMcpHttpServerManager::HandleMcpRequest` calls) starts the call and does not answer at once.
   On UE 5.8, when the request's `Accept` lists `text/event-stream` and `params._meta.progressToken` is set, the
   response is a `text/event-stream`: `notifications/progress` events (the token echoed with its JSON type, a number
   stays a number), then the JSON-RPC response with the CallToolResult, then the connection closes. Otherwise (no
   token, JSON-only Accept, or UE 5.6/5.7) the request is held and answered with one `application/json` response
   when the task finishes; no progress is sent. The 5.6/5.7 choice follows the open decision card 0085 and lives in
   `FN2CMcpHttpServerManager::CanStreamToolProgress()`. A long-running tool inside a JSON-RPC batch is refused.
3. **Progress Reporting**: Tools report progress through `Context->ReportProgress(Progress, Message)` (any thread;
   delivered on the game thread, dropped once the call has its result); the translation reports real stages
   (collecting the Blueprint, request sent, waiting with elapsed seconds, parsing) at least once per second, and each
   value sent is larger than the last, as MCP requires
4. **Cancellation**: Clients cancel a running call with the standard `notifications/cancelled` naming the
   tools/call's JSON-RPC `requestId`, in the same session. The manager (`CancelTaskByRequestId`) cancels the call's
   context; the tool's `OnCancelled` callbacks run on the game thread and stop the work (the translation cancels its
   provider HTTP request). No response is sent for a request cancelled this way: a stream closes without the
   response event, a held request ends with 202 and no body. The custom `nodetocode/cancelTask` with the call's
   progress token still works but is deprecated; there the tool's cancelled error result is the call's response.

### Creating a Long-Running Tool

1. Mark the tool as long-running in its definition (`Definition.bIsLongRunning = true`).
2. Override `InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments)`. It is
   called once per call, on the game thread, and must not block: start the work (for example an async HTTP request)
   and return a `TFuture<FMcpToolCallResult>`. Set its promise exactly once, on the game thread, with the result or a
   cancelled result. A broken promise (destroyed unset) asserts, so whatever owns the promise sets it on every path.
3. Report progress with `Context->ReportProgress`, and register `Context->OnCancelled` to stop the work. Hold your
   own state weakly in that callback: the context can outlive it.

There is no registration step and no tool-name switch: the server sees `bIsLongRunning` and the manager calls the
tool's `InvokeAsync`. `Execute` keeps its default (an error); inside a JSON-RPC batch the tool manager refuses a
long-running tool. `FN2CTranslateBlueprintCall` is the reference implementation (`FN2CTranslateBatchCall`, for
translate-batch, does the same around a whole `UN2CBatchTranslationOrchestrator` batch): it keeps itself alive until its
result is set, holds the `FN2CLLMRequestHandle` that `ProcessN2CJsonWithOverrides` returns, reports progress from an
`FTSTicker`, and calls `Cancel()` on the handle on cancel and on timeout.

```cpp
// N2CMcpMyLongTool.cpp (the class derives from FN2CMcpToolBase and declares the InvokeAsync override)
#include "N2CMcpMyLongTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSerializer.h"
#include "LLM/N2CLLMModule.h"
#include "Async/Future.h"

REGISTER_MCP_TOOL(FN2CMcpMyLongTool)

TFuture<FMcpToolCallResult> FN2CMcpMyLongTool::InvokeAsync(const FN2CToolCallContextRef& Context,
    const TSharedPtr<FJsonObject>& Arguments)
{
    check(IsInGameThread());

    struct FState
    {
        TPromise<FMcpToolCallResult> Promise;
        bool bSet = false;
        FN2CLLMRequestHandlePtr Request;
        void Set(const FMcpToolCallResult& Result) { if (!bSet) { bSet = true; Promise.SetValue(Result); } }
        ~FState() { Set(FMcpToolCallResult::CreateErrorResult(TEXT("dropped"))); }
    };
    const TSharedRef<FState> State = MakeShared<FState>();
    TFuture<FMcpToolCallResult> Future = State->Promise.GetFuture();

    FString Json;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("json"), Json))
    {
        State->Set(FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: json")));
        return Future;
    }

    // The translation is saved with the Blueprint the JSON describes (a capture's FN2CCapture::Blueprint, when you have one)
    FN2CBlueprint Captured;
    FN2CSerializer::FromJson(Json, Captured);

    Context->ReportProgress(0.1f, TEXT("Sending request..."));
    State->Request = UN2CLLMModule::Get()->ProcessN2CJson(Json, Captured, FOnLLMResponseReceived::CreateLambda(
        [State](const FString& Response)
        {
            // Runs on the game thread, at most once, and never after Request->Cancel() succeeded
            State->Set(FMcpToolCallResult::CreateTextResult(Response));
            State->Request.Reset(); // the request holds this callback, which holds State
        }));

    const TWeakPtr<FState> WeakState = State;
    const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;
    Context->OnCancelled([WeakState, WeakContext]()
    {
        const TSharedPtr<FState> Pinned = WeakState.Pin();
        const FN2CToolCallContextPtr PinnedContext = WeakContext.Pin();
        if (Pinned && PinnedContext && Pinned->Request.IsValid() && Pinned->Request->Cancel())
        {
            Pinned->Set(FMcpToolCallResult::CreateErrorResult(PinnedContext->GetCancelledMessage()));
            Pinned->Request.Reset();
        }
    });
    return Future;
}
```

### Client Usage with Progress

When calling a long-running tool, clients should provide a progress token (`$SID` is the session from
"Calling the Server with curl" below):

```bash
# Call with progress token
curl -N -X POST http://127.0.0.1:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "translate-focused-blueprint",
      "arguments": {"language": "cpp"},
      "_meta": {
        "progressToken": "unique-progress-token-123"
      }
    },
    "id": 1
  }'

# UE 5.8 (Accept lists text/event-stream and a progressToken is sent): 200, Content-Type: text/event-stream,
# one event per message; the connection closes after the response (-N prints events as they arrive):
# event: message
# data: {"jsonrpc":"2.0","method":"notifications/progress","params":{"progressToken":"unique-progress-token-123","progress":0.019999999552965164,"message":"Collecting the focused Blueprint"}}
#
# ... "Translation request sent to the LLM provider", "Waiting for the LLM response (3 s elapsed)" about once a
# second, "LLM response received; parsing the translation", then progress 1 "Translation complete" ...
#
# event: message
# data: {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"..."}]}}
#
# Accept without text/event-stream, no progressToken, or UE 5.6/5.7: the request is held, no progress is sent, and
# the call answers with one application/json response when it finishes:
# {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"..."}]}}
#
# With no Blueprint editor focused the call ends at once with "isError": true and the text
# "... Failed to get Blueprint JSON: No active Blueprint Editor." (the 5.8 stream sends progress 1, "Translation failed", first).
```

### Cancelling a Task

To cancel a running call, send `notifications/cancelled` with the tools/call's id (here 1) in the same session:

```bash
curl -X POST http://127.0.0.1:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{
    "jsonrpc": "2.0",
    "method": "notifications/cancelled",
    "params": {
      "requestId": 1,
      "reason": "User pressed Stop"
    }
  }'

# Response: 202 Accepted, no body. The tools/call's own response then ends without a JSON-RPC response.
```

The deprecated `nodetocode/cancelTask` (`"params": {"progressToken": "unique-progress-token-123"}`) still answers
`{"status":"cancellation_initiated"}`, and the call then answers with the cancelled error result.

## Creating a New MCP Tool

Follow these steps to add a new MCP tool. First check "Frozen tool families (dec-5)" above: a frozen toolset,
or a tool Epic's EditorToolset also has, takes no new tools.

### 1. Create the Tool Header File

Create a new header file in the appropriate subdirectory under `Source/Private/MCP/Tools/Implementations/`. Choose the subdirectory based on your tool's toolset:
- `Blueprint/Analysis/` - For Blueprint inspection tools
- `Blueprint/Functions/` - For function management tools
- `Blueprint/Variables/` - For variable creation tools
- `Blueprint/Graph/` - For node and graph manipulation
- `Blueprint/Organization/` - For tagging and organizing
- `Blueprint/Compilation/` - For save and compile tools
- `Blueprint/Classes/` - For Blueprint class tools
- `Blueprint/Components/` - For component management tools
- `Translation/` - For code translation tools
- `FileSystem/` - For file operations
- `ContentBrowser/` - For content browser operations
- `Python/` - For Python script tools

```cpp
// N2CMcpYourToolName.h
#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

class FN2CMcpYourToolName : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    
    // Only override if your tool needs Game Thread execution
    // virtual bool RequiresGameThread() const override { return true; }
};
```

### 2. Create the Tool Implementation

Create the corresponding .cpp file in the same subdirectory as the header file:

```cpp
// N2CMcpYourToolName.cpp
#include "N2CMcpYourToolName.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpYourToolName)

FMcpToolDefinition FN2CMcpYourToolName::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("your-tool-name"),
        TEXT("Description of what your tool does"),
        N2CMcpToolsets::BlueprintDiscovery // Its declared toolset (see Toolsets); a tool with none is never served
    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("param1"), TEXT("string"));
    Properties.Add(TEXT("param2"), TEXT("number"));
    
    TArray<FString> Required;
    Required.Add(TEXT("param1")); // Only param1 is required
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpYourToolName::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // Extract parameters
    FString Param1;
    if (!Arguments->TryGetStringField(TEXT("param1"), Param1))
    {
        return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: param1"));
    }
    
    double Param2 = 0.0;
    Arguments->TryGetNumberField(TEXT("param2"), Param2);
    
    // Your tool logic here
    FString Result = FString::Printf(TEXT("Processed %s with value %f"), *Param1, Param2);
    
    return FMcpToolCallResult::CreateTextResult(Result);
}
```

A new tool changes the mode 3 count: update `Tests/smoke/expected.json` (`default`), the tables in this file and
the list in `Context/mcp-server/AGENTS-mcp-server.md` in the same change. Any change to a tool's definition (a new,
renamed or removed tool, its description, arguments, title, hints or toolset) also means regenerating the tool
reference, `Context/mcp-server/tools.generated.json` and `tools.generated.md`: `python -I
Scripts/generate_tool_reference.py --engine <UE root> --project <host .uproject>` after building; `build.ps1 -Test`
fails while they are stale (`Tests/python/test_tool_reference.py`, `NodeToCode.MCP.ToolReference`), and so does
`python -I Scripts/docs_lint.py` (CI, on every pull request) while a doc names a tool, argument or count the reference
does not have. Renaming or removing a tool also means
updating the Agent Skills that name it (`Agents/claude-code/nodetocode/skills/`, then `python -I
Agents/sync_skills.py`); `Tests/python/test_agent_skills.py` fails while a skill names a tool that is not registered.

A tool whose result carries text from the project's Blueprints (comments, titles, names, tooltips, descriptions,
string literals) sets `Definition.bReturnsUntrustedContent = true`; the catalog's `Invoke` then marks its
successful results as untrusted data (card 0124, "Untrusted Blueprint text" in `Context/mcp-server/AGENTS-mcp-server.md`).
Add its name to the list in `Source/Private/Tests/MCP/N2CMcpUntrustedContent.spec.cpp`.

Play In Editor (card 0170, `MCP/Utils/N2CMcpPlaySessionGuard.h`): while a PIE or Simulate session runs, every tool
that is not read-only (`Annotations.IsReadOnly()`; an unset `readOnlyHint` counts as changing things) answers
`PIE_ACTIVE: ...` instead of running, through the catalog's `Invoke` (for quick tools and, through
`FN2CToolAsyncTaskManager`, long-running ones); registration adds the rule to such a tool's description. So a tool that only reads sets
`SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly())`, and a tool that writes but never changes a Blueprint,
an asset or a level (editor navigation, translation output files, the script library, run-python) sets
`Definition.bAllowedDuringPlaySession = true`. Everything else is refused during PIE, which is what a new editing tool
wants. `NodeToCode.MCP.PlaySession` checks the rule for every registered tool.

### 3. Tools Requiring Game Thread Execution

If your tool needs to access Unreal Editor APIs, it must run on the Game Thread:

```cpp
class FN2CMcpEditorTool : public FN2CMcpToolBase
{
public:
    virtual bool RequiresGameThread() const override { return true; }
    
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
    {
        // Use the base class helper for Game Thread execution
        return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
        {
            // Your editor API calls here
            // This lambda will run on the Game Thread
            return FMcpToolCallResult::CreateTextResult(TEXT("Result"));
        });
    }
};
```

### 4. Build and Test

1. Build the plugin (`.\build.ps1 -All`, see the repo `AGENTS.md`)
2. The tool registers itself when the plugin loads; it is served when the tool profile enables its toolset (the
   default profile enables all of them), and listed directly or reached through `call_tool` per Tool Exposure
3. Test with an MCP client or with curl, after the `initialize` in "Calling the Server with curl" below:

```bash
# List available tools
curl -X POST http://127.0.0.1:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'

# Call your tool
curl -X POST http://127.0.0.1:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "your-tool-name",
      "arguments": {
        "param1": "test",
        "param2": 42
      }
    },
    "id": 2
  }'
```

## Helper Functions

The `FN2CMcpToolBase` class provides several helper functions:

### `BuildInputSchema`
Creates a JSON schema for tool parameters. The optional third map gives each property its `description`; with no
properties the schema also says `"additionalProperties": false`:
```cpp
TMap<FString, FString> Properties;
Properties.Add(TEXT("message"), TEXT("string"));
Properties.Add(TEXT("count"), TEXT("number"));

TArray<FString> Required;
Required.Add(TEXT("message"));

TMap<FString, FString> Descriptions;
Descriptions.Add(TEXT("message"), TEXT("Text to log"));

Definition.InputSchema = BuildInputSchema(Properties, Required, Descriptions);
```

### `BuildEmptyObjectSchema`
For tools with no parameters (`properties` empty, `"additionalProperties": false`):
```cpp
Definition.InputSchema = BuildEmptyObjectSchema();
```

### Title and hints: the presentation table
Every tool's title and its four behaviour hints live in one table, `N2CToolPresentation.cpp` (card 0205), not in the
tool: `FN2CToolCatalog::ApplyPresentation` sets them, with `MaxResultSizeChars` and `bAlwaysLoad`, on the definition
`FN2CMcpToolManager` caches at registration and on the one `N2CMcpToolReference` collects, so `tools/list`, the
catalog, `describe_toolset` and the generated reference agree. Where a tool sets its own title or hints, the table's
win; a tool with no entry keeps its own. A new tool adds a row to the table (the comment there gives the hint rules)
and to `Expected` in `Source/Private/Tests/MCP/N2CToolPresentation.spec.cpp`; `NodeToCode.MCP.ToolPresentation`
fails on a tool without a row, a missing title or hint, a hint that breaks the rules, or a read-only hint that differs
from the one the tool sets itself (the Play In Editor rule must not change behind the tool's back). So a tool that
only reads still calls `SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly())`.

### `SetAnnotations`
Sets the four MCP behaviour hints (`readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint`), replacing
any set before. Clients use them for permission prompts and parallel calls; they are hints, not a security boundary:
```cpp
SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());       // read-only, idempotent, closed world
SetAnnotations(Definition, false, true, false, false);             // readOnly, destructive, idempotent, openWorld
```
An unset hint is not sent (the client assumes the MCP default). `Definition.Annotations.IsReadOnly()` is true only
for a tool that set `readOnlyHint` true.

### Tool metadata
`FMcpToolDefinition` (`N2CMcpToolTypes.h`) is one `tools/list` entry (MCP 2025-11-25 `Tool`): `Name`, `Title` (sent
as `title`; clients fall back to the name), `Description`, `InputSchema`, `OutputSchema` (when set, every successful
result must carry a matching `FMcpToolCallResult::StructuredContent`), `Annotations`, and `_meta`: `Toolset` is sent
as `_meta["nodetocode/toolset"]` (the toolset's name, not a top-level field), `MaxResultSizeChars` as `_meta["anthropic/maxResultSizeChars"]`,
`bAlwaysLoad` as `_meta["anthropic/alwaysLoad"]: true`, and `Meta` holds any other `_meta` entries.
`bIsLongRunning`, `bReturnsUntrustedContent` and `bAllowedDuringPlaySession` stay on the server.

`FN2CMcpToolManager::RegisterTool` builds a tool's definition once and keeps it: `tools/list`
(`GetAllToolDefinitions`, sorted by name, so the list is the same on every call), `GetToolDefinition(Name)` (O(1)) and
every `tools/call` use that copy. So `GetDefinition()` must not depend on state that changes while the server runs.

### `ExecuteOnGameThread`
Runs code on the Game Thread and waits for it, with a timeout. Called on the Game Thread it runs the logic
directly. From another thread it queues the logic and waits up to `TimeoutSeconds`; the logic and its result live
in shared state, not on the caller's stack. On timeout it returns an error result and abandons the work: if the
queued task has not started, it skips the logic when it finally runs. Abandoned is not cancelled: logic the Game
Thread had already started still runs to completion and its result is dropped, so keep the logic free of
references to the caller's locals (capture by value).
```cpp
return ExecuteOnGameThread([this]() -> FMcpToolCallResult
{
    // Code that must run on Game Thread
    return FMcpToolCallResult::CreateTextResult(TEXT("Done"));
}, 30.0f); // 30 second timeout
```

The logic runs as one tool call (`FN2CMcpToolCallScope`, card 0151): a Blueprint edit calls
`FN2CMcpBlueprintUtils::MarkModified(Blueprint, bStructural)` (or `MarkCosmeticChange` for comments and positions)
instead of compiling, and when the logic returns, after its `FScopedTransaction`s have closed, each Blueprint it
marked is compiled once and the compiler's diagnostics are added to the result as a `compile` object (or as an extra
text item when the result text is not a JSON object). Call `FN2CMcpBlueprintUtils::Compile` yourself only when the
result needs the compile's outcome in its own fields. Scopes nest, so a run-python script whose bridge calls edit
a Blueprint compiles it once, at the end of the run-python call.

## Calling the Server with curl

The server speaks MCP Streamable HTTP (2025-11-25) on one endpoint, `http://127.0.0.1:27000/mcp`, on Unreal's
HTTPServer, which listens on loopback only unless `[HTTPServer.Listeners]` in the project's engine config says
otherwise. Server Port is 27000 by default; when that port is taken the server uses the next free one in Port Fallback
Range and writes the URL it took to `<Project>/Saved/NodeToCode/mcp-server.json` and
`<UserSettingsDir>/NodeToCode/servers/<pid>.json`. There is no second port and no other MCP route (`/mcp/health` is
a plain health check).

- **POST** carries every JSON-RPC message. It needs `Content-Type: application/json` (else 415). Send
  `Accept: application/json, text/event-stream`: a request answers with `application/json`, except a long-running
  `tools/call` on UE 5.8, which may answer with `text/event-stream` (see "Long-Running Tools and Async Execution" above). Notifications
  and client responses get 202 with no body.
- **`Mcp-Session-Id`**: `initialize` returns it as a response header; every later POST must send it (none: 400, an
  unknown or ended session: 404, after which the client initializes again).
- **`MCP-Protocol-Version`**: send the negotiated version after `initialize`. An unsupported value gets 400; a missing
  header is accepted and the session's version applies.
- **GET** gets 405 with `Allow: POST, DELETE` (there is no server-to-client stream); **DELETE** with the session id
  ends the session (204) and cancels its running long-running calls.
- A body over 8 MiB gets 413. A request whose `Origin` or `Host` names a host other than `localhost`, `127.0.0.1` or
  `[::1]` (any port) gets 403; a request without `Origin` (curl, CLI agents) passes that check.

Transport and session errors come back as a JSON-RPC error with a null id, for example
`{"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"Session not found"}}`. Details: "Sessions and versions" in
`Context/mcp-server/AGENTS-mcp-server.md`.

```bash
# Open a session: keep the Mcp-Session-Id response header
SID=$(curl -s -D - -o /dev/null -X POST http://127.0.0.1:27000/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc": "2.0", "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "curl", "version": "1.0"}}, "id": 0}' \
  | tr -d '\r' | sed -n 's/^[Mm]cp-[Ss]ession-[Ii]d: //p')
curl -X POST http://127.0.0.1:27000/mcp -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "notifications/initialized"}'
# 202, no body

# See all available tools and their schemas
curl -X POST http://127.0.0.1:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'
```

The examples in this file and in the per-folder `AGENTS.md` files under `Implementations/` assume `$SID` is set this
way. End the session when done (later requests with that id get 404):

```bash
curl -X DELETE http://127.0.0.1:27000/mcp -H "Mcp-Session-Id: $SID"
```

## Best Practices

1. **Tool Naming**: Use kebab-case for tool names (e.g., `get-blueprint-n2cjson`)
2. **Error Handling**: Always validate input parameters and return clear error messages
3. **Logging**: Use `FN2CLogger` for debugging and error reporting
4. **Thread Safety**: If your tool doesn't need Editor APIs, avoid requiring Game Thread execution
5. **Documentation**: Include clear descriptions in your tool definition
6. **Input Validation**: List required parameters in the schema's `required` (the catalog refuses a call that lacks one) and check their values in the tool
7. **Tool Dependencies**: If tools work together (like search and add nodes), clearly document the workflow
8. **Toolset**: Pass the declared toolset (`N2CMcpToolsets::...`) that fits the tool; declare a new one only for a new domain

## Troubleshooting

### Tool Not Appearing in tools/list
- Check the tool profile and exposure: Python scripting only lists 7 tools (4 without Allow MCP Python Execution),
  a Custom profile serves only its toolsets, and Tool search lists only `list_toolsets`, `describe_toolset` and
  `call_tool` (the tool is in `describe_toolset` of its toolset)
- Check the tool passes a toolset to `FMcpToolDefinition` (the log warns "names no toolset, so it is never served")
- `run-python`, `save-python-script` and `delete-python-script` need the per-user Allow MCP Python Execution
- Ensure the `REGISTER_MCP_TOOL` macro is in the .cpp file
- Check that the .cpp file is included in the build
- Look for registration errors in the log
- A settings change applies after an editor restart

### Tool Execution Fails
- Check the log for error messages
- Verify input parameters match the schema
- For Game Thread tools, ensure timeout is sufficient

### Threading Issues
- Tools accessing Editor APIs must override `RequiresGameThread()` to return true
- Use `ExecuteOnGameThread` helper for safe execution
- Adjust timeout if operations take longer than expected
