# Blueprint MCP Tools

This directory contains MCP tools for interacting with Blueprint assets in Unreal Engine. These tools provide comprehensive functionality for Blueprint manipulation, inspection, and organization.

## Overview

Blueprint tools are organized into the following categories:

- **[Analysis/](Analysis/)** - Tools for inspecting and analyzing Blueprint content
- **[Classes/](Classes/)** - Tools for discovering and creating Blueprint classes
- **[Compilation/](Compilation/)** - Tools for compiling and saving Blueprints
- **[Components/](Components/)** - Tools for managing actor components in Blueprints
- **[Functions/](Functions/)** - Tools for creating and managing Blueprint functions
- **[Variables/](Variables/)** - Tools for creating variables and searching variable types
- **[Graph/](Graph/)** - Tools for manipulating nodes and connections in Blueprint graphs
- **[Organization/](Organization/)** - Tools for tagging and organizing Blueprint graphs

## Common Patterns

All Blueprint tools:
- Require Game Thread execution (they interact with Unreal Editor APIs)
- Support both focused Blueprint operations and explicit Blueprint path targeting
- Include comprehensive error handling and validation
- Return detailed metadata about operations performed

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
Tools also support explicit Blueprint paths for automation:

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

1. **Blueprint Context**: Always check if a Blueprint is focused before operations
2. **Path Validation**: Validate Blueprint paths to ensure they exist and are valid
3. **Transaction Support**: Use Unreal's transaction system for undo/redo support
4. **Error Messages**: Provide clear, actionable error messages
5. **Metadata Return**: Include relevant metadata (GUIDs, paths, counts) in responses