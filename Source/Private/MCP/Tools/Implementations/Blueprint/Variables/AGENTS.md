# Blueprint Variable Tools

This directory contains the MCP tools in the 'Blueprint Variable Management' category: creating member and local
variables, reading them, setting their defaults, placing Get and Set nodes, and searching variable types.

## Targeting: the focused Blueprint only

Every tool here works on the Blueprint, or the graph, of the active Blueprint editor. Member-variable tools resolve
it with `FN2CMcpBlueprintUtils::ResolveBlueprint(TEXT(""))`, graph tools with
`FN2CMcpBlueprintUtils::GetFocusedEditorGraph`, and the local-variable tools also need that graph to be a function
graph (they look for its `UK2Node_FunctionEntry`). No tool in this folder has a `blueprintPath` parameter, and the MCP
layer does not reject arguments a schema does not list, so an extra `blueprintPath` is ignored and the edit goes to
the focused Blueprint. Open the Blueprint first (`open-blueprint-asset`, or `open-blueprint-function` for a function
graph).

- **Known issue (card 0274):** there is no explicit target; a `blueprintPath` argument is silently ignored.

Variable names are matched case-insensitively (`FString` comparison in `FindMemberVariable` and `FindVariable`, `FName`
comparison in `FindLocalVariable`): `health` finds `Health`.

## Available Tools

### create-variable
- **Description**: Creates a member variable in the focused Blueprint
  (`FBlueprintEditorUtils::AddMemberVariable` in `FN2CMcpCreateVariableTool::CreateVariable`)
- **Parameters**:
  - `variableName` (required): Name for the new variable. Empty names, names with invalid object-name characters and
    a few reserved words (`None`, `Self`, `True`, `Class`, `Return`, ...) are refused (`ValidateVariableName`)
  - `typeIdentifier` (required): Type identifier, see "Type Identifiers" (for maps, the VALUE type)
  - `defaultValue` (optional): Default value, as text
  - `category` (optional, default `Default`): Category shown in the My Blueprint panel
  - `isInstanceEditable` (optional, default true): Per-instance editability
  - `isBlueprintReadOnly` (optional, default false): Read-only in graphs
  - `tooltip` (optional): Tooltip text
  - `replicationCondition` (optional, default `none`): `none`, `replicated` or `repnotify`
  - `containerType` (optional, default `none`): `none`, `array`, `set` or `map`
  - `mapKeyTypeIdentifier` (optional): Key type, required when `containerType` is `map` and refused otherwise
- **Returns**: `success`, `variableName` (as requested), `actualName`, `blueprintName`, `typeInfo`,
  `containerType`, `message`. A taken name is made unique by `FBlueprintEditorUtils::FindUniqueKismetName`: a
  second `Health` becomes `Health_0`. For a map, `typeInfo` describes the KEY type
- **Known issue (card 0154):** `isInstanceEditable`, `isBlueprintReadOnly` and the replication options are written
  to the compiled class property (`ApplyVariableProperties`), not to the variable description, so the next compile
  can drop them, and `isInstanceEditable` turns on Expose to Cinematics (`SetInterpFlag`)

### create-local-variable
- **Description**: Creates a local variable in the focused function graph
  (`FN2CMcpCreateLocalVariableTool::CreateLocalVariable` appends to `UK2Node_FunctionEntry::LocalVariables`)
- **Parameters**: `variableName` (required), `typeIdentifier` (required), `defaultValue`, `tooltip`,
  `containerType`, `mapKeyTypeIdentifier` (as for create-variable)
- **Returns**: `success`, `variableName`, `actualName`, `typeInfo`, `functionName`, `blueprintName`, `message`, and
  for a container `containerType` (plus a `note` for maps). The name is made unique among the function's locals only
  (`MakeUniqueLocalVariableName`): a second `TempCounter` becomes `TempCounter_1`; a local may share a member
  variable's name
- **Errors**: In an event graph,
  `Not in a function graph. Local variables can only be created in functions, not event graphs.`

