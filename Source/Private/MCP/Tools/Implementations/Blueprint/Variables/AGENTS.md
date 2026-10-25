# Blueprint Variable Tools

This directory contains the MCP tools in the 'Blueprint Variable Management' category: creating member and local
variables, reading them, setting their defaults, placing Get and Set nodes, and searching variable types.

## Targeting: the focused Blueprint only

Every tool here works on the Blueprint, or the graph, of the active Blueprint editor. Member-variable tools resolve
it with `FN2CMcpBlueprintUtils::ResolveBlueprint(TEXT(""))`; the four node tools and `create-local-variable` take the
focused graph from the shared resolver with no target arguments (`FN2CGraphTarget::Resolve`, so a read-only graph is
refused with `READ_ONLY_GRAPH`), and `get-blueprint-function-local-variables` and
`set-local-function-variable-default-value` from `FN2CMcpBlueprintUtils::GetFocusedEditorGraph`. The local-variable
tools need that graph to be in a function: the function graph itself, or a collapsed graph inside it (one shared
`FN2CMcpVariableUtils::FindFunctionEntryNode`, which goes through `FBlueprintEditorUtils::GetTopLevelGraph`; the
function's name is the locals' scope and the `functionName` they return, card 0155). No tool in this folder has a
`blueprintPath` parameter, and the MCP layer does not reject arguments a schema does not list, so an extra
`blueprintPath` is ignored and the edit goes to the focused Blueprint. Open the Blueprint first (`open-blueprint-asset`, or `open-blueprint-function` for a function
graph).

- **Known issue (card 0274):** there is no explicit target; a `blueprintPath` argument is silently ignored.

Variable names are matched case-insensitively (`FName` lookups in `FN2CMcpVariableUtils::FindMemberVariableProperty`
and `FindLocalVariable`, `FString` comparison in `FindVariable`): `health` finds `Health`.

## Available Tools

### create-variable
- **Description**: Creates a member variable in the focused Blueprint
  (`FBlueprintEditorUtils::AddMemberVariable` in `FN2CMcpCreateVariableTool::CreateMemberVariable`). Every setting is
  written to the variable description (`FBPVariableDescription` in `Blueprint->NewVariables`) through the engine
  helpers the details panel uses, so every later compile keeps it (card 0154):
  - `isInstanceEditable` clears `CPF_DisableEditOnInstance` (`SetBlueprintOnlyEditableFlag`); it never sets
    `CPF_Interp` (Expose to Cinematics)
  - `isBlueprintReadOnly` sets `CPF_BlueprintReadOnly` (`SetBlueprintPropertyReadOnlyFlag`)
  - `replicated` sets `CPF_Net`; `repnotify` sets `CPF_Net | CPF_RepNotify`, sets the description's `RepNotifyFunc`
    to `OnRep_<name>` and creates that function graph when the Blueprint has none (as the details panel and UE 5.8's
    `UBlueprintEditorLibrary::SetBlueprintVariableReplication` do)
  - `category` (`SetBlueprintVariableCategory`) and `tooltip` (`tooltip` metadata) also go on the description
  - The default value is written to the description; the compile moves it to the class default object
- **Parameters**:
  - `variableName` (required): Name for the new variable. Empty names, names with invalid object-name characters and
    a few reserved words (`None`, `Self`, `True`, `Class`, `Return`, ...) are refused (`ValidateVariableName`)
  - `typeIdentifier` (required): Type identifier, see "Type Identifiers" (for maps, the VALUE type)
  - `defaultValue` (optional): Default value, as text
  - `category` (optional, default `Default`): Category shown in the My Blueprint panel
  - `isInstanceEditable` (optional, default false, as for a variable added in the editor): Instance Editable
  - `isBlueprintReadOnly` (optional, default false): Read-only in graphs
  - `tooltip` (optional): Tooltip text
  - `replicationCondition` (optional, default `none`): `none`, `replicated` or `repnotify` (case-insensitive); any
    other value is refused with `INVALID_ARGUMENT: ...` before the Blueprint is changed
  - `containerType` (optional, default `none`): `none`, `array`, `set` or `map`
  - `mapKeyTypeIdentifier` (optional): Key type, required when `containerType` is `map` and refused otherwise
