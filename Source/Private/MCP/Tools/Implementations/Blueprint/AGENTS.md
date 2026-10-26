# Blueprint MCP Tools

This directory contains MCP tools for interacting with Blueprint assets in Unreal Engine. These tools provide comprehensive functionality for Blueprint manipulation, inspection, and organization.

## Status: frozen, bug fixes only (dec-5)

Most of these folders are frozen tool families. Decision record
[0007](../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied provisionally as
the review's option 2, with card 0089 applied provisionally as option (b).

| Folder | Toolset | Status |
|---|---|---|
| `Graph/` | `BlueprintGraphTools` | frozen; also `search-blueprint-nodes` (`BlueprintDiscoveryTools`, Epic overlap) |
| `Functions/` | `BlueprintFunctionTools` | frozen |
| `Variables/` | `BlueprintVariableTools` | frozen |
| `Components/` | `BlueprintComponentTools` | frozen |
| `Classes/` | `BlueprintClassTools` | frozen |
| `Compilation/` | `BlueprintCompileTools` | frozen |
| `Analysis/` | `BlueprintDiscoveryTools` | not frozen, except `list-blueprint-functions` and `list-overridable-functions` (Epic overlap) |
| `Organization/` | `BlueprintTagTools` | not frozen |

`UN2CStructUtilsLibrary` (`Source/Private/BlueprintLibraries/N2CStructUtilsLibrary.cpp`) is frozen too.

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in a frozen family or new tools Epic's EditorToolset also has, new arguments, modes
  or result fields that add capability, and moving a frozen tool into an unfrozen toolset. On UE 5.8 that work
  belongs to Epic's EditorToolset; on 5.6 and 5.7 it waits for the owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.

Each frozen folder's `AGENTS.md` lists the fix cards still owed in it; the whole policy is "Frozen tool
families" in `Source/Private/MCP/Tools/AGENTS.md`.

## Overview

Blueprint tools are organized into these folders (each tool names its toolset in `N2CMcpToolsets.h`; the table is in `Source/Private/MCP/Tools/AGENTS.md`):

- **[Analysis/](Analysis/)** - Tools for inspecting and analyzing Blueprint content
- **[Classes/](Classes/)** - Tools for discovering and creating Blueprint classes
- **[Compilation/](Compilation/)** - Tools for compiling and saving Blueprints
- **[Components/](Components/)** - Tools for managing actor components in Blueprints
- **[Functions/](Functions/)** - Tools for creating and managing Blueprint functions
- **[Variables/](Variables/)** - Tools for creating variables and searching variable types
- **[Graph/](Graph/)** - Tools for manipulating nodes and connections in Blueprint graphs
- **[Organization/](Organization/)** - Tools for tagging and organizing Blueprint graphs

## Common Patterns

Every Blueprint tool runs on the Game Thread (it calls Unreal Editor APIs).

### Targeting

Not every tool can be pointed at a Blueprint; what each takes:

| Arguments | Tools |
|---|---|
| `blueprintPath`, `graphName`, `graphGuid` | every `Graph/` and `Functions/` tool and `search-blueprint-nodes` (all through `FN2CGraphTarget::Resolve`), `get-blueprint-n2cjson` |
| `blueprintPath` only | `list-blueprint-graphs`, `list-blueprint-functions`, `list-overridable-functions`, `compile-blueprint`, `save-blueprint`, the three `Components/` tools |
| none: the focused Blueprint | every `Variables/` tool, the `Organization/` tag tools, `get-focused-blueprint`, `get-open-blueprint-editors` (card 0274 owes the explicit targets) |

`export-n2cjson-batch` and `translate-batch` take their own lists of asset paths or tags, and `create-blueprint-class`
takes the path of the asset it creates.

- `blueprintPath` is the asset path, `/Game/Blueprints/BP_Door.BP_Door` or `/Game/Blueprints/BP_Door`; for the
  `FN2CGraphTarget` tools a path that loads no Blueprint is `ASSET_NOT_FOUND`.
- `graphName` names any graph of the Blueprint, ignoring case (event graph, function, macro, collapsed graph,
  delegate signature, interface graph); `graphGuid` is its `UEdGraph::GraphGuid` (32 hex digits or hyphenated; a
  function's `functionGuid`) and wins when both are given. A graph GUID is unique only inside one Blueprint, so an
  agent working without the editor's focus passes `blueprintPath` with `graphGuid`.
- With none of the three the tool uses the focused graph; with `graphName` or `graphGuid` alone, that graph of the
  focused Blueprint; with `blueprintPath` alone, its focused graph when one of its graphs is focused, else its event
  graph. Writes to a read-only graph are refused (`READ_ONLY_GRAPH`).
- Results of the `Graph/` and `Functions/` tools and `search-blueprint-nodes` carry `target`: `blueprintPath`, `blueprintName`, `graphName`, `graphGuid`,
  `graphKind` and `resolvedBy`, which says how the graph was chosen: `explicit` (named by `graphName` or
  `graphGuid`), `focus` (the focused graph) or `eventGraph` (`blueprintPath` alone). Error codes and details:
  "Targeting and IDs" in `Graph/AGENTS.md`.

### Compile, undo and save

- **Compile:** no tool takes a compile argument. An edit marks the Blueprint modified, and the tool call compiles
  each Blueprint it marked once, after its transactions close (card 0151), adding the compiler's diagnostics to
  the result as a `compile` object (`status`, `success`, `errorCount`, `warningCount`, `messages`, ...; fields in
  `Graph/AGENTS.md`). A failed compile does not make the call an error. `create-comment-node` only dirties the
  package, and read tools compile nothing. `compile-blueprint` compiles on request and reports in its own fields.
- **Undo:** not every operation is an undo step. Each folder's `AGENTS.md` says which tools open a transaction;
  creating an asset (`create-blueprint-class`) and the Content Browser operations are not undoable, and undoing
  `set-member-variable-default-value` misses the class default object until card 0283.
- **Save:** nothing is saved to disk except by `save-blueprint`.

## Integration with NodeToCode

These tools integrate with NodeToCode's core functionality:
- Use `FN2CBlueprintEditorTracker` (`Core/N2CBlueprintEditorTracker.h`) for the focused Blueprint editor and for the
  editor of a given Blueprint (`GetEditorFor`); `FN2CEditorIntegration::GetActiveBlueprintEditor` and
  `GetFocusedGraphFromActiveEditor` forward to it
- Leverage `FN2CNodeCollector` and the capture module (`Core/N2CCapture.h`: `N2C::CaptureNodes`, `N2C::DescribeNode`) for Blueprint serialization
- Support the N2CJSON format for Blueprint representation

## Examples

### Which Blueprint is focused
The focused Blueprint is the Blueprint editor last activated (card 0150, `FN2CBlueprintEditorTracker`):
- the user brought its asset tab to the front or clicked inside it (a graph, Details, My Blueprint): the global tab
  manager's `OnTabForegrounded` and `OnActiveTabChanged`;
- it was opened (by the user, the Content Browser, `open-blueprint-asset`, `open-blueprint-function`, the Python
  bridge's `open_blueprint`), several at once included (`UAssetEditorSubsystem::OnAssetOpenedInEditor`);
  `FN2CMcpBlueprintUtils::OpenBlueprintEditor` always returns and focuses the requested Blueprint's own editor;
- a NodeToCode overlay button or toolbar command was used in it.

Activating anything else (the Content Browser, the level editor, the NodeToCode window) leaves the focus where it was,
and a tab in an editor whose asset tab is behind another one (a background rebuild) never takes it; the overlay
injector does not change it either. When the focused editor closes, the focus goes to the Blueprint whose asset tab
comes to the front, or else to the most recently activated Blueprint editor still open. The graph is the one that
editor shows. Checked by `NodeToCode.Editor.Focus` (UE 5.8; 5.6 and 5.7 cannot close a headless Blueprint editor).

### Working with the Focused Blueprint
Many tools can operate on the currently focused Blueprint without requiring an explicit path:

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Get the focused Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get-focused-blueprint",
      "arguments": {}
    },
    "id": 1
  }'
```

### Specifying a Blueprint Path
The tools in the first two rows of the targeting table also take an explicit Blueprint path:

```bash
# List functions in a specific Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "list-blueprint-functions",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter"
      }
    },
    "id": 1
  }'
```

## Best Practices

1. **Blueprint Context**: Resolve the target with `FN2CGraphTarget` (explicit arguments, else the focused graph)
   instead of assuming a focused editor
2. **Path Validation**: Validate Blueprint paths to ensure they exist and are valid
3. **Transaction Support**: Wrap a Blueprint edit in an `FScopedTransaction` so it is one undo step (not every
   existing tool does: see "Compile, undo and save" above)
4. **Error Messages**: Provide clear, actionable error messages
5. **Metadata Return**: Include relevant metadata (GUIDs, paths, counts) in responses