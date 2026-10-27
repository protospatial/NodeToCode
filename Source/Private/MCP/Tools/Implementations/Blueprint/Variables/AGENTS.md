# Blueprint Variable Tools

This directory contains the MCP tools in the `BlueprintVariableTools` toolset: creating member and local
variables, reading them, setting their defaults, placing Get and Set nodes, and searching variable types.

## Status: frozen, bug fixes only (dec-5)

Every tool here (`BlueprintVariableTools`) is frozen, the read-only `search-variable-types` and the two list
tools included. Decision record [0007](../../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b).

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in this family, and new arguments, modes or result fields that
  add capability. On UE 5.8 that work belongs to Epic's EditorToolset (`BlueprintTools.list_variables`, `create_node`, `ObjectTools.set_properties`); on 5.6 and 5.7 it waits for the
  owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.
- **Fix cards done here:** 0274 (explicit targets, admitted as next-6) and 0295 (toast icons, now
  `FN2CMcpNotification`). Card 0292 (class,
  soft and interface references) is new capability and waits for the owner.

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

## Targeting: explicit, else the focused Blueprint

Every tool here takes an explicit target and falls back to the active Blueprint editor without one (card 0274); none
needs an open editor when the target is given:
- **Member-variable tools** (`create-variable`, `get-blueprint-member-variables`, `set-member-variable-default-value`)
  take optional `blueprintPath` (`FN2CGraphTarget::AddSchemaProperties(Definition, false)`), resolved by
  `FN2CGraphTarget::LoadBlueprint`: `/Game/BP_Door` or `/Game/BP_Door.BP_Door`; without it, the focused Blueprint
  (`NO_FOCUSED_GRAPH: ...` when none). A path that loads no Blueprint gives `ASSET_NOT_FOUND: ...`. Their results
  carry `blueprintPath`.
- **Graph tools** (the four node tools, `create-local-variable`, `get-blueprint-function-local-variables`,
  `set-local-function-variable-default-value`) take optional `blueprintPath`, `graphName` and `graphGuid` and resolve
  them with `FN2CGraphTarget::Resolve(Arguments)`, as the Graph and Functions tools do (card 0152): with none, the
  focused graph; with `blueprintPath` alone, its focused graph or else its event graph; `graphGuid` (a function's
  `functionGuid`) wins over `graphName`. Every result, errors included, has `target` (`blueprintPath`,
  `graphName`, `graphGuid`, `graphKind`, `resolvedBy`). The editing tools refuse a read-only graph with
  `READ_ONLY_GRAPH`. The local-variable tools need that graph to be in a function: the function graph itself, or a
  collapsed graph inside it (one shared `FN2CMcpVariableUtils::FindFunctionEntryNode`, which goes through
  `FBlueprintEditorUtils::GetTopLevelGraph`; the function's name is the locals' scope and the `functionName` they
  return, card 0155); any other graph gives `NOT_A_FUNCTION_GRAPH: ...`.
- `search-variable-types` reads no Blueprint and needs no editor.

Variable names are matched case-insensitively (`FName` lookups in `FN2CMcpVariableUtils::FindMemberVariableProperty`
and `FindLocalVariable`, `FString` comparison in `FindVariable`): `health` finds `Health`.

## Available Tools