- **Returns**: `success`, `variableName` (as requested), `actualName`, `blueprintName`, `typeInfo`,
  `containerType`, `message`. A taken name is made unique by `FBlueprintEditorUtils::FindUniqueKismetName`: a
  second `Health` becomes `Health_0`. `typeInfo.category` is `object`, `struct`, `enum` (with `enumName` and
  `path`) or `primitive` (with the pin category as `typeName`). For a map, `typeInfo` describes the KEY type
- **Errors**: A type that does not resolve (see "Type Identifiers") fails before the Blueprint is changed, for
  example `INVALID_TYPE: 'multicastdelegate' cannot be created from a type name: ...` or
  `TYPE_NOT_FOUND: Unknown type 'HitResults'. ...`. A Blueprint Interface, Function Library or Macro Library is
  refused, as the editor offers no new variable there (`FBlueprintEditor::NewDocument_IsVisibleForType`):
  `UNSUPPORTED_BLUEPRINT_TYPE: 'BPI_Door' is a Blueprint Interface, which cannot have member variables`. With
  `repnotify`, an object of the Blueprint already named `OnRep_<name>` that is not a graph gives `NAME_COLLISION: ...`

### create-local-variable
- **Description**: Creates a local variable in the focused function graph
  (`FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction`, which calls `FBlueprintEditorUtils::AddLocalVariable`
  on the focused graph's top-level graph, so a collapsed graph inside a function works too). The focused graph comes
  from `FN2CGraphTarget::Resolve` with no target arguments (card 0152), so a read-only graph (an interface function)
  is refused with `READ_ONLY_GRAPH: ...`. The local gets `CPF_BlueprintVisible`, the `Default` category and the
  tooltip as `tooltip` metadata
- **Parameters**: `variableName` (required), `typeIdentifier` (required), `defaultValue`, `tooltip`,
  `containerType`, `mapKeyTypeIdentifier` (as for create-variable)
- **Returns**: `success`, `variableName`, `actualName`, `typeInfo`, `functionName`, `blueprintName`, `message`, and
  for a container `containerType` (plus a `note` for maps). The name is made unique with
  `FBlueprintEditorUtils::FindUniqueKismetName` in the function's scope, also checked against the entry and return
  nodes' parameters (`MakeUniqueLocalVariableName`): a name taken by a member variable, graph, parameter or another
  local of the function gets a suffix (a second `TempCounter` becomes `TempCounter_0`, a local named like the
  parameter `Target` becomes `Target_0`), and invalid characters are replaced. `actualName` is the name used
- **Errors**: In an event graph or macro,
  `NOT_A_FUNCTION_GRAPH: Not in a function graph. Local variables can only be created in functions, not event graphs.`

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
- **Description**: Places a Get node for a member variable in the focused graph. The variable is looked up on the
  Blueprint's skeleton class, so its own variables, inherited ones (a parent Blueprint or a C++ base, such as
  `RootComponent`) and component variables all work; one that Blueprints cannot read (not Blueprint visible, private
  to another class, a delegate) is refused
- **Parameters**:
  - `variableName` (required): A variable from get-blueprint-member-variables, or an inherited or component variable
  - `location` (optional): `{ "x": number, "y": number }`, graph coordinates (default 0, 0)
- **Returns** (the same shape for all four node tools, card 0155): `success`, `nodeId` (the node GUID with hyphens,
  `157CF7BA-49F8-CE45-71C9-61AAEA82547F`, for connect-pins, find-nodes-in-graph and delete-blueprint-node), the fields
  add-bp-node-to-active-graph returns (`nodeGuid`, the same GUID as 32 digits; `nodeName`; `inputPins` and
  `outputPins` with `pinGuid`, `pinName`, `displayName`, `type`, an input's `defaultValue`; visible pins only),
  `nodeType` (`K2Node_VariableGet`), `nodeClass`, `variableName` (as declared), `graphName`, `blueprintName`,
  `location`, `pins` (every pin, hidden self pin included: `id` = the pin GUID, `name`, `direction` `input` or
  `output`, `type`, `friendlyName`, an input's `defaultValue`), `variableType` (`category`, `subCategory`,
  `typeObject`, `typeName`, `isArray`, `isSet`, `isMap`), `message`, and `compile`

### create-set-member-variable-node
- **Description**: Places a Set node for a member variable in the focused graph
- **Parameters**:
  - `variableName` (required)
  - `defaultValue` (optional): Value for the Set node's input pin (not the variable's default). A value the pin
    rejects does not fail the call: the result carries `warning`
  - `location` (optional): As for the Get node
