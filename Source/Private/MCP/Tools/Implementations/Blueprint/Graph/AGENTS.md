# Blueprint Graph Manipulation Tools

This directory contains MCP tools for manipulating nodes and connections within Blueprint graphs. These tools enable programmatic construction of Blueprint logic.

## Available Tools

### search-blueprint-nodes
- **Description**: Searches for Blueprint nodes/actions relevant to a query
- **Parameters**:
  - `searchTerm` (required): Text query to search for
  - `contextSensitive` (optional): Perform context-sensitive search
  - `maxResults` (optional): Maximum results to return (1-100)
  - `blueprintContext` (optional): Context for sensitive search
- **Returns**: Array of nodes with spawn metadata
- **Use Case**: Discovering available nodes before adding them

### add-bp-node-to-active-graph
- **Description**: Adds a Blueprint node to the currently active graph
- **Parameters**:
  - `nodeName` (required): Name of the node to add
  - `actionIdentifier` (required): Identifier from search results
  - `location` (optional): X,Y coordinates for placement
- **Returns**: Node ID, graph name, and success status
- **Use Case**: Building Blueprint logic programmatically

### connect-pins
- **Description**: Connect pins between Blueprint nodes using GUIDs
- **Parameters**:
  - `connections` (required): Array of connection specifications
  - `options` (optional): Transaction name and link breaking behavior
- **Returns**: Succeeded/failed connections with details
- **Use Case**: Wiring up Blueprint node networks

### set-input-pin-value
- **Description**: Sets the default value of an input pin on a Blueprint node
- **Parameters**:
  - `nodeGuid` (required): GUID of the node containing the pin
  - `pinGuid` (required): GUID of the pin to set
  - `pinName` (optional): Pin name for fallback lookup
  - `value` (required): The value to set (as string)
- **Returns**: Pin information and old/new values
- **Use Case**: Setting default values on node inputs

### delete-blueprint-node
- **Description**: Deletes one or more Blueprint nodes from the focused graph
- **Parameters**:
  - `nodeGuids` (required): Array of node GUIDs to delete
  - `preserveConnections` (optional): Attempt to bridge connections
  - `force` (optional): Skip validation and force deletion
- **Returns**: Deleted nodes info and preserved connections
- **Use Case**: Removing nodes while optionally maintaining flow

### create-comment-node
- **Description**: Creates a comment node around specified Blueprint nodes using their GUIDs
- **Parameters**:
  - `nodeGuids` (required): Array of node GUIDs to include in the comment
  - `commentText` (optional): Text for the comment (default: "Comment")
  - `color` (optional): RGB color object with r, g, b values 0-1 (default: white)
  - `fontSize` (optional): Font size 1-1000 (default: 18)
  - `moveMode` (optional): "group" or "none" (default: "group")
  - `padding` (optional): Extra padding around nodes (default: 50)
- **Returns**: Comment node info, included nodes, and any missing GUIDs
- **Use Case**: Organizing and documenting Blueprint logic with visual grouping

### find-nodes-in-graph
- **Description**: Searches for specific nodes in the focused Blueprint graph by keywords or node GUIDs
- **Parameters**:
  - `searchTerms` (required): Array of keywords or GUIDs to search for
  - `searchType` (optional): "keyword" or "guid" (default: "keyword")
  - `caseSensitive` (optional): Whether keyword search is case-sensitive (default: false)
  - `maxResults` (optional): Maximum nodes to return 1-200 (default: 50)
- **Returns**: Matching nodes in N2C JSON format with full GUID information
- **Use Case**: Quickly locate specific nodes in large graphs without fetching entire blueprint

## Node Search and Add Workflow

The search and add tools work together in a two-step process:

### Step 1: Search for Nodes
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-nodes",
      "arguments": {
        "searchTerm": "print string",
        "contextSensitive": true
      }
    },
    "id": 1
  }'
```

### Step 2: Add Found Node
```bash
# Use the actionIdentifier from search results
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-bp-node-to-active-graph",
      "arguments": {
        "nodeName": "Print String",
        "actionIdentifier": "printstring>development>|utilities>printstring>development>|utilities",
        "location": {"x": 400, "y": 200}
      }
    },
    "id": 2
  }'
```

## Pin Connection System

### Connection Structure
Each connection requires:
- `from`: Source pin specification
  - `nodeGuid`: GUID of source node
  - `pinGuid`: GUID of source pin
  - `pinName` (optional): Fallback name
  - `pinDirection` (optional): Validation
- `to`: Target pin specification (same structure)

### Connection Rules
- **Execution Pins**: One-to-one connections only
- **Data Output Pins**: Can connect to multiple inputs
- **Data Input Pins**: Can only have one connection

### Batch Connection Example
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "connect-pins",
      "arguments": {
        "connections": [
          {
            "from": {
              "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
              "pinGuid": "BBE5F1A04B2E8F9E003C6B8F12345678"
            },
            "to": {
              "nodeGuid": "CCE5F1A04B2E8F9E003C6B8F12345678",
              "pinGuid": "DDE5F1A04B2E8F9E003C6B8F12345678"
            }
          }
        ],
        "options": {
          "transactionName": "Wire up print logic",
          "breakExistingLinks": true
        }
      }
    },
    "id": 1
  }'
```

## Setting Pin Values

### Set String Value Example
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
        "pinGuid": "BBE5F1A04B2E8F9E003C6B8F12345678",
        "value": "Hello, World!"
      }
    },
    "id": 1
  }'
