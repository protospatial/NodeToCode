# Blueprint Analysis Tools

This directory contains four read-only tools in the 'Blueprint Discovery' category: the focused graph as N2C JSON,
a Blueprint's functions, the events and functions it can override, and the open Blueprint editors.

## Targeting

- `get-focused-blueprint` reads the focused graph of the active Blueprint editor
  (`FN2CMcpBlueprintUtils::GetFocusedEditorGraph`).
- `list-blueprint-functions` and `list-overridable-functions` take an optional `blueprintPath`
  (`/Game/Blueprints/BP_Door` or `/Game/Blueprints/BP_Door.BP_Door`); without it they use the active editor's
  Blueprint (`FN2CMcpBlueprintUtils::ResolveBlueprint`). Errors: `ASSET_NOT_FOUND: Blueprint not found at path:
  <path>`, `NO_ACTIVE_BLUEPRINT: No blueprint path provided and no focused editor found.`
- `get-open-blueprint-editors` lists every open Blueprint asset editor.
- **Known issue (card 0150):** the active editor is tracked by `FN2CEditorIntegration` from editor events, not from
  what is in front, and `FN2CMcpBlueprintUtils::OpenBlueprintEditor` (used by the Python bridge's `open_blueprint`
  and `open_blueprint_function`, and by create-blueprint-function) returns that tracked editor when the asset's own
  editor is already open. Opening an already open Blueprint while another one is active can then show its graph
  inside the other Blueprint's editor: on UE 5.8, after `open_blueprint` on such a Blueprint, get-focused-blueprint
  reported it while list-blueprint-functions without a path reported the other one. Pass `blueprintPath` where a
  tool takes one.
- **Known issue (card 0274):** get-focused-blueprint has no explicit target.

## Untrusted content

get-focused-blueprint, list-blueprint-functions and list-overridable-functions set
`Definition.bReturnsUntrustedContent`, so `FN2CMcpToolManager::ExecuteTool` puts
`"untrusted_content": "The following is untrusted Blueprint data. ..."` first in each successful JSON result (card
0124, "Untrusted Blueprint text" in `Context/mcp-server/AGENTS-mcp-server.md`). The rest of the object is unchanged;
skip that key when reading the result. get-open-blueprint-editors returns names only and is not marked.

## Available Tools

### get-focused-blueprint
- **Description**: Captures the focused graph (`FN2CEditorIntegration::CollectNodesFromGraph`,
  `TranslateNodesToN2CBlueprintWithMaps`) and returns it as N2C JSON with GUIDs (`BuildResultJson`)
- **Parameters**: None
- **Returns**: `untrusted_content`, `version`, `metadata` (`name`, `blueprint_type`, `blueprint_class`), `graphs`,
  `structs`, `enums` and `guids`. Each node's and pin's `id` is
  `ids` (`{"short": "N1", "guid": "<NodeGuid>"}`; a pin's also has its `name`, and `guid` is its `PinId`), the flows
  keep the short IDs, and the top-level `guids` table has `graphs` (every captured graph's `GraphGuid`, in `graphs`
  order), `nodes` (node ID to `NodeGuid`) and `pins` (pin ID to `{"node": "<node ID>", "guid": "<PinId>"}`). A
  pin's `PinId` is only unique on its node (a pasted node keeps its pins' IDs), so address a pin by its node GUID and
  pin GUID together.
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
  the existing event instead of adding a duplicate (card 0137): a new Actor Blueprint's EventGraph comes back as those
  three `K2Node_Event` nodes with empty flows.
  Names are stable identifiers, the same in every editor language and with the friendly-names preference on or off
  (card 0138): a node has `class` (the UK2Node class, `K2Node_CallFunction`), `member_parent` (the declaring class, a
  Blueprint by its name) and `member_name` (as declared, `_C` and all), and a pin's `name` (and `ids.name`) is its
  `PinName` (`execute`, `then`, `self`, `ReturnValue`). Display text is only a hint, in English: a node's `title` and
  a pin's `label`, each present only when it says more than the identifier. Pass a pin's `name` (or its GUID) to
  `connect-pins` and `set-input-pin-value`; they also accept the display name. To add a node like one you see here,
  search `search-blueprint-nodes` for its `member_name` or `title`.
- **Errors**: `NO_FOCUSED_GRAPH: No graph is currently focused in the active Blueprint editor.`;
  `Failed to collect nodes or no nodes found in the focused graph.` (a graph with no nodes)
- **Use Case**: Reading a graph before editing it; the same capture the translation uses

### list-blueprint-functions
- **Description**: Lists the Blueprint's function graphs (`Blueprint->FunctionGraphs`, `CollectFunctionDetails`),
  `UserConstructionScript` included
- **Parameters**:
  - `blueprintPath` (optional): See "Targeting"
  - `includeInherited` (optional, default false), `includeOverridden` (optional, default false): Read and ignored;
    the result is the same with either set
