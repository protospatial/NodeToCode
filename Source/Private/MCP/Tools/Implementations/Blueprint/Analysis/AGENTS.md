# Blueprint Analysis Tools

This directory contains MCP tools for inspecting and analyzing Blueprint assets. These tools provide read-only operations to gather information about Blueprints without modifying them.

## Available Tools

### get-focused-blueprint
- **Description**: Collects and serializes the currently focused Blueprint graph into N2CJSON format
- **Parameters**: None
- **Returns**: Complete N2CJSON representation of the focused Blueprint, where each node's and pin's `id` is
  `ids` (`{"short": "N1", "guid": "<NodeGuid>"}`; a pin's also has its `name`, and `guid` is its `PinId`), the flows
  keep the short IDs, and a top-level `guids` table lists every captured graph's `GraphGuid` (in `graphs` order), every
  node ID with its `NodeGuid` and every pin ID with `{"node": "<node ID>", "guid": "<PinId>"}`. A pin's `PinId` is only
  unique on its node (a pasted node keeps its pins' IDs), so address a pin by its node GUID and pin GUID together.
  Short IDs are unique across the capture and given in graph order, so an unchanged graph gets the same IDs each time.
  The document is N2C JSON 2.0.0: `flows.data` maps each output pin ("N1.P2") to the input pin it feeds, or to an
  array of input pins when it feeds several, and every connected data input appears there once. `flows.execution`
  has one entry per exec wire, from the exec output pin to the exec input pin it runs ("N3.P5->N4.P1"), so the
  source pin says which output fired (Branch True/False, a Switch case, Sequence then_0/then_1, Loop Body/Completed)
  and the target pin which input runs (Gate Enter/Open/Close, Timeline Play/Reverse). Nodes Blueprint never runs
  carry `"unused": true` (pure, nothing live reads it), `"unreachable": true` (impure, no entry reaches it) or
  `"disabled": true` (not compiled; exec passes through one with one exec input and one exec output). Unlike a
  translation, this tool (and find-nodes-in-graph) keeps the editor's unwired default events (ghost BeginPlay,
  ActorBeginOverlap, Tick) as `"disabled": true` nodes (`FN2CCaptureOptions::bIncludeGhostNodes`), so an agent wires
  the existing event instead of adding a duplicate (card 0137).
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