### search-variable-types
- **Description**: Searches the types a variable can have and returns identifiers the other tools accept
- **Parameters**:
  - `searchTerm` (required, not empty): Space-separated words; every word must appear in a type's display name or
    description (case-insensitive). Results are ranked: exact name, name prefix, name contains, description contains
    (`FilterTypesBySearchTerm`)
  - `category` (optional, default `all`): `all`, `primitive`, `class`, `struct` or `enum` (case-insensitive)
  - `includeEngineTypes` (optional, default true): With false, types whose path starts with `/Script/` or `/Engine/`
    are left out (`IsEngineType`)
  - `maxResults` (optional, default 50): Clamped to 1-200
- **Returns**: `types` (each with `typeName`, `typeIdentifier`, `category`, `description`, plus `icon` for
  primitives, `parentClass` and `isAbstract` for classes, `values` for enums) and `totalMatches`, the number returned
  (after the `maxResults` cut, not the number that matched). Primitive identifiers are `bool`, `uint8`, `int32`,
  `int64`, `float`, `double`, `FString`, `FText` and `FName`; every other identifier is a full path
  (`/Script/Engine.HitResult`), which is the safe form for every type (see "Type Identifiers")
- **Errors**: Needs an open Blueprint editor even though it reads no Blueprint: `No active Blueprint editor found`

### get-blueprint-member-variables
- **Description**: Lists the member variables declared by the focused Blueprint (`Blueprint->NewVariables`; inherited
  and component variables are not included)
- **Parameters**: None
- **Returns**: `blueprintName`, `blueprintPath`, `parentClass`, `variables`, `variableCount`, `categories`. Each
  variable has `name`, `category`, `friendlyName`, `defaultValue`, `guid`, `type` (`category`, `subCategory`,
  `typeObject`, `typeName`, `container`, `keyType` for maps, `isReference`, `isConst`, `displayString`), `flags`
  (`instanceEditable`, `blueprintVisible`, `blueprintReadOnly`, `exposeOnSpawn`, `private`, `transient`, `saveGame`,
  `advancedDisplay`, `deprecated`, `config`, `replication`) and `metadata`. The result is marked as untrusted
  Blueprint data: its first key is `untrusted_content` (card 0124)
- **Known issue (card 0283):** `FN2CMcpGetBlueprintMemberVariablesTool::PinTypeToJson` reports float and double
  variables with `category` and `displayString` `unknown` (the type is in `subCategory`), and for a map it reports
  the key type as `category` and the value type as `keyType`. `defaultValue` is always empty: the compile moves the
  description's default into the class default object

### create-get-member-variable-node
- **Description**: Places a Get node for a member variable in the focused graph
- **Parameters**:
  - `variableName` (required): A variable from get-blueprint-member-variables
  - `location` (optional): `{ "x": number, "y": number }`, graph coordinates (default 0, 0)
- **Returns**: `success`, `nodeId`, `nodeType` (`K2Node_VariableGet`), `variableName`, `graphName`, `blueprintName`,
  `location`, `pins` (each with `id` = the pin GUID, `name`, `direction`, `type`, `friendlyName`), `variableType`,
  `message`
- **Known issue (card 0155):** `nodeId` is a counter string such as `GetNode_Health_1`, not a node GUID, and the node
  is created without one (`CreateGetNode` never calls `CreateNewGuid`; get-focused-blueprint shows its GUID as all
  zeros). connect-pins answers `INVALID_GUID` for that `nodeId` and cannot find the node by GUID, so the node cannot
  be wired through MCP

### create-set-member-variable-node
- **Description**: Places a Set node for a member variable in the focused graph
- **Parameters**:
  - `variableName` (required)
  - `defaultValue` (optional): Value for the Set node's input pin (not the variable's default). A value the pin
    rejects is only logged as a warning; the call still succeeds
  - `location` (optional): As for the Get node
