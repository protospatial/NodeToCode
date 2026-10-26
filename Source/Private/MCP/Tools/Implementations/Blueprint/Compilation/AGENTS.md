# Blueprint Compilation Tools

This directory contains MCP tools for compiling and saving Blueprint assets in Unreal Engine. These tools provide programmatic access to Blueprint compilation and persistence functionality, enabling automated validation, build processes, and asset management.

## Status: frozen, bug fixes only (dec-5)

Every tool here (`BlueprintCompileTools`) is frozen. Decision record [0007](../../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b).

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in this family, and new arguments, modes or result fields that
  add capability. On UE 5.8 that work belongs to Epic's EditorToolset (`BlueprintTools.compile_blueprint`, `AssetTools.save_assets`); on 5.6 and 5.7 it waits for the
  owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.
- **Fix cards still owed here:** 0290 (save-blueprint checks files out without modal dialogs), and 0274 (explicit targets, admitted as
  next-6).

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

## Available Tools

### compile-blueprint
- **Description**: Compiles a Blueprint using the same functionality as the Compile button in the Blueprint editor
- **Parameters**:
  - `blueprintPath` (optional): Asset path of the Blueprint to compile. If not provided, uses the currently focused Blueprint
  - `forceRecompile` (optional): seeded fake argument (card 0180 check)
  - `skipGarbageCollection` (optional): Skip garbage collection during compilation for performance (default: true)
- **Returns**: Comprehensive compilation results including errors, warnings, status changes, and timing
- **Use Case**: Automated Blueprint validation, CI/CD pipelines, batch compilation

### save-blueprint
- **Description**: Saves a Blueprint asset to disk, writing the package file
- **Parameters**:
  - `blueprintPath` (optional): Asset path of the Blueprint to save. If not provided, uses the currently focused Blueprint
  - `saveOnlyIfDirty` (optional): Only save if the Blueprint has unsaved changes (default: true)
- **Returns**: Save operation results including success status, package information, and timestamp
- **Use Case**: Persisting Blueprint changes, automated save workflows, asset management

## Common Usage Patterns

### Compiling the Focused Blueprint
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Compile the currently focused Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "compile-blueprint",
      "arguments": {}
    },
    "id": 1
  }'
```

### Compiling a Specific Blueprint
```bash
# Compile a Blueprint by path
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "compile-blueprint",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter"
      }
    },
    "id": 1
  }'
```

### Performance-Optimized Compilation
```bash
# Compile with garbage collection enabled for thorough validation
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "compile-blueprint",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_GameMode",
        "skipGarbageCollection": false
      }
    },
    "id": 1
  }'
```

### Saving the Focused Blueprint
```bash
# Save the currently focused Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "save-blueprint",
      "arguments": {}
    },
    "id": 1
  }'
```

### Saving a Specific Blueprint
```bash
# Save a Blueprint by path, only if it has unsaved changes
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "save-blueprint",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter",
        "saveOnlyIfDirty": true
      }
    },
    "id": 1
  }'
```

### Force Save Blueprint
```bash
# Force save a Blueprint even if it has no changes
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "save-blueprint",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_GameMode",
        "saveOnlyIfDirty": false
      }
    },
    "id": 1
  }'
