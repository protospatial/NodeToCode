# Blueprint Function Management Tools

This directory contains the MCP tools in the 'Blueprint Function Management' category: creating, opening and deleting
Blueprint functions and editing their parameters and return values.

## Where Parameters Live

A Blueprint function graph has one `UK2Node_FunctionEntry` and one or more `UK2Node_FunctionResult` (Return) nodes.
Both derive from `UK2Node_EditablePinBase` and keep their own `UserDefinedPins`:

- **Inputs** are user-defined pins on the entry node (its output pins, `EGPD_Output`).
  `FN2CMcpCreateBlueprintFunctionTool::CreateParameter` and `FN2CMcpAddFunctionEntryPinTool::CreateInputPin` call
  `EntryNode->CreateUserDefinedPin(..., EGPD_Output)`.
- **Outputs (return values)** are user-defined pins on the result node (its input pins, `EGPD_Input`).
  `CreateParameter` and `FN2CMcpAddFunctionReturnPinTool::CreateReturnPin` call
  `ResultNode->CreateUserDefinedPin(..., EGPD_Input)`.

So `remove-function-entry-pin` removes inputs only (`FN2CMcpRemoveFunctionEntryPinTool::Execute` searches the entry
node), and `remove-function-return-pin` removes return values (it searches the result node). Asked for a return value,
remove-function-entry-pin answers `Pin 'bSuccess' not found. Available pins: then (display: ), Tag, ...`.

## Function GUIDs

`functionGuid` is not stored anywhere. `FN2CMcpFunctionGuidUtils::GenerateDeterministicGuid` computes it as the MD5
of `<Blueprint path name>::<function graph name>`, and `FindFunctionByGuid` recomputes it for each function graph.
The same function has the same GUID in every editor session, but renaming the function, or renaming or moving the
Blueprint, gives it a new one: after a rename the old GUID answers `Function with GUID ... not found`. Get the current
GUID from `list-blueprint-functions` (`guid`) or from create-blueprint-function's result. Both the 32-digit form the
tools return and the hyphenated form are accepted (`FGuid::Parse`).

- **Known issue (card 0152):** function GUIDs change on rename; the card switches to the persistent `GraphGuid`.

## Available Tools

### create-blueprint-function
- **Description**: Creates a function graph, adds its parameters, compiles, and opens and focuses the new graph in the
  Blueprint editor
- **Parameters**:
  - `functionName` (required): Name of the new function. Refused when empty, with invalid characters, when a
    function graph of that name exists (`NAME_COLLISION`), or when the parent class has a non-BlueprintEvent
    function of that name (`ValidateFunctionName`)
  - `blueprintPath` (optional): Asset path of the target Blueprint (`/Game/Blueprints/BP_MyActor.BP_MyActor`);
    without it, the focused Blueprint. A path that does not resolve is an error (`ASSET_NOT_FOUND: ...`)
  - `parameters` (optional): Array of objects with `name` and `type` (required), `direction` (`input`, the default,
    or `output`, compared case-insensitively), `subType`, `container` (`none`, `array`, `set`, `map`), `keyType` (map
    key), `isReference`, `isConst`, and `defaultValue` (inputs only). `type` and `subType` are resolved like a
    variable's type (see "Type Identifiers" in `../Variables/AGENTS.md`): `type` alone takes a primitive, a type
    name (`HitResult`, `AActor`) or a path; `type` `object`, `class`, `softobject`, `softclass`, `struct`, `enum` or
    `interface` takes `subType` naming the class, struct or enum. Every parameter type is resolved before the function
    is created: one that does not resolve fails the call with
    `INVALID_PARAMETER_TYPE: parameter '<name>': <reason>` and nothing is added
  - `functionFlags` (optional): Object with `isPure`, `isCallInEditor`, `isStatic`, `isConst`,
    `isReliableReplication`, `isMulticast` (booleans), `category` (default `Default`), `keywords`, `tooltip`, and
    `accessSpecifier` (`public`, the default, `protected` or `private`). `isOverride` is read but has no effect
- **Returns**: `success`, `functionName`, `functionGuid` (32 hex digits), `blueprintName`, `blueprintPath`,
  `graphInfo` (`graphName`, `nodeCount`), `message`
- **Known issue (card 0280):** a parameter whose direction is neither `input` nor `output` is left out and only
  logged (`CreateFunctionParameters`); the call still reports success. Check the
  signature with list-blueprint-functions afterwards. `CreateNewGraph` also renames any other graph that already has
  the name (an event dispatcher, the event graph)

### open-blueprint-function
- **Description**: Opens a function graph in the Blueprint editor and focuses it
- **Parameters**:
  - `functionGuid` (required): See "Function GUIDs"
  - `blueprintPath` (optional): Blueprint to search. If it does not resolve, the focused Blueprint is searched instead
    (`FindFunctionByGuid`)
  - `centerView` (optional, default true): Accepted but has no effect (`CenterViewOnFunction` only returns true)
  - `selectNodes` (optional, default true): Accepted but has no effect (`SelectAllNodesInFunction` only returns true)
- **Returns**: `success`, `functionName`, `functionGuid`, `blueprintName`, `blueprintPath`, `graphName`,
  `editorState` (`opened`)

### delete-blueprint-function
- **Description**: Deletes a function graph, and with `force` the nodes that call it
- **Parameters**:
  - `functionGuid` (required)
  - `blueprintPath` (optional): Without it, the focused Blueprint
  - `force` (optional, default false): Also delete the function's call nodes in this Blueprint, and delete a function
    that child Blueprints override
- **Returns**: `success`, `deletedFunction` (`name`, `guid`), `referencesRemoved` (each `graphName`, `nodeId`,
  `nodeTitle`), `blueprintPath`, `transactionId`. `transactionId` is a new random GUID, not a handle to the undo
  transaction
