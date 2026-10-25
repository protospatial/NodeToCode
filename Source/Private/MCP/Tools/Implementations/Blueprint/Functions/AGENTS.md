# Blueprint Function Management Tools

This directory contains MCP tools for creating, opening, and managing Blueprint functions. These tools enable programmatic manipulation of Blueprint function graphs.

## Available Tools

### create-blueprint-function
- **Description**: Creates a new Blueprint function with specified parameters and opens it in the editor
- **Parameters**:
  - `functionName` (required): Name of the function to create
  - `blueprintPath` (optional): Asset path of the Blueprint
  - `parameters` (optional): Array of parameter definitions with type, direction, containers
  - `functionFlags` (optional): Configuration flags (isPure, isStatic, category, etc.)
- **Returns**: Function GUID, graph information, and success status
- **Use Case**: Programmatically creating functions with complex signatures

### open-blueprint-function
- **Description**: Opens a Blueprint function in the editor using its GUID
- **Parameters**:
  - `functionGuid` (required): The GUID of the function to open
  - `blueprintPath` (optional): Asset path of the Blueprint
  - `centerView` (optional): Center graph view on function entry
  - `selectNodes` (optional): Select all nodes in the function
- **Returns**: Function information and editor state
- **Use Case**: Navigating to specific functions after creation or from analysis

### delete-blueprint-function
- **Description**: Deletes a specific Blueprint function using its GUID
- **Parameters**:
  - `functionGuid` (required): The GUID of the function to delete
  - `blueprintPath` (optional): Asset path of the Blueprint
  - `force` (optional): Force deletion even with references
- **Returns**: Deleted function info and removed references
- **Use Case**: Cleaning up unused functions or refactoring

### add-function-input-pin
- **Description**: Adds a new input parameter to the currently focused Blueprint function
- **Parameters**:
  - `pinName` (required): Name for the new input parameter
  - `typeIdentifier` (required): Type identifier from search-variable-types (e.g., 'bool', '/Script/Engine.Actor')
  - `defaultValue` (optional): Default value for the parameter
  - `isPassByReference` (optional): Whether the parameter is passed by reference
  - `tooltip` (optional): Tooltip description for the parameter
- **Returns**: Created pin information including:
  - `pinName`: The requested name
  - `actualName`: The actual created name (may differ if made unique)
  - `displayName`: The display name shown in the editor
  - `pinId`: The unique pin ID
  - Type details
- **Use Case**: Dynamically adding parameters to existing functions

### add-function-return-pin
- **Description**: Adds a new return value to the currently focused Blueprint function
- **Parameters**:
  - `pinName` (required): Name for the new return value
  - `typeIdentifier` (required): Type identifier from search-variable-types (e.g., 'bool', '/Script/Engine.Actor')
  - `tooltip` (optional): Tooltip description for the return value
- **Returns**: Created pin information including:
  - `pinName`: The requested name
  - `actualName`: The actual created name (may differ if made unique)
  - `displayName`: The display name shown in the editor
  - `pinId`: The unique pin ID
  - Type details
- **Use Case**: Adding multiple return values to functions (Blueprint functions support multiple returns)

### remove-function-entry-pin
- **Description**: Removes a parameter pin from the function entry node
- **Parameters**:
  - `pinName` (required): Name of the pin to remove - can use either the internal name or display name
- **Returns**: Success status with removed pin name and function information
- **Use Case**: Removing any parameter pin from the function entry node
- **Note**: The tool will match pin names by internal name first, then display name. Case-insensitive matching is supported.

### remove-function-return-pin
- **Description**: Removes a return value from the currently focused Blueprint function
- **Parameters**:
  - `pinName` (required): Name of the return value to remove - can use either the internal name or display name
- **Returns**: Success status with removed pin name and function information
- **Use Case**: Simplifying function return values during refactoring
- **Note**: The tool will match pin names by internal name first, then display name. Case-insensitive matching is supported.

## Parameter Type System