- **Returns**: As for create-get-member-variable-node, with `nodeType` `K2Node_VariableSet`
- **Errors**: `READ_ONLY_VARIABLE: ...` for a variable Blueprints may read but not write (`BlueprintReadOnly`, such as
  `RootComponent`, or private to another class), as the editor offers no Set node for it; nothing is added

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
- **Returns**: As for create-get-member-variable-node, plus `functionName` (the function, also from a collapsed graph
  inside it), `x` and `y`. A local's nodes have no self pin

### create-set-local-function-variable-node
- **Description**: Places a Set node for a local variable in the focused function graph
- **Parameters**: `variableName` (required), `x`, `y` (optional), `inputPinValue` (optional): value for the node's
  input pin
- **Returns**: As for create-get-local-function-variable-node; the Set node has exactly four pins (`execute`, `then`,
  the value input and `Output_Get`). A refused `inputPinValue` gives `warning`

### set-local-function-variable-default-value
- **Description**: Sets a local variable's default value
- **Parameters**: `variableName` (required), `defaultValue` (required, not empty, as for the member tool)
- **Returns**: `message`, `variableName`, `functionName`, `oldValue`, `newValue`, `variableType`, `containerType`,
  `compilationResults` (`success`, `errorCount`, `warningCount`, `compilationTime`)

## Type Identifiers

`create-variable`, `create-local-variable`, the function tools and the Python bridge resolve `typeIdentifier` with
`FN2CMcpTypeResolver::ResolvePinType` (`Source/Private/MCP/Utils/N2CMcpTypeResolver.cpp`). Every identifier becomes
the pin type the editor's own variable type picker makes, or the call fails with an error code before the Blueprint
is touched. The checks run in this order:

1. **Generic categories** (with a sub-type): `object`, `class`, `softobject`, `softclass`, `struct`, `enum` and
   `interface` take the class, struct or enum from a sub-type (the `subType` of a create-blueprint-function
   parameter). The variable tools send no sub-type, so there `object`, `class`, `struct`, `enum` and `interface` fail
   with `INVALID_SUBTYPE`, and `softobject` / `softclass` alone are soft references to `UObject`. A sub-type of the
   wrong kind is refused: `object` + `HitResult` gives `INVALID_TYPE: 'HitResult' is a struct (...), not a object`.
2. **Paths**: any identifier starting with `/` is an object path: `/Script/Engine.HitResult`,
   `/Script/CoreUObject.Guid`, `/Script/Engine.ECollisionChannel`, `/Script/Engine.Actor`,
   `/Game/Data/S_MyStruct.S_MyStruct`, a plugin's `/<PluginName>/...` or `/Engine/...`. `/Game/Data/S_MyStruct`
   (no object name) means the asset of that name. A Blueprint path (`/Game/BP_Enemy.BP_Enemy` or
   `/Game/BP_Enemy.BP_Enemy_C`) stands for its generated class. An asset that is not loaded is looked up in the asset
   registry first, and only a Blueprint, user struct or user enum is loaded; a map, mesh or other asset is refused
   with `INVALID_TYPE_AT_PATH` without loading it. A missing native type is `TYPE_NOT_FOUND`, a missing asset
   `ASSET_NOT_FOUND`.
3. **Primitives** (case-insensitive): `bool`/`boolean`, `byte`/`uint8`, `int`/`int32`, `int64`, `float`, `double`,
   `string`/`FString`, `text`/`FText`, `name`/`FName`. `float` and `double` both become a `real` pin (sub-category
   `float` or `double`).
4. **Math structs** (case-insensitive, with or without the `F`): `FVector` (also `vector3`), `FVector2D`, `FVector4`,
   `FRotator`, `FTransform`, `FQuat`, `FColor`, `FLinearColor`.
5. **Refused names**: `wildcard`, `exec`, `delegate` and `multicastdelegate` fail with `INVALID_TYPE` (a delegate
   variable needs a signature; make event dispatchers in the Blueprint editor). C++ template spellings
   (`TArray<int32>`, `TSubclassOf<AActor>`, `TSoftObjectPtr<...>`) fail with `INVALID_TYPE`: containers go in
   `containerType`, and class and soft references to a particular class cannot be expressed through
   `typeIdentifier` (card 0292).