- **Errors**: Call nodes in the same Blueprint without `force`: `Function '<name>' has <n> references and cannot be
  deleted. Use force=true to remove anyway.` `UserConstructionScript` and a few engine event names are protected
  (`IsSystemFunction`)
- **Known issue (card 0280):** references are found by function name only (`FindFunctionReferences`), so with
  `force` a call to a same-named function of another class is deleted too

### add-function-input-pin
- **Description**: Adds an input parameter to the focused function (a pin on its entry node)
- **Parameters**:
  - `pinName` (required): Name for the new input. A taken name is made unique by `CreateUserDefinedPin`: a second
    `Tag` becomes `Tag1`
  - `typeIdentifier` (required): A single type (no containers), resolved as in "Type Identifiers" in
    `../Variables/AGENTS.md`. A type that does not resolve fails the call with
    `Failed to resolve type '<type>': <reason>` before the function is touched
  - `defaultValue` (optional), `isPassByReference` (optional, default false), `tooltip` (optional)
- **Returns**: `success`, `pinName` (as requested), `actualName`, `displayName` (as the editor shows it, for example
  `Target Actor`), `pinId` (the pin GUID), `typeInfo` (`category`, `className`, `path`, `typeName`), `functionName`,
  `blueprintName`, `message`
- **Errors**: With an event graph focused, `Not in a function graph. Please focus on a Blueprint function.`

### add-function-return-pin
- **Description**: Adds a return value to the focused function (a pin on its result node; a result node is created
  when the function has none, `FN2CMcpFunctionPinUtils::EnsureFunctionResultNode`)
- **Parameters**: `pinName` (required), `typeIdentifier` (required, a single type), `tooltip` (optional). Return
  values take no default value and are never by reference
- **Returns**: As for add-function-input-pin (`bSuccess` is displayed as `Success`)
- **Known issue (card 0156):** only the first result node gets the pin, and the tooltip is stored on the pin
  (`SetPinTooltip`), so it is lost when the node is rebuilt
- **Known issue (new):** with an event graph focused the call fails with `Failed to create return pin`, but
  `EnsureFunctionResultNode` has already added a Return node to the event graph, and it stays there

### remove-function-entry-pin
- **Description**: Removes an input parameter (a user-defined pin on the entry node)
- **Parameters**: `pinName` (required): Matched as the exact pin name first, then case-insensitively against the pin
  name or the display name (`FN2CMcpFunctionPinUtils::RemoveFunctionPin`): `TargetActor`, `targetactor` and
  `Target Actor` all work. Use `actualName` or `displayName` from the add result
- **Returns**: `success`, `removedPinName` (as sent), `functionName`, `blueprintName`, `message`
- **Errors**: A pin that is not on the entry node (a return value, a misspelling): `Pin '<name>' not found. Available
  pins: ...`. The exec pin: `Cannot remove execution pin`. A pin that is not user-defined: `Cannot remove
  non-user-defined pin. ...`

### remove-function-return-pin
- **Description**: Removes a return value (a user-defined pin on the result node)
- **Parameters**: `pinName` (required), matched as for remove-function-entry-pin
- **Returns**: As for remove-function-entry-pin
- **Errors**: A function without a result node: `No function result node found. This function has no return values.`

## Example: Complex Function Creation
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

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

`Tag` becomes an output pin of the entry node; `FoundActors` and `Count` become input pins of the result node.

## Function Lifecycle Workflow

1. **Create**: `create-blueprint-function`; the new function graph is opened and focused
2. **Keep the GUID**: the returned `functionGuid` is valid until the function or its Blueprint is renamed or moved
3. **Open later**: `open-blueprint-function` with the GUID (from `list-blueprint-functions` if it may have changed)
4. **Edit the signature** (the function graph must be focused):
   - `add-function-input-pin` / `remove-function-entry-pin` for inputs (entry node)
   - `add-function-return-pin` / `remove-function-return-pin` for return values (result node)
5. **Implement**: use the Graph tools (`../Graph/AGENTS.md`) and the local-variable tools
6. **Delete**: `delete-blueprint-function`

## Example: Adding Function Input Pin

```bash
# A function graph must be focused
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
        "tooltip": "The actor to process"
      }
    },
    "id": 1
  }'
```

## Example: Adding Function Return Pin

```bash
# A function graph must be focused
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
# Removes an input parameter; return values need remove-function-return-pin
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

## Undo, Compile and Save

- Every tool here except `open-blueprint-function` opens an `FScopedTransaction`, so each edit is one editor undo
  step.
- **Known issue (card 0156):** the add-pin tools never call `Modify()` on the entry or result node before
  `CreateUserDefinedPin`, so undo leaves the parameter in `UserDefinedPins`; `EnsureFunctionResultNode` runs before
  the transaction opens; and call sites are rebuilt before the skeleton class is, so a function's callers can fail to
  compile after a signature change (`FN2CMcpFunctionPinUtils::UpdateFunctionCallSites`).
- Every successful edit compiles the Blueprint synchronously and marks its package dirty
  (`FN2CMcpBlueprintUtils::MarkBlueprintAsModifiedAndCompile`); add-function-return-pin included (card 0064).
  Nothing is saved: use `save-blueprint`.

## Implementation Notes

- New graphs come from `FBlueprintEditorUtils::CreateNewGraph` and `AddFunctionGraph`; result nodes from
  `FBlueprintEditorUtils::FindOrCreateFunctionResultNode` (create-blueprint-function) or `EnsureFunctionResultNode`
  (add-function-return-pin)
- Blueprint functions can return several values
- Pin removal breaks the pin's links before the pin is removed
- Execution pins and pins that are not user-defined cannot be removed (`ValidatePinForRemoval`)