### Basic Types
- Primitives: `"bool"`, `"int32"`, `"float"`, `"FString"`, `"FName"`, `"FText"`
- Vectors: `"FVector"`, `"FVector2D"`, `"FRotator"`, `"FTransform"`
- Colors: `"FLinearColor"`, `"FColor"`

### Object References
- Use `type: "object"` with `subType` for specific classes:
  ```json
  {
    "name": "Target",
    "type": "object",
    "subType": "Actor"
  }
  ```

### Container Types
- Arrays: `container: "array"`
- Sets: `container: "set"`
- Maps: `container: "map"` with `keyType` specification

### Example: Complex Function Creation
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-blueprint-function",
      "arguments": {
        "functionName": "FindActorsByTag",
        "parameters": [
          {
            "name": "Tag",
            "type": "FName",
            "direction": "input"
          },
          {
            "name": "FoundActors",
            "type": "object",
            "subType": "Actor",
            "container": "array",
            "direction": "output"
          },
          {
            "name": "Count",
            "type": "int32",
            "direction": "output"
          }
        ],
        "functionFlags": {
          "isPure": false,
          "category": "Gameplay",
          "tooltip": "Finds all actors with the specified tag"
        }
      }
    },
    "id": 1
  }'
```

## Function Lifecycle Workflow

1. **Create Function**: Use `create-blueprint-function` to define the function
2. **Get Function GUID**: Save the returned `functionGuid` for future operations
3. **Open for Editing**: Use `open-blueprint-function` with the GUID
4. **Add/Modify Parameters**: 
   - Use `add-function-input-pin` to add new input parameters
   - Use `add-function-return-pin` to add new return values
   - Use `remove-function-entry-pin` to remove any pin from the entry node
   - Use `remove-function-return-pin` to remove return values from result node
5. **Add Implementation**: Use graph manipulation tools to add nodes
6. **Delete if Needed**: Use `delete-blueprint-function` for cleanup

## Example: Adding Function Input Pin

```bash
# First, make sure you have a function open in the editor
# Then add a new input parameter:
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-function-input-pin",
      "arguments": {
        "pinName": "TargetActor",
        "typeIdentifier": "/Script/Engine.Actor",
        "isPassByReference": true,
        "tooltip": "The actor to process",
        "defaultValue": ""
      }
    },
    "id": 1
  }'
```

## Example: Adding Function Return Pin

```bash
# First, make sure you have a function open in the editor
# Then add a new return value:
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-function-return-pin",
      "arguments": {
        "pinName": "bSuccess",
        "typeIdentifier": "bool",
        "tooltip": "True if the operation succeeded"
      }
    },
    "id": 1
  }'
```

## Example: Removing Function Entry Pin

```bash
# First, make sure you have a function open in the editor
# Then remove an existing pin from the entry node (input parameter or return value):
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "remove-function-entry-pin",
      "arguments": {
        "pinName": "TargetActor"
      }
    },
    "id": 1
  }'
```

## Example: Removing Function Return Pin

```bash
# First, make sure you have a function open in the editor
# Then remove an existing return value:
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "remove-function-return-pin",
      "arguments": {
        "pinName": "bSuccess"
      }
    },
    "id": 1
  }'
```

## Implementation Notes

- Functions are created using `UK2Node_FunctionEntry` and `UK2Node_FunctionResult`
- **Pin Storage in Blueprint Functions**:
  - `UK2Node_FunctionEntry` stores BOTH input parameters AND return values
  - `UK2Node_FunctionResult` only references/displays return values, doesn't store them
  - This is why `remove-function-entry-pin` can remove both types of pins
- Parameter types are resolved through Unreal's type registry
- All operations use transactions for undo/redo support
- Function GUIDs persist across editor sessions
- Blueprint functions support multiple return values (unlike C++)
- Return pins don't support default values or pass-by-reference
- Pin removal automatically breaks connections before deleting
- Cannot remove execution pins or non-user-defined pins
- When creating pins, Unreal may rename them for uniqueness (e.g., "MyPin" → "MyPin_0")
- Always use the `actualName` or `displayName` from the creation response when removing pins
- Pin removal supports both internal names and display names with case-insensitive matching