### create-variable
- **Description**: Creates a member variable in the Blueprint `blueprintPath` names, else the focused Blueprint
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
- **Description**: Creates a local variable in the target function graph
  (`FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction`, which calls `FBlueprintEditorUtils::AddLocalVariable`
  on the graph's top-level graph, so a collapsed graph inside a function works too). The graph comes from
  `FN2CGraphTarget::Resolve` with the target arguments (cards 0152, 0274), so a read-only graph (an interface
  function) is refused with `READ_ONLY_GRAPH: ...`. The local gets `CPF_BlueprintVisible`, the `Default` category and the
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
  - `includeEngineTypes` (optional, default true): With false, types from the engine and engine plugins are left
    out: a native type by where its module's binary is (under the engine directory or not), an asset by its mount
    point (`/Engine`, or a plugin loaded from the engine). The project's own native types (`/Script/MyGame`) and
    project plugins' types stay (`IsEngineTypePath`). Primitives always stay
  - `maxResults` (optional, default 50): Clamped to 1-200
- **Returns**: `types` (each with `typeName`, `typeIdentifier`, `category`, `description`, plus `icon` for
  primitives, `parentClass` and `isAbstract` for classes, and for enums `values`, the enumerator names a default value
  takes (`NewEnumerator0`), and `displayNames`, what the editor shows), `totalMatches`, the number that matched before
  the `maxResults` cut, and `truncated`, true when the list was cut. Each type is listed once. Primitive identifiers are `bool`, `uint8`, `int32`,
  `int64`, `float`, `double`, `FString`, `FText` and `FName`; every other identifier is a full path
  (`/Script/Engine.HitResult`), which is the safe form for every type (see "Type Identifiers")
- **Sources**: one cached index, built without loading any asset: native classes, structs and enums, and from the
  asset registry the user structs, user enums and Blueprints (subclasses included, so Widget and Anim Blueprints),
  filtered by `UUserDefinedStruct::StaticClass()->GetClassPathName()` and the like (since 5.5 the struct class is in
  CoreUObject, and the registry applies no class redirects). A Blueprint's identifier is its generated class's object
  path (`/Game/BP_Enemy.BP_Enemy_C`). The native part is rebuilt when a module loads, the asset part when the asset
  registry adds, removes, renames or updates an asset. User types loaded in memory but not in the registry are added
  per search. Only the enums returned are loaded, to list their enumerators
- Needs no Blueprint editor (card 0274; it used to fail with `No active Blueprint editor found`)

### get-blueprint-member-variables
- **Description**: Lists the member variables declared by the Blueprint `blueprintPath` names, else the focused one
  (`Blueprint->NewVariables`; inherited and component variables are not included)
- **Parameters**: `blueprintPath` (optional)
- **Returns**: `blueprintName`, `blueprintPath`, `parentClass`, `variables`, `variableCount`, `categories`. Each
  variable has `name`, `category`, `friendlyName`, `defaultValue`, `guid`, `type` (`category`, `subCategory`,
  `typeObject`, `typeName`, `container`, `keyType` and `valueType` for maps, `isReference`, `isConst`,
  `displayString`), `flags` (`instanceEditable`, `blueprintVisible`, `blueprintReadOnly`, `exposeOnSpawn`, `private`,
  `transient`, `saveGame`, `advancedDisplay`, `deprecated`, `config`, `replication`) and `metadata`. The result is
  marked as untrusted Blueprint data: its first key is `untrusted_content` (card 0124). Card 0283:
  - `defaultValue` is read from the class default object (`FN2CMcpVariableDefaults::ReadMemberDefault`, the
    editor's `PropertyValueToString`: `100`, `75.000000`, `(X=1.000000,Y=2.000000,Z=3.000000)`); before the class
    has been compiled with the variable, the description's pending default
  - `type.category` is `float` or `double` for a real (UE5's `real` pin with that sub-category). The top-level type
    fields describe the element type, which for a map is its value (what create-variable's `typeIdentifier` takes);
    `keyType` is the key (`mapKeyTypeIdentifier`) and `valueType` the value again; `displayString` is
    `TMap<key, value>`
  - `instanceEditable` is `CPF_Edit` without `CPF_DisableEditOnInstance` (a new variable is not Instance Editable);
    `private` is the `BlueprintPrivate` metadata (the details panel's Private box)

### create-get-member-variable-node
- **Description**: Places a Get node for a member variable in the target graph. The variable is looked up on the
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
- **Description**: Places a Set node for a member variable in the target graph
- **Parameters**:
  - `variableName` (required)
  - `defaultValue` (optional): Value for the Set node's input pin (not the variable's default), checked by the K2
    schema first as set-input-pin-value does (card 0278). A value the pin refuses does not fail the call (the node is
    still made): the result has `defaultValueApplied: false`, the reason in `defaultValueReason` (also in `warning`),
    and the pin keeps its default. With a value sent, `defaultValueApplied` is `true` when it took
  - `location` (optional): As for the Get node
- **Returns**: As for create-get-member-variable-node, with `nodeType` `K2Node_VariableSet`
- **Errors**: `READ_ONLY_VARIABLE: ...` for a variable Blueprints may read but not write (`BlueprintReadOnly`, such as
  `RootComponent`, or private to another class), as the editor offers no Set node for it; nothing is added

### set-member-variable-default-value
- **Description**: Sets a member variable's default value (the Details panel value), without adding a node. Card
  0283, `FN2CMcpVariableDefaults::SetMemberDefault`: the value is parsed as the compiled property's type
  (`FBlueprintEditorUtils::PropertyValueFromString`) and written on the class default object in one undo step
  (`Modify`, `PreEditChange`, `PostEditChangeProperty`), as the Details panel does; instances still on the old
  default (placed actors, child classes' defaults) follow it, as the property editor propagates a class-default
  change. Nothing is compiled, unless the class is not up to date with the variable description (the class lacks the
  variable, the description still holds a default no compile has applied, or the Blueprint is dirty): then it
  compiles once first, outside the undo step, and the result has `compile`
- **Parameters**:
  - `variableName` (required)
  - `defaultValue` (required, may be empty): Text as the Details panel's copy gives it (`75`, `true`,
    `(X=1.0,Y=2.0,Z=3.0)`, `(1,2,3)` for an array). A boolean takes `true`/`false`/`yes`/`no`/`1`/`0`; an enum an
    entry's name, display name (any case) or index. An empty string resets the variable to its type's default value
    (zero, empty, a struct's defaults): the parent class has no such property to inherit
- **Returns**: `success`, `variableName` (as declared), `blueprintName`, `blueprintPath`, `oldDefaultValue` and
  `newDefaultValue` (both read from the class default object as exported text: `75` on a float reads back as
  `75.000000`), `instancesUpdated`, `typeInfo`, `category`, `message`
- **Errors**: `Variable '<name>' not found in Blueprint '<bp>'`; a value that does not parse is refused with nothing
  changed (no undo step, package clean): `Invalid default value for variable 'Speed': 'abc' is not a valid Float
  (single-precision) value` (an enum's error lists its entries)

### get-blueprint-function-local-variables
- **Description**: Lists the local variables of the target function graph
- **Parameters**:
  - `includeDetails` (optional, default true): With false, only `name`, `displayName` and `category` per variable
- **Returns**: `blueprintName`, `functionName`, `localVariableCount`, `localVariables` (with details: `type` with
  `typeName`, `subCategory`, `subCategoryObject`, `isArray`, `isSet`, `isMap`, `isReference`, `isConst`;
  `defaultValue`, `guid`, `flags`, `replicationCondition`). Marked as untrusted Blueprint data (`untrusted_content`)
- **Errors**: Outside a function graph, `NOT_A_FUNCTION_GRAPH: the graph is not a function graph. ...`

### create-get-local-function-variable-node
- **Description**: Places a Get node for a local variable in the target function graph
- **Parameters**: `variableName` (required), `x`, `y` (optional, graph coordinates, default 0)
- **Returns**: As for create-get-member-variable-node, plus `functionName` (the function, also from a collapsed graph
  inside it), `x` and `y`. A local's nodes have no self pin

### create-set-local-function-variable-node
- **Description**: Places a Set node for a local variable in the target function graph
- **Parameters**: `variableName` (required), `x`, `y` (optional), `inputPinValue` (optional): value for the node's
  input pin
- **Returns**: As for create-get-local-function-variable-node; the Set node has exactly four pins (`execute`, `then`,
  the value input and `Output_Get`). With `inputPinValue`: `defaultValueApplied`, and when refused
  `defaultValueReason` and `warning`, as for create-set-member-variable-node

### set-local-function-variable-default-value
- **Description**: Sets a local variable's default value. The value is parsed against the local's property on the
  skeleton class's function (`FN2CMcpVariableDefaults::PrepareLocalDefault`, card 0283) before anything changes, and
  the description gets the text the details panel stores for it (`1.5` on a float is stored as `1.500000`); then
  the Blueprint is compiled once
- **Parameters**: `variableName` (required), `defaultValue` (required, may be empty: an empty string clears the
  default; formats as for the member tool)
- **Returns**: `message`, `variableName`, `functionName`, `oldValue`, `newValue` (as stored), `variableType`,
  `containerType`, `compilationResults` (`success`, `errorCount`, `warningCount`, `compilationTime`) and `compile`
  (the compiler's messages)
- **Errors**: a value that does not parse: `Invalid default value for local variable 'Count': 'abc' is not a valid
  Integer value`, with the description unchanged and no undo step

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
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it. Without `blueprintPath` (and `graphName` or `graphGuid` for the graph
tools) each acts on the focused Blueprint.

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
Name the function with `blueprintPath` plus `graphName` or `graphGuid`, or focus it (for example after
`create-blueprint-function` or `open-blueprint-function`).
```bash
# Get all local variables of BP_Door's function OpenDoor
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get-blueprint-function-local-variables",
      "arguments": {"blueprintPath": "/Game/BP_Door", "graphName": "OpenDoor"}
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
  too). Undoing `set-member-variable-default-value` restores the class default object and the instances it changed
  (card 0283).
- **Read-only**: `search-variable-types`, `get-blueprint-member-variables`, `get-blueprint-function-local-variables`.
- Every successful edit marks the Blueprint modified (structurally for create-variable and create-local-variable)
  and its package dirty, and the tool call compiles it once, after the edit's transaction has closed, with the
  compiler's diagnostics in the result as `compile` (card 0151; fields in the Graph folder's AGENTS.md).
  set-member-variable-default-value is the exception: a class default needs no compile, so it only dirties the
  package (card 0283); set-local-function-variable-default-value's `compilationResults` come from its compile. Nothing is saved: use
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
