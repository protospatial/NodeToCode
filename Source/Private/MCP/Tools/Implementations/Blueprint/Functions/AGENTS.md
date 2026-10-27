# Blueprint Function Management Tools

This directory contains the MCP tools in the `BlueprintFunctionTools` toolset: creating, opening and deleting
Blueprint functions and editing their parameters and return values.

## Status: frozen, bug fixes only (dec-5)

Every tool here (`BlueprintFunctionTools`) is frozen. Decision record [0007](../../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b).

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in this family, and new arguments, modes or result fields that
  add capability. On UE 5.8 that work belongs to Epic's EditorToolset (`BlueprintTools.add_function_graph`, `add_function_param`, `remove_function_param`); on 5.6 and 5.7 it waits for the
  owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.
- **Fix cards still owed here:** none. 0280 (create- and delete-blueprint-function renamed or deleted unrelated
  graphs and nodes) and 0295 (delete-blueprint-function's toast icon, now `FN2CMcpNotification`) are done.

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

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

A function's ID (`functionGuid` from create-blueprint-function, `guid` from `list-blueprint-functions`) is its
function graph's `UEdGraph::GraphGuid` (`FN2CMcpFunctionGuidUtils::GetFunctionId`, card 0152). The engine saves it
with the graph and keeps it when the function is renamed and when the Blueprint is renamed or moved, so an agent can
keep using the ID it got first. It is unique only inside one Blueprint (a duplicated Blueprint keeps its graphs'
GUIDs), so pass `blueprintPath` with it when the function's Blueprint is not the focused one. Both the 32-digit form
the tools return and the hyphenated form are accepted (`FGuid::Parse`).

Every function tool here takes the ID: `functionGuid` on open-blueprint-function and delete-blueprint-function, and
`graphGuid` on all of them (the same value), with `blueprintPath` and `graphName` as in "Targeting and IDs" in
`../Graph/AGENTS.md`. The pin tools act on that function, or on the focused function without one.

- **Deprecated, accepted until NodeToCode 2.1:** the IDs the tools returned before card 0152 were
  MD5(`<Blueprint path name>::<function name>`) and changed on every rename or move. Such an ID still finds its
  function while the function and its Blueprint keep the name and path it was made from; the result's `target` then
  has `deprecatedId` (the ID sent) and `warning` (naming the stable ID to use instead), and the editor log warns.
  NodeToCode 2.1 removes this fallback (`FN2CMcpFunctionGuidUtils::LegacyIdRemovalVersion`).
- A function graph saved without a valid `GraphGuid` (never seen in practice; `UEdGraph::PostInitProperties` gives
  every graph one) keeps the old hash as its ID.

## Available Tools

### create-blueprint-function
- **Description**: Creates a function graph, adds its parameters, compiles, and opens and focuses the new graph in the
  Blueprint editor
- **Parameters**:
  - `functionName` (required): Name of the new function, checked by the editor's own `FKismetNameValidator`
    (`ValidateFunctionName`, card 0280). A name any graph of the Blueprint has (the event graph, a function, a macro,
    an event dispatcher), a member variable, a component or a function of the class or its parents (`ReceiveBeginPlay`
    included) is refused: `NAME_COLLISION: 'OnDied' is already an event dispatcher in BP_Door. Pick another name, for
    example 'OnDied_0'.` (the suggestion is `FBlueprintEditorUtils::FindUniqueKismetName`). Too long or bad
    characters: `INVALID_NAME: ...`. Before card 0280 only function graphs and non-event parent functions were
    checked, and `CreateNewGraph` renamed any other graph of that name out of the way (`EdGraph_0`), which broke an
    event dispatcher or renamed the event graph
  - `blueprintPath` (optional): Asset path of the target Blueprint (`/Game/Blueprints/BP_MyActor.BP_MyActor`);
    without it, the focused Blueprint. A path that does not resolve is an error (`ASSET_NOT_FOUND: ...`). It takes
    no `graphName` or `graphGuid` (it makes the graph)
  - `parameters` (optional): Array of objects with `name` and `type` (required), `direction` (`input`, the default,
    or `output`, compared case-insensitively), `subType`, `container` (`none`, `array`, `set`, `map`), `keyType` (map
    key), `isReference`, `isConst`, and `defaultValue` (inputs only). `type` and `subType` are resolved like a
    variable's type (see "Type Identifiers" in `../Variables/AGENTS.md`): `type` alone takes a primitive, a type
    name (`HitResult`, `AActor`) or a path; `type` `object`, `class`, `softobject`, `softclass`, `struct`, `enum` or
    `interface` takes `subType` naming the class, struct or enum. A taken name is made unique, as in the editor (a
    second `Value` becomes `Value1`); the result's `parameters` says which name each got
  - `functionFlags` (optional): Object with `isPure`, `isCallInEditor`, `isStatic`, `isConst` (booleans), `category`
    (default `Default`), `keywords`, `tooltip`, and `accessSpecifier` (`public`, the default, `protected` or
    `private`, case-insensitive). The access specifier replaces the `FUNC_Public` that `AddFunctionGraph` seeds (the
    `FUNC_AccessSpecifiers` mask is cleared first, as the details panel does); before card 0280 a private function
    had both `FUNC_Public` and `FUNC_Private`
- **Refused before the Blueprint is touched** (card 0280; no undo step, the package stays clean). A cancelled
  transaction does not undo what was done inside it, so every check runs before the graph is made:
  - A parameter `direction` other than `input` or `output` (`return`, `out`): `INVALID_PARAMETER: parameter
    '<name>': direction '<d>' is neither 'input' nor 'output' ...`. A `defaultValue` on an output, or a parameter
    without a name: `INVALID_PARAMETER`. Before card 0280 such a parameter was left out and the call reported success
  - A `type` that does not resolve: `INVALID_PARAMETER_TYPE: parameter '<name>': <reason>`
  - A `defaultValue` the pin would refuse, checked on a scratch pin of the parameter's type with the same
    `FN2CMcpPinDefault::Prepare` set-input-pin-value uses (`abc` for an int, `maybe` for a bool, any default on an
    array, set, map or by-reference input): `INVALID_DEFAULT_VALUE: parameter '<name>' refused the default ...`
  - `isStatic` outside a Blueprint Function Library (whose functions are static already), `isOverride` (it was read
    and ignored; override a parent function from the editor's Functions > Override menu), and `isReliableReplication`
    or `isMulticast` (a Blueprint function cannot be an RPC; only a custom event can be replicated), or an unknown
    `accessSpecifier`: `INVALID_FLAGS: ...`
- Defaults go through `UK2Node_EditablePinBase::ModifyUserDefinedPinDefaultValue` on the pin `CreateUserDefinedPin`
  returned, so they are kept in the entry's `UserDefinedPins` and survive every node rebuild. Before card 0280 the
  default was written to `FindPin(name)` (the first `Value` when the second was renamed `Value1`) and only on the pin
- **Returns**: `success`, `functionName`, `functionGuid` (32 hex digits; the stable ID, see "Function GUIDs"),
  `blueprintName`, `blueprintPath`, `graphInfo` (`graphName`, `nodeCount`), `parameters` (each pin made: `name` as
  sent, `actualName`, `pinId`, `direction`, and `defaultValue` as stored), `warnings` (only when a checked parameter
  still could not be made), `message`, and `target` (the new function as `blueprintPath` plus `graphGuid`, for the
  graph and pin tools)

### open-blueprint-function
- **Description**: Opens a function graph in the Blueprint editor and focuses it
- **Parameters**:
  - `functionGuid` (or `graphGuid`), or `graphName`: The function; one of them is required. See "Function GUIDs"
  - `blueprintPath` (optional): Blueprint to search; without it, the focused Blueprint. A path that does not load is
    an error (`ASSET_NOT_FOUND`); before card 0152 it fell back to the focused Blueprint. A graph that is not a
    function: `NOT_A_FUNCTION_GRAPH` (open other graphs with `open-blueprint-asset` and `focus_graph`)
  - `centerView` (optional, default true): Accepted but has no effect (`CenterViewOnFunction` only returns true)
  - `selectNodes` (optional, default true): Accepted but has no effect (`SelectAllNodesInFunction` only returns true)
- **Returns**: `success`, `functionName`, `functionGuid` (the stable ID, also when an old one was sent),
  `blueprintName`, `blueprintPath`, `graphName`, `editorState` (`opened`), `target`

### delete-blueprint-function
- **Description**: Deletes a function graph, and with `force` the nodes that call it
- **Parameters**:
  - `functionGuid` (or `graphGuid`), or `graphName`: The function; one of them is required
  - `blueprintPath` (optional): Without it, the focused Blueprint. Functions of an interface Blueprint can be deleted
    (the resolver's read-only check does not apply to a delete)
  - `force` (optional, default false): Delete the function although it is called or overridden: its call nodes in
    this Blueprint are removed (`FBlueprintEditorUtils::RemoveNode`); call nodes in other Blueprints are left in place
    and reported, and those Blueprints then fail to compile until they are fixed
- **References** (`FindFunctionReferences`, card 0280): a call node counts when the function it resolves to
  (`UK2Node_CallFunction::GetTargetFunction`) belongs to this Blueprint's skeleton or generated class, never by name
  alone, so a call to another class's same-named function (`UKismetSystemLibrary::PrintString` beside a function
  `PrintString`) is neither counted nor deleted. Every loaded Blueprint is searched (child Blueprints and any
  Blueprint calling it through a reference), and Blueprints the asset registry lists as hard referencers of this one
  that are not loaded are reported as unchecked. Before card 0280 references were matched by bare name, in the owning
  Blueprint only, so `force` deleted unrelated call nodes and a call from another Blueprint was missed
- **Returns**: `success`, `deletedFunction` (`name`, `guid`), `referencesRemoved` (each `graphName`, `nodeId`,
  `nodeTitle`), `externalReferencesLeft` (with `force`, call nodes in other Blueprints: `blueprintPath` and the same fields)
  and `uncheckedReferencers` (with `force`, object paths of referencing Blueprints that were not loaded), each only
  when not empty, `blueprintPath`, `target` (the deleted function, taken before the delete)
- **Errors**: References without `force`, checked before anything changes (no undo step, the package stays clean):
  `Function '<name>' has references and cannot be deleted: 1 call node(s) in BP_Door; 2 call node(s) in other
  Blueprints (/Game/BP_Player.BP_Player), which force does not remove: they stop compiling. Use force=true to remove
  anyway.` `UserConstructionScript` and a few engine event names are protected (`IsSystemFunction`)
- The Blueprint editor's tabs on the function and its subgraphs are closed before `RemoveGraph` (as My Blueprint's
  delete does), so no tab is left on a graph the Blueprint no longer owns. The graph resolver refuses a focused graph
  that belongs to no Blueprint (`NO_FOCUSED_GRAPH: the focused graph ... belongs to no Blueprint`). Before card 0280
  the tab stayed open and the focused-graph tools edited the removed graph

### add-function-input-pin
- **Description**: Adds an input parameter to the target function (a pin on its entry node): the function
  `blueprintPath` plus `graphName` or `graphGuid` names, else the focused one. A read-only function graph is refused
  (`READ_ONLY_GRAPH`), and so is a graph that is not a function (`NOT_A_FUNCTION_GRAPH`); the four pin tools share
  this, and their results carry `target`
- **Parameters**:
  - `pinName` (required): Name for the new input. A taken name is made unique by `CreateUserDefinedPin`: a second
    `Tag` becomes `Tag1`
  - `typeIdentifier` (required): A single type (no containers), resolved as in "Type Identifiers" in
    `../Variables/AGENTS.md`. A type that does not resolve fails the call with
    `Failed to resolve type '<type>': <reason>` before the function is touched
  - `defaultValue` (optional), `isPassByReference` (optional, default false)
  - `tooltip` (optional): Kept in the function's tooltip metadata as an `@param <actualName> <text>` line
    (`FN2CMcpFunctionPinUtils::SetParameterTooltip`), which the engine shows as the pin's tooltip on call nodes and
    leaves out of the function's own tooltip. Before card 0156 it went to the pin itself and was lost on the next
    node rebuild. Removing the pin drops its line
- **Returns**: `success`, `pinName` (as requested), `actualName`, `displayName` (as the editor shows it, for example
  `Target Actor`), `pinId` (the pin GUID), `typeInfo` (`category`, `className`, `path`, `typeName`), `functionName`,
  `blueprintName`, `message`
- **Errors**: An event graph (focused or named): `NOT_A_FUNCTION_GRAPH: EventGraph (event graph) of BP_Door is not a
  function. ...`
- Every call node of the function, in this Blueprint and in other loaded Blueprints, gets the new input with its links
  kept; see "Signature Edits and Call Sites"

### add-function-return-pin
- **Description**: Adds a return value to the target function: the same pin on every result (Return) node of the
  function, so a function with an early-out Return keeps matching Return nodes. A function with no result node gets
  one from `FBlueprintEditorUtils::FindOrCreateFunctionResultNode` (as My Blueprint > Outputs does): it has a node
  GUID, is transactional, sits right of the entry and is wired from the entry's exec output when that is free
- **Parameters**: `pinName` (required), `typeIdentifier` (required, a single type), `tooltip` (optional, kept as for
  add-function-input-pin). Return values take no default value and are never by reference
- **Returns**: As for add-function-input-pin (`bSuccess` is displayed as `Success`); `pinId` is the pin on the first
  Return node
- An event graph is refused with `NOT_A_FUNCTION_GRAPH` before anything changes. Before card 0156 only the first
  result node got the pin (removing it then failed the compile with "Return nodes don't match each other"), and a
  created result node had no GUID and no exec wire and was made outside the undo transaction

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
- **Description**: Removes a return value from every result node of the function
- **Parameters**: `pinName` (required), matched as for remove-function-entry-pin (against the first result node)
- **Returns**: As for remove-function-entry-pin
- **Errors**: A function without a result node: `No function result node found. This function has no return values.`
- A refused removal on either remove tool (no such pin, the exec pin, a pin that is not user-defined, no result node)
  is checked before anything changes: no undo transaction, and the package stays clean. Before card 0156 it dirtied
  the package

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
2. **Keep the GUID**: the returned `functionGuid` stays valid when the function or its Blueprint is renamed or moved
3. **Open later**: `open-blueprint-function` with the GUID (and `blueprintPath`), or skip opening: every tool below
   takes `blueprintPath` plus `graphGuid`
4. **Edit the signature** (the function `blueprintPath` plus `graphGuid` names, or the focused one):
   - `add-function-input-pin` / `remove-function-entry-pin` for inputs (entry node)
   - `add-function-return-pin` / `remove-function-return-pin` for return values (result node)
5. **Implement**: use the Graph tools (`../Graph/AGENTS.md`) and the local-variable tools
6. **Delete**: `delete-blueprint-function`

## Example: Adding Function Input Pin

```bash
# The function blueprintPath plus graphGuid (its functionGuid) names; without them, the focused function
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "add-function-input-pin",
      "arguments": {
        "blueprintPath": "/Game/Blueprints/BP_MyActor.BP_MyActor",
        "graphGuid": "<functionGuid from create-blueprint-function>",
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
# The focused function (or pass blueprintPath and graphGuid)
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
- The four pin tools call `Modify()` on the Blueprint, the function graph, the entry node and every result node they
  edit before changing them, so one undo restores the pins, `UserDefinedPins`, a result node the call created, the
  tooltip metadata and every call site the edit rebuilt (card 0156).
- Every successful edit marks the Blueprint structurally modified and its package dirty
  (`FN2CMcpBlueprintUtils::MarkModified`); the tool call then compiles it once, after the edit's transaction has
  closed, and adds the compiler's diagnostics to the result as `compile` (card 0151; fields in the Graph folder's
  AGENTS.md). One deferred `FBlueprintActionDatabase::RefreshAssetActions` per changed Blueprint follows a couple of
  frames later, shared by every edit made before it runs. Nothing is saved: use `save-blueprint`.

## Signature Edits and Call Sites

The four pin tools and the Python bridge's `add_function_input_pin`, `add_function_return_pin`,
`remove_function_entry_pin` and `remove_function_return_pin` share one path, `FN2CMcpFunctionPinUtils::AddFunctionPin`
and `RemoveFunctionPin` (card 0156), the same steps as the Blueprint editor's My Blueprint > Inputs / Outputs
(`FBlueprintGraphActionDetails::OnAddNewOutputClicked`, `OnParamsChanged`):

1. Validate (type, name, the pin to remove, `CanCreateUserDefinedPin` on each node) before anything changes. A refused
   edit opens no transaction and leaves the package clean
2. Open the transaction; `Modify()` the Blueprint, the graph, the entry and every edited result node
3. Add or remove the pin on the entry node, or on every result node under one name
4. `ReconstructNode` each edited node, then `UEdGraphSchema_K2::HandleParameterDefaultValueChanged`: it regenerates the
   skeleton class first and only then rebuilds the function's call nodes (and delegate and macro nodes) in every
   loaded Blueprint (`FParamsChangedHelper`, which `Modify()`s each one), so they get the new signature and keep the
   links of the pins that did not change. Before card 0156 the call nodes were rebuilt by name before the skeleton
   was regenerated (`UpdateFunctionCallSites`), so they kept the old pins and the Blueprint failed to compile with
   "Could not find a pin for the parameter"
5. `FN2CMcpBlueprintUtils::MarkModified`: the package is dirty and the tool call compiles the Blueprint once after the
   transaction closes, returning the diagnostics as `compile`

A removed parameter that is still wired at a call site leaves an orphaned pin on that call node, as in the editor, and
the compile reports it. No tool changes a parameter's type (owner decision 0140, provisional: no new tool arguments);
remove the pin and add it again.

Checked by `NodeToCode.MCP.FunctionPinEdits` (the tools on every engine, the bridge on a focused editor on UE 5.8),
`NodeToCode.Robustness.MarkModifiedDirties` and the `FunctionPinEdits` case of `NodeToCode.Capture.Golden`.

## Implementation Notes

- New graphs come from `FBlueprintEditorUtils::CreateNewGraph` and `AddFunctionGraph`; result nodes from
  `FBlueprintEditorUtils::FindOrCreateFunctionResultNode` (create-blueprint-function and add-function-return-pin)
- Blueprint functions can return several values
- Pin removal breaks the pin's links before the pin is removed
- Execution pins and pins that are not user-defined cannot be removed (`ValidatePinForRemoval`)
- create-blueprint-function's refusals and delete-blueprint-function's reference matching, `force` and tab closing are
  checked by `NodeToCode.MCP.FunctionCreateDelete` (card 0280; the cases that open the Blueprint editor run on UE 5.8)