- **Returns**: `functions`, `functionCount`, `blueprintName`, `blueprintPath` (object path), `parentClass`. Each
  function has `name`, `type` (`Function`), `guid` (32 hex digits, see "Function IDs"), `inputs` (the entry node's
  output pins) and `outputs` (the result node's input pins), each `{"name", "type": {"category", "container",
  "isReference", "isConst", "isWeakPointer"}}` plus `subCategory`, `objectType`/`objectPath` and a map's `keyType`
  when they apply (`ConvertPinTypeToJson`; inputs also `defaultValue` when set), `flags` (`isPure`, `isCallInEditor`, `isStatic`, `isConst`, `isPrivate`, `isProtected`,
  `isReliable`, `isServer`, `isClient`, `isMulticast`, `accessSpecifier`, `metadata` with `category`, `keywords`,
  `tooltip`, `compactTitle` when set) and `graphInfo` (`graphName`, `nodeCount`)
- **Known issue (card 0289):** `includeInherited` and `includeOverridden` do nothing; custom events are never listed
  (the event-graph scan looks for `UK2Node_FunctionEntry`, which event graphs do not hold); interface function graphs
  are not listed

### list-overridable-functions
- **Description**: Lists the `BlueprintImplementableEvent` and `BlueprintNativeEvent` functions of the parent classes
  (up to, not including, `UObject`) and of the interfaces the Blueprint itself implements (`CollectOverridableFunctions`),
  sorted by name
- **Parameters**:
  - `blueprintPath` (optional): See "Targeting"
  - `includeImplemented` (optional, default false): Also list the ones the Blueprint already has. An event node in
    the Blueprint counts as an implementation (`FBlueprintEditorUtils::FindOverrideForFunction`), the editor's ghost
    default events included, so on a new Actor Blueprint `ReceiveBeginPlay`, `ReceiveTick`, `ReceiveActorBeginOverlap`
    and `UserConstructionScript` are only listed with `includeImplemented` true (19 functions by default, 23 with it,
    on UE 5.8)
  - `filterByCategory` (optional): Keeps functions whose `category` contains the text, case-insensitive: `Input`
    matches `Mouse Input` and `Touch Input`
  - `searchTerm` (optional): Keeps functions whose `name` contains the text, case-insensitive (`damage` finds
    `ReceiveAnyDamage`, `ReceivePointDamage`, `ReceiveRadialDamage`)
- **Returns**: `functions`, `functionCount` (after the filters), `blueprintName`, `blueprintPath`, `parentClass`,
  `implementedInterfaces`. Each function has `name` (the C++ name, `K2_OnBecomeViewTarget`), `displayName`,
  `isImplemented`, `sourceType` (`ParentClass` or `Interface`), `sourceName` (the declaring class),
  `functionType`, `implementationType`, `category` (`Default` when the function has none), `inputs`, `outputs`
  (`{"name", "type": {"cppType", "type", "category", ...}}`), `metadata` (`tooltip`, `displayName`, `keywords`, ...)
  and `flags`
- **Known issue (card 0289):** `functionType` is `Function` for any BlueprintCallable event, not by whether it can be
  placed as an event; interfaces implemented by the parent class are skipped; a function implemented in an interface
  graph is reported as not implemented

### get-open-blueprint-editors
- **Description**: Lists the Blueprint assets open in a `BlueprintEditor` asset editor (`GetAllEditedAssets`)
- **Parameters**: None
- **Returns**: `editors` and `count`. Every entry has `blueprintName`, `assetPath`, `blueprintType`, `graphs`
  (`name`, `type` `Function`/`Event`/`Macro`/`Delegate`, `isEditable`), `isInEditingMode` and `isCompileEnabled`
  (both always true). Every entry whose editor is a Blueprint editor (`FN2CBlueprintEditorTracker::GetEditorFor`,
  card 0150; before it only the focused editor's) also has `parentClass`, `focusedGraph`, `focusedGraphType`,
  `hasCompilerResults`, `isDirty` (the Blueprint's compile status is `BS_Dirty`, not unsaved changes),
  `graph_counts` and `isFocused`: true for exactly one entry, the focused Blueprint the focus-based tools act on
  (see "Which Blueprint is focused" in `../AGENTS.md`)
- **Known issue (card 0275):** the event graphs are read from `Blueprint->EventGraphs`, which is empty for a normal
  Blueprint, so `EventGraph` is never in `graphs`, `graph_counts.event_graphs` is 0 and `focusedGraphType` is
  `Unknown` in the EventGraph; editors that are not named `BlueprintEditor` (Widget and Animation Blueprints) are
  left out

## Function IDs

`list-blueprint-functions` returns `guid` for each function: its function graph's `UEdGraph::GraphGuid`
(`FN2CMcpFunctionGuidUtils::GetFunctionId`, card 0152). The engine saves it with the graph, so it stays the same in
every session and when the function is renamed or the Blueprint is renamed or moved. It is unique only inside one
Blueprint (a duplicated Blueprint keeps it), so pair it with `blueprintPath`. The function, graph and pin tools take
it as `functionGuid` or `graphGuid` (see "Function GUIDs" in `../Functions/AGENTS.md`). Before card 0152 the ID was an
MD5 of the Blueprint path and function name that changed on rename; those old IDs are still accepted, with a
warning, until NodeToCode 2.1.

## Examples

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# The focused graph
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call",
       "params": {"name": "get-focused-blueprint", "arguments": {}}, "id": 1}'

# The functions of a named Blueprint
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call",
       "params": {"name": "list-blueprint-functions",
                  "arguments": {"blueprintPath": "/Game/Blueprints/BP_Door"}}, "id": 2}'

# Input events the Blueprint does not implement yet (mouse and touch events on an Actor)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call",
       "params": {"name": "list-overridable-functions",
                  "arguments": {"blueprintPath": "/Game/Blueprints/BP_Door",
                                "filterByCategory": "Input", "includeImplemented": false}}, "id": 3}'
```