- **Returns**: As for create-get-member-variable-node, with `nodeType` `K2Node_VariableSet`, `nodeId` such as
  `SetNode_Health_1`, and each input pin's `defaultValue`
- **Known issue (card 0155):** the same counter `nodeId` and missing node GUID as the Get node

### set-member-variable-default-value
- **Description**: Sets a member variable's default value (the Details panel value), without adding a node
- **Parameters**:
  - `variableName` (required)
  - `defaultValue` (required, not empty): An empty string is refused with `Field 'defaultValue' cannot be empty.`
    (`TryGetRequiredString`); there is no reset-to-zero form
- **Returns**: `success`, `variableName`, `blueprintName`, `blueprintPath`, `oldDefaultValue`, `newDefaultValue`,
  `typeInfo`, `category`, `message`, `compilationSuccess`, `compilationErrorCount`, `compilationWarningCount`,
  `compilationTime`. The value reaches the class default object (a float set to `75` reads back as 75.0)
- **Known issue (card 0283):** `oldDefaultValue` is always empty, and `ValidateDefaultValue` checks the text only for
  bool, int, int64 and byte variables: floats are `real` pins in UE5, so `abc` is accepted for a float

### get-blueprint-function-local-variables
- **Description**: Lists the local variables of the focused function graph
- **Parameters**:
  - `includeDetails` (optional, default true): With false, only `name`, `displayName` and `category` per variable
- **Returns**: `blueprintName`, `functionName`, `localVariableCount`, `localVariables` (with details: `type` with
  `typeName`, `subCategory`, `subCategoryObject`, `isArray`, `isSet`, `isMap`, `isReference`, `isConst`;
  `defaultValue`, `guid`, `flags`, `replicationCondition`). Marked as untrusted Blueprint data (`untrusted_content`)
- **Errors**: Outside a function graph, `The focused graph is not a function graph. ...`