```

## Response Format

### Compile Blueprint Response

The compile-blueprint tool returns detailed compilation results:

```json
{
  "success": true,
  "blueprintName": "BP_PlayerCharacter",
  "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter",
  "compilationStatus": {
    "previousStatus": "Dirty",
    "currentStatus": "UpToDate",
    "statusCode": 3
  },
  "compilationTime": 0.125,
  "results": {
    "errorCount": 0,
    "warningCount": 1,
    "noteCount": 2,
    "messages": [
      {
        "severity": "Warning",
        "message": "Variable 'Health' is never used"
      },
      {
        "severity": "Note",
        "message": "Compilation completed successfully"
      }
    ]
  },
  "message": "Blueprint compiled successfully with 1 warning"
}
```

A message that names a node (as the editor's Compiler Results log links it) also carries `nodeGuid` (the node GUID
that get-focused-blueprint and find-nodes-in-graph report), `nodeTitle`, `graph` and, when it names a pin, `pin`;
for example an error `This blueprint (self) is not a StaticMeshComponent, therefore ' Target ' must have a
connection.` with `nodeTitle` `Set Static Mesh`, `graph` `EventGraph`, `pin` `self` (card 0151). The edit tools
return the same diagnostics in a `compile` object after the one compile each tool call runs.

### Save Blueprint Response

The save-blueprint tool returns save operation results:

```json
{
  "success": true,
  "message": "Blueprint saved successfully",
  "blueprintName": "BP_PlayerCharacter",
  "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter",
  "packageInfo": {
    "packageName": "BP_PlayerCharacter",
    "isDirty": false,
    "fileName": "/Users/username/Project/Content/Blueprints/BP_PlayerCharacter.uasset"
  },
  "timestamp": "2025-01-22 10:30:45.123"
}
```

For already saved Blueprints (when `saveOnlyIfDirty` is true):
```json
{
  "success": true,
  "message": "Blueprint is already saved (not dirty)",
  "blueprintName": "BP_PlayerCharacter",
  "blueprintPath": "/Game/Blueprints/BP_PlayerCharacter",
  "packageInfo": {
    "packageName": "BP_PlayerCharacter",
    "isDirty": false,
    "fileName": "/Users/username/Project/Content/Blueprints/BP_PlayerCharacter.uasset"
  },
  "timestamp": "2025-01-22 10:30:45.123"
}
```

### Status Values
- `Unknown`: Blueprint status is unknown
- `Dirty`: Blueprint has unsaved changes that need compilation
- `Error`: Blueprint has compilation errors
- `UpToDate`: Blueprint is compiled and error-free
- `BeingCreated`: Blueprint is currently being created
- `UpToDateWithWarnings`: Blueprint compiled with warnings

## Error Handling

### Compile Blueprint Errors

The compile-blueprint tool provides specific error codes:

- `ASSET_NOT_FOUND`: The specified Blueprint path is invalid
- `NO_ACTIVE_BLUEPRINT`: No Blueprint path provided and no Blueprint is focused
- `COMPILATION_FAILED`: A critical error occurred during compilation
- `INTERNAL_ERROR`: An unexpected exception occurred

### Save Blueprint Errors

The save-blueprint tool provides specific error codes:

- `ASSET_NOT_FOUND`: The specified Blueprint path is invalid
- `NO_ACTIVE_BLUEPRINT`: No Blueprint path provided and no Blueprint is focused
- `SAVE_FAILED`: The save operation failed (e.g., file system error, permissions)
- `INTERNAL_ERROR`: Blueprint has no package or other unexpected error

## Implementation Notes

### Compile Blueprint
- Uses `FKismetEditorUtilities::CompileBlueprint()` - the same API as the editor
- Captures all messages from `FCompilerResultsLog` for detailed diagnostics
- Automatically refreshes the Blueprint action database after compilation
- Executes on the Game Thread as required for Blueprint operations
- Supports performance optimization through garbage collection control

### Save Blueprint
- Uses `FEditorFileUtils::SavePackages()` - the standard editor save API
- Checks package dirty state to avoid unnecessary saves (configurable)
- Returns detailed package information including file system path
- Executes on the Game Thread as required for asset operations
- Shows editor notifications for user feedback

## Integration with CI/CD

These tools are ideal for continuous integration workflows:

1. **Pre-commit Validation**: Compile and save Blueprints before committing changes
2. **Automated Testing**: Verify Blueprint compilation as part of test suites
3. **Batch Processing**: Compile and save multiple Blueprints programmatically
4. **Error Reporting**: Parse JSON results for automated error tracking
5. **Asset Management**: Ensure all Blueprint changes are persisted to disk
6. **Build Automation**: Save assets after automated modifications

## Future Enhancements

Planned improvements for the compilation and save tools:

- Batch compilation and save support for multiple Blueprints
- Dependency compilation (compile all dependent Blueprints)
- Integration with Blueprint validation tools
- Progress reporting for long-running operations
- Compilation performance metrics and optimization suggestions
- Save with custom backup options
- Integration with source control for auto-commit after save
- Dirty asset detection across the entire project