6. **Type names**: anything else is a class, struct or enum name, found among loaded types with
   `FindFirstObject` (native types first) and then, for user types that are not loaded, in the asset registry
   (Blueprints, user structs, user enums). The C++ prefix is optional and names the kind: `HitResult` or `FHitResult`
   (struct), `Guid` or `FGuid`, `ECollisionChannel` (enum), `Actor`, `AActor` or `AActor*` (object reference),
   `UObject`, `IMyInterface` (interface), `BP_Enemy` or `BP_Enemy_C` (a Blueprint's class), `S_Loot`, `E_AIState`.
   A prefix only matches its own kind (`FActor` is not the class `Actor`). A name that matches more than one type
   fails with `AMBIGUOUS_TYPE` and lists their paths; pass one of them. An unknown name is `TYPE_NOT_FOUND`.

Every class, struct and enum must pass `UEdGraphSchema_K2::IsAllowableBlueprintVariableType`, as in the type picker:
a class or struct that is not a `BlueprintType`, `LevelScriptActor`, skeleton classes and internal-only structs are
refused with `INVALID_TYPE`. Enums, native or user-defined, become `byte` pins with the enum as sub-type (the only
form the Blueprint compiler turns into an enum property). As a last check every result passes
`FN2CMcpTypeResolver::ValidateVariablePinType`, so an object pin whose sub-type is not a class never reaches
`FBlueprintEditorUtils::AddMemberVariable` (on UE 5.8 that asserted and closed the editor, card 0153).
`search-variable-types` returns full paths, which always work.

### Containers
- `containerType` `array`: `typeIdentifier` is the element type.
- `containerType` `set`: `typeIdentifier` is the element type, which must be hashable
  (`FBlueprintEditorUtils::HasGetTypeHash`, as in the type picker): bool, text and structs without a hash function
  (`HitResult`) fail with `INVALID_SET_ELEMENT_TYPE`; `FGuid`, names, strings, numbers, enums and object references
  work.
- `containerType` `map`: `typeIdentifier` is the VALUE type and `mapKeyTypeIdentifier` the KEY type, which must be
  hashable in the same way (`FN2CMcpVariableUtils::ValidateMapKeyType`): `Guid` or `/Script/CoreUObject.Guid` keys
  work; bool, text and `HitResult` keys fail with `INVALID_MAP_KEY_TYPE`.

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
Wire the result's `nodeId` with connect-pins (card 0155).
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

- **Undoable** (the tool opens an `FScopedTransaction`): `create-variable` and `create-local-variable` (card 0154:
  one undo removes the variable with all its settings, and for `repnotify` the `OnRep_` function too, and leaves the
  previous transaction alone), `set-member-variable-default-value` (`ApplyDefaultValue`),
  `set-local-function-variable-default-value`, and the four node tools (card 0155: one undo removes the node with its
  input value, and leaves the previous transaction alone; the node is `RF_Transactional`, so later edits to it undo
  too).
- **Known issue (card 0283):** undoing `set-member-variable-default-value` restores the variable description but not
  the class default object.
- **Read-only**: `search-variable-types`, `get-blueprint-member-variables`, `get-blueprint-function-local-variables`.
- Every successful edit marks the Blueprint modified (structurally for create-variable and create-local-variable)
  and its package dirty, and the tool call compiles it once, after the edit's transaction has closed, with the
  compiler's diagnostics in the result as `compile` (card 0151; fields in the Graph folder's AGENTS.md).
  set-member-variable-default-value compiles once (it compiled twice before); its `compilation*` fields and
  set-local-function-variable-default-value's `compilationStatus` come from that compile. Nothing is saved: use
  `save-blueprint`.

## Implementation Notes

- Member variables are stored in `Blueprint->NewVariables`; local variables in
  `UK2Node_FunctionEntry::LocalVariables`
- The four node tools spawn through `UBlueprintVariableNodeSpawner` (`CreateFromMemberOrParam` for members,
  `CreateFromLocal` with the function graph as the scope for locals), as the editor's menus do, in
  `FN2CMcpNodeSpawn::CreateMemberVariableNode` and `CreateLocalVariableNode` (`MCP/Utils/N2CMcpNodeSpawnUtils.h`): a
  new node GUID, `RF_Transactional`, `PostPlacedNewNode`, pins allocated once, and the member reference the engine
  sets (a self reference with the variable's GUID for the Blueprint's own variables). `NodeToCode.MCP.SpawnedNodes`
  and the `SpawnedVariableNodes` capture golden test them
- Container types are set on the `FEdGraphPinType` (`ContainerType`, and `PinValueType` for a map's value)