### create-get-local-function-variable-node
- **Description**: Places a Get node for a local variable in the focused function graph
- **Parameters**: `variableName` (required), `x`, `y` (optional, graph coordinates, default 0)
- **Returns**: `message`, `nodeId` (the node's GUID, usable with connect-pins), `nodeClass`, `nodeType`,
  `variableName` (as sent), `functionName`, `blueprintName`, `x`, `y`, `variableType`, `pins` (`name`, `type`,
  `direction`, `id` = pin GUID, `friendlyName`)

### create-set-local-function-variable-node
- **Description**: Places a Set node for a local variable in the focused function graph
- **Parameters**: `variableName` (required), `x`, `y` (optional), `inputPinValue` (optional): value for the node's
  input pin
- **Returns**: `message`, `nodeId`, `nodeClass`, `x`, `y`, `pins` (`name`, `type`, `direction`, `id`, and
  `defaultValue` on inputs with one)
- **Known issue (card 0155):** `nodeId` is all zeros and `pins` lists every pin twice:
  `FN2CMcpCreateSetLocalFunctionVariableNode::Execute` calls `AllocateDefaultPins` after `ReconstructNode` and never
  gives the node a GUID

### set-local-function-variable-default-value
- **Description**: Sets a local variable's default value
- **Parameters**: `variableName` (required), `defaultValue` (required, not empty, as for the member tool)
- **Returns**: `message`, `variableName`, `functionName`, `oldValue`, `newValue`, `variableType`, `containerType`,
  `compilationResults` (`success`, `errorCount`, `warningCount`, `compilationTime`)

## Type Identifiers

`create-variable`, `create-local-variable` and the function tools resolve `typeIdentifier` with
`FN2CMcpTypeResolver::ResolvePinType`. The checks run in this order:

1. **Paths**: an identifier starting with `/Script/`, `/Game/` or `/Plugin/` is loaded as an object path and must be
   a class, struct or enum (`ResolveFullObjectPathInternal`): `/Script/Engine.Actor`, `/Script/Engine.HitResult`,
   `/Script/CoreUObject.Guid`, `/Script/Engine.ECollisionChannel`, `/Game/Data/S_MyStruct.S_MyStruct`. A path into
   plugin content (`/<PluginName>/...`) or `/Engine/` does not start with these prefixes: it goes to step 5, which
   loads it but always makes an object pin, even for a struct or enum (card 0153).
2. **Generic categories**: `object`, `class`, `struct`, `enum` and `interface` need a sub-type (the `subType` of a
   create-blueprint-function parameter); as a variable's `typeIdentifier` they fail with `INVALID_SUBTYPE`.
3. **Primitives** (`ResolvePrimitiveTypeInternal`, case-insensitive): `bool`/`boolean`, `byte`/`uint8`, `int`/`int32`,
   `int64`, `float`, `double`, `string`/`FString`, `text`/`FText`, `name`/`FName`. `float` and `double` both become a
   `real` pin (sub-category `float` or `double`).
4. **Math structs** (`ResolveMathTypeInternal`, case-insensitive, with or without the `F`): `FVector` (also `vector3`),
   `FVector2D`, `FVector4`, `FRotator`, `FTransform`, `FQuat`, `FColor`, `FLinearColor`. These eight are the only
   structs a short name works for. The special names `wildcard`, `delegate`, `multicastdelegate`, `softobject` and
   `softclass` are checked here too (`ResolveSpecialTypeInternal`).
5. **Anything else** is looked up by object name (`ResolveObjectLikeTypeInternal` with `object`, then
   `FindObjectByPathOrName`, which walks every loaded object). A class name without its prefix works (`Actor`,
   `Pawn`); `AActor`, `FHitResult`, `FGuid` and `FDateTime` fail with `INVALID_PARAMETER_TYPE: Unknown type`.

- **Struct types**: use the full path, as search-variable-types returns it. `FHitResult` and `FGuid` are refused.
- **Known issue (card 0153):** a short struct name without the `F` (`HitResult`, `Guid`) or a short enum name
  (`ECollisionChannel`) is found by the name walk and becomes an object pin whose sub-type is a struct or enum. On
  UE 5.8, `create-variable` with `HitResult` fails an assertion (`ClassObject != nullptr`) in
  `FBlueprintEditorUtils::AddMemberVariable` and the editor exits. Never send these short names.
- **Known issue (card 0153):** native enums (`/Script/Engine.ECollisionChannel`) get the `enum` pin category, which
  the compiler does not treat like an editor-made enum variable, and `wildcard`, `delegate`, `multicastdelegate`,
  `softobject` and `softclass` (step 4) are accepted with no check of whether a variable can have that type.

### Containers
- `containerType` `array` or `set`: `typeIdentifier` is the element type (sets are not checked for hashable elements).
- `containerType` `map`: `typeIdentifier` is the VALUE type and `mapKeyTypeIdentifier` the KEY type.
  `FN2CMcpVariableUtils::ValidateMapKeyType` accepts byte, integer, int64, float/double, name, string, object, class,
  soft object, soft class and enum keys, and refuses bool (`Boolean types cannot be used as map keys`), text, and
  every struct (`Struct types like '/Script/CoreUObject.Guid' cannot be used as map keys in Blueprints`).

#### Array Variable
```json
{
  "variableName": "ActorArray",
  "typeIdentifier": "/Script/Engine.Actor",
  "containerType": "array"
}
```

#### Map Variable
```json
{
  "variableName": "NameToActorMap",
  "typeIdentifier": "/Script/Engine.Actor",
  "containerType": "map",
  "mapKeyTypeIdentifier": "FName"
}
```

## Workflow Examples

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it. Each acts on the focused Blueprint.

### Finding and Creating Variables
```bash
# Step 1: Search for Actor-related types
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-variable-types",
      "arguments": {
        "searchTerm": "Actor",
        "category": "class"
      }
    },
    "id": 1
  }'

# Step 2: Create a variable using a found typeIdentifier
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-variable",
      "arguments": {
        "variableName": "TargetActor",
        "typeIdentifier": "/Script/Engine.Actor",
        "category": "Gameplay",
        "tooltip": "The actor to interact with"
      }
    },
    "id": 2
  }'
```

### Creating Complex Container Variables
```bash
# Create a map from String to Transform
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-variable",
      "arguments": {
        "variableName": "SavedTransforms",
        "typeIdentifier": "FTransform",
        "containerType": "map",
        "mapKeyTypeIdentifier": "FString",
        "category": "Save System"
      }
    },
    "id": 1
  }'
```

### Inspecting Blueprint Variables
```bash
# Get all member variables from the focused Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get-blueprint-member-variables",
      "arguments": {}
    },
    "id": 1
  }'
```

### Creating Get and Set Nodes for Member Variables
These nodes cannot be wired with connect-pins yet (card 0155, above).
```bash
# Create a Get node for a member variable
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-get-member-variable-node",
      "arguments": {
        "variableName": "Health",
        "location": { "x": 200, "y": 200 }
      }
    },
    "id": 1
  }'

# Create a Set node whose input pin holds 100.0
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-set-member-variable-node",
      "arguments": {
        "variableName": "Health",
        "defaultValue": "100.0",
        "location": { "x": 400, "y": 200 }
      }
    },
    "id": 2
  }'
```

### Setting Variable Default Values
```bash
# Set the default value of a member variable (like in the Details panel); the value may not be empty
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-member-variable-default-value",
      "arguments": {
        "variableName": "Health",
        "defaultValue": "100"
      }
    },
    "id": 1
  }'
```

### Working with Local Function Variables
A function graph must be focused (for example after `create-blueprint-function` or `open-blueprint-function`).
```bash
# Get all local variables in the focused function
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get-blueprint-function-local-variables",
      "arguments": {}
    },
    "id": 1
  }'

# Create a Get node for a local variable
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-get-local-function-variable-node",
      "arguments": {
        "variableName": "TempCounter",
        "x": 200,
        "y": 150
      }
    },
    "id": 2
  }'

# Create a Set node for a local variable
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-set-local-function-variable-node",
      "arguments": {
        "variableName": "TempCounter",
        "x": 300,
        "y": 150,
        "inputPinValue": "0"
      }
    },
    "id": 3
  }'

# Set the default value of a local variable
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-local-function-variable-default-value",
      "arguments": {
        "variableName": "TempResult",
        "defaultValue": "false"
      }
    },
    "id": 4
  }'
```

## Undo, Compile and Save

- **Undoable** (the tool opens an `FScopedTransaction`): `set-member-variable-default-value`
  (`ApplyDefaultValue`), `set-local-function-variable-default-value`, `create-get-local-function-variable-node` and
  `create-set-local-function-variable-node`. These are the four `.cpp` files in this folder that open one.
- **Not undoable** by their own transaction: `create-variable`, `create-local-variable`,
  `create-get-member-variable-node` and `create-set-member-variable-node`. Ctrl+Z after one of them undoes the
  editor's previous transaction instead, and restoring that earlier snapshot can also remove the untracked change
  (an undo of an earlier `set-member-variable-default-value` removed a variable `create-variable` had added after it).
- **Known issue (cards 0154, 0155, 0283):** the four tools above get transactions in those cards, and undoing
  `set-member-variable-default-value` restores the variable description but not the class default object.
- **Read-only**: `search-variable-types`, `get-blueprint-member-variables`, `get-blueprint-function-local-variables`.
- Every successful edit compiles the Blueprint synchronously and marks its package dirty
  (`FN2CMcpBlueprintUtils::MarkBlueprintAsModifiedAndCompile`). Nothing is saved: use `save-blueprint`.

## Implementation Notes

- Member variables are stored in `Blueprint->NewVariables`; local variables in
  `UK2Node_FunctionEntry::LocalVariables`
- Member Get/Set nodes use `VariableReference.SetSelfMember()`, local ones `VariableReference.SetLocalMember()`
- Container types are set on the `FEdGraphPinType` (`ContainerType`, and `PinValueType` for a map's value)