```

### Set Numeric Value Example
```bash
# Integer value
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
        "pinGuid": "CCE5F1A04B2E8F9E003C6B8F12345678",
        "value": "42"
      }
    },
    "id": 2
  }'

# Float value
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
        "pinGuid": "DDE5F1A04B2E8F9E003C6B8F12345678",
        "value": "3.14159"
      }
    },
    "id": 3
  }'
```

### Set Boolean Value Example
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "AAE5F1A04B2E8F9E003C6B8F12345678",
        "pinGuid": "EEE5F1A04B2E8F9E003C6B8F12345678",
        "value": "true"
      }
    },
    "id": 4
  }'
```

### Pin Value Rules
- Only input pins can have default values set
- Connected pins ignore default values (disconnect first)
- Execution pins cannot have default values
- Container pins (arrays, sets, maps) cannot have inline defaults
- Reference pins (by-ref parameters) typically cannot have defaults

## Node Deletion

### Delete Single Node Example
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-blueprint-node",
      "arguments": {
        "nodeGuids": ["AAE5F1A04B2E8F9E003C6B8F12345678"]
      }
    },
    "id": 1
  }'
```

### Delete Multiple Nodes Example
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-blueprint-node",
      "arguments": {
        "nodeGuids": [
          "AAE5F1A04B2E8F9E003C6B8F12345678",
          "BBE5F1A04B2E8F9E003C6B8F12345679",
          "CCE5F1A04B2E8F9E003C6B8F12345680"
        ]
      }
    },
    "id": 1
  }'
```

### Delete with Connection Preservation
When deleting nodes in the middle of a flow, preserve connections:
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-blueprint-node",
      "arguments": {
        "nodeGuids": ["AAE5F1A04B2E8F9E003C6B8F12345678"],
        "preserveConnections": true
      }
    },
    "id": 1
  }'
```

### Force Delete Protected Nodes
Override protection for system nodes (use with caution):
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-blueprint-node",
      "arguments": {
        "nodeGuids": ["AAE5F1A04B2E8F9E003C6B8F12345678"],
        "force": true
      }
    },
    "id": 1
  }'
```

### Node Deletion Rules
- **Protected Nodes**: Function entry/result nodes cannot be deleted without force
- **Connection Preservation**: Only works for compatible pin types
- **Batch Operations**: All deletions occur in a single transaction
- **Undo Support**: Full undo/redo support via Unreal's transaction system

## Comment Node Creation

### Create Comment with Default Settings
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-comment-node",
      "arguments": {
        "nodeGuids": [
          "AAE5F1A04B2E8F9E003C6B8F12345678",
          "BBE5F1A04B2E8F9E003C6B8F12345679"
        ]
      }
    },
    "id": 1
  }'
```

### Create Colored Comment with Custom Text
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-comment-node",
      "arguments": {
        "nodeGuids": [
          "AAE5F1A04B2E8F9E003C6B8F12345678",
          "BBE5F1A04B2E8F9E003C6B8F12345679",
          "CCE5F1A04B2E8F9E003C6B8F12345680"
        ],
        "commentText": "Player Input Handling",
        "color": {
          "r": 0.2,
          "g": 0.8,
          "b": 0.2
        },
        "fontSize": 24,
        "moveMode": "group"
      }
    },
    "id": 1
  }'
```

### Comment Node Workflow
1. Call `get-focused-blueprint` to get node GUIDs and understand graph structure
2. Identify nodes to group by their GUIDs
3. Create comment node with desired settings
4. Comment automatically sizes and positions around selected nodes

### Comment Node Features
- **Group Movement**: When `moveMode` is "group", moving the comment moves all contained nodes
- **No Group Movement**: When `moveMode` is "none", comment acts as visual annotation only
- **Auto-sizing**: Comment automatically calculates bounds to encompass all specified nodes
- **Color Coding**: Use colors to categorize different logic sections
- **Nested Comments**: Comments support depth layering for organization

## Finding Nodes in Graph

### Search by Keywords
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "find-nodes-in-graph",
      "arguments": {
        "searchTerms": ["print", "debug"],
        "searchType": "keyword",
        "caseSensitive": false,
        "maxResults": 10
      }
    },
    "id": 1
  }'
```

### Search by Node GUIDs
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "find-nodes-in-graph",
      "arguments": {
        "searchTerms": ["AAE5F1A0-4B2E-8F9E-003C-6B8F12345678"],
        "searchType": "guid"
      }
    },
    "id": 1
  }'
```

### Find Nodes Workflow
1. Use `find-nodes-in-graph` to search for specific nodes
2. Get detailed node information including GUIDs and pin IDs
3. Use node/pin GUIDs with other tools like `connect-pins` or `set-input-pin-value`
4. More efficient than calling `get-focused-blueprint` for large graphs

## Common Patterns

### Building a Simple Print Debug Flow
1. Search for "Event BeginPlay"
2. Add Event BeginPlay node
3. Search for "Print String"
4. Add Print String node
5. Connect execution pins
6. Set string value on Print String node

### Working with Variables
1. Search for variable getter/setter nodes
2. Add variable nodes to graph
3. Connect data pins to function parameters
4. Chain execution through multiple operations

## Implementation Notes

- Node search uses Unreal's action database system
- Action identifiers use `>` as delimiter for menu paths
- Node placement respects graph zoom and pan state
- Pin connections validate type compatibility
- All operations use transactions for undo support