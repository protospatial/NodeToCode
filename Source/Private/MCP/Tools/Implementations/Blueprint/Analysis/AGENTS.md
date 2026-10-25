# Blueprint Analysis Tools

This directory contains MCP tools for inspecting and analyzing Blueprint assets. These tools provide read-only operations to gather information about Blueprints without modifying them.

## Available Tools

### get-focused-blueprint
- **Description**: Collects and serializes the currently focused Blueprint graph into N2CJSON format
- **Parameters**: None
- **Returns**: Complete N2CJSON representation of the focused Blueprint
- **Use Case**: Primary tool for extracting Blueprint data for LLM translation

### list-blueprint-functions
- **Description**: Lists all functions defined in a Blueprint with their parameters and metadata
- **Parameters**:
  - `blueprintPath` (optional): Asset path of the Blueprint
  - `includeInherited` (optional): Include inherited functions from parent classes
  - `includeOverridden` (optional): Include overridden parent functions
- **Returns**: Detailed function information including parameters, flags, and graph metadata
- **Use Case**: Understanding Blueprint structure and available functions

### list-overridable-functions
- **Description**: Lists all functions that can be overridden from parent classes and interfaces
- **Parameters**:
  - `blueprintPath` (optional): Asset path of the Blueprint
  - `includeImplemented` (optional): Include already implemented functions
  - `filterByCategory` (optional): Filter by function category
  - `searchTerm` (optional): Search function names
- **Returns**: Overridable functions with source information and implementation status
- **Use Case**: Discovering which parent/interface functions can be implemented

### get-open-blueprint-editors
- **Description**: Returns a list of all currently open Blueprint editors
- **Parameters**: None
- **Returns**: List of open editors with asset paths, graph information, and editor state
- **Use Case**: Managing multiple Blueprint editors and understanding workspace state

## Common Usage Patterns

### Analyzing Blueprint Structure
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# First, get the focused Blueprint data
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call", 
       "params": {"name": "get-focused-blueprint", "arguments": {}}, "id": 1}'

# Then list its functions
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call", 
       "params": {"name": "list-blueprint-functions", 
                  "arguments": {"includeInherited": true}}, "id": 2}'
```

### Finding Functions to Override
```bash
# Search for input-related functions that can be overridden
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call", 
       "params": {"name": "list-overridable-functions", 
                  "arguments": {"filterByCategory": "Input", 
                               "includeImplemented": false}}, "id": 1}'
```

## Implementation Notes

- All tools use `FN2CEditorIntegration` to access Blueprint editor context
- The N2CJSON format provides a complete representation of Blueprint structure
- Function metadata includes GUIDs for precise function identification
- Tools handle both Actor and non-Actor Blueprint types appropriately