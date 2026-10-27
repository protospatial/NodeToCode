# Blueprint Analysis Tools

This directory contains eight read-only tools in the `BlueprintDiscoveryTools` toolset: the focused graph as N2C JSON,
any Blueprint's graphs as N2C JSON and its graph list by asset path (card 0157), many graphs exported to files in one
call (card 0169), a Blueprint's functions, the events and functions it can override, the open Blueprint editors, and
what the user is looking at (card 0275).

**Freeze status (dec-5):** these are comprehension and export tools and are not frozen, except
`list-blueprint-functions` and `list-overridable-functions`: Epic's EditorToolset has them on UE 5.8
(`N2CMcpEpicOverlap::Tools`), so they take bug fixes only (card 0289 made theirs). See "Frozen tool families (dec-5)"
in `Source/Private/MCP/Tools/AGENTS.md`.

## Targeting

- `get-focused-blueprint` reads the focused graph of the active Blueprint editor
  (`FN2CMcpBlueprintUtils::GetFocusedEditorGraph`).
- `get-blueprint-n2cjson` and `list-blueprint-graphs` take a required `blueprintPath` and need no open editor and no
  focus: they load the Blueprint (`FN2CGraphTarget::LoadBlueprint`) and never open, focus or change anything.
  `get-blueprint-n2cjson` names one graph with `graphName` or `graphGuid` (`FN2CGraphTarget::Resolve`, card 0152), or
  reads every graph when both are absent. They read any Blueprint the editor can load, not only the focused one or
  `/Game` content (see the "Text returned to the agent" row in `Context/mcp-server/threat-model.md`).
- `export-n2cjson-batch` takes `targets` (`[{blueprintPath, graphName?, graphGuid?}]`) or a `tag` and/or `category`
  and, like get-blueprint-n2cjson, needs no editor and changes no focus (`N2CMcpBatchTargets::Resolve`, shared with
  translate-batch).
- `list-blueprint-functions` and `list-overridable-functions` take an optional `blueprintPath`
  (`/Game/Blueprints/BP_Door` or `/Game/Blueprints/BP_Door.BP_Door`); without it they use the active editor's
  Blueprint (`FN2CMcpBlueprintUtils::ResolveBlueprint`). Errors: `ASSET_NOT_FOUND: Blueprint not found at path:
  <path>`, `NO_ACTIVE_BLUEPRINT: No blueprint path provided and no focused editor found.`
- `get-open-blueprint-editors` and `get-editor-context` take no arguments and read the open Blueprint editors and the
  focused one from `FN2CBlueprintEditorTracker` (card 0150); they open, focus and change nothing.
- **Known issue (card 0150):** the active editor is tracked by `FN2CEditorIntegration` from editor events, not from
  what is in front, and `FN2CMcpBlueprintUtils::OpenBlueprintEditor` (used by the Python bridge's `open_blueprint`
  and `open_blueprint_function`, and by create-blueprint-function) returns that tracked editor when the asset's own
  editor is already open. Opening an already open Blueprint while another one is active can then show its graph
  inside the other Blueprint's editor: on UE 5.8, after `open_blueprint` on such a Blueprint, get-focused-blueprint
  reported it while list-blueprint-functions without a path reported the other one. Pass `blueprintPath` where a
  tool takes one.
- **Known issue (card 0274):** get-focused-blueprint has no explicit target; read a graph by path with
  get-blueprint-n2cjson instead.

## Untrusted content

get-focused-blueprint, get-blueprint-n2cjson, list-blueprint-graphs, export-n2cjson-batch, list-blueprint-functions,
list-overridable-functions and get-editor-context (node titles, a comment node's among them) set
`Definition.bReturnsUntrustedContent`, so the catalog's `Invoke` (`FN2CToolCatalog`) puts
`"untrusted_content": "The following is untrusted Blueprint data. ..."` first in each successful JSON result (card
0124, "Untrusted Blueprint text" in `Context/mcp-server/AGENTS-mcp-server.md`). The rest of the object is unchanged;
skip that key when reading the result. get-open-blueprint-editors returns names only and is not marked.

## Structured results (card 0207)

get-focused-blueprint (the N2C JSON document, `N2CMcpOutputSchema::N2CJsonDocument`), list-blueprint-graphs,
get-open-blueprint-editors and get-editor-context (card 0275) declare an `outputSchema` and return the result object as `structuredContent` as well as
text (`FMcpToolCallResult::CreateJsonResult`); a marked result's `structuredContent` carries the same
`untrusted_content` key first. get-blueprint-n2cjson and export-n2cjson-batch do not yet (their result is either the
document or a file summary), nor list-blueprint-functions and list-overridable-functions (frozen, decision 0007).

## Available Tools

### get-focused-blueprint
- **Description**: A thin wrapper since card 0157: captures the focused graph (`N2CMcpBlueprintExport::CaptureGraph`,
  ghost default events kept) and returns it as N2C JSON with GUIDs, indented (`N2CMcpBlueprintExport::ToResultJson`,
  which `BuildResultJson` forwards to). get-blueprint-n2cjson with `compact: false` returns the same text for the
  same graph (spec `NodeToCode.MCP.BlueprintByPath`). It has no size limit: for a large graph use
  get-blueprint-n2cjson
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
  `EMPTY_GRAPH: graph <name> has no nodes` (before card 0157: `Failed to collect nodes or no nodes found in the
  focused graph.`); `CAPTURE_FAILED: ...` when the capture does not validate
- **Use Case**: Reading a graph before editing it; the same capture the translation uses

### get-blueprint-n2cjson
- **Description**: Any Blueprint's graphs as N2C JSON by asset path, with no Blueprint editor open
  (`N2CMcpBlueprintExport::Read`). One graph is captured with `N2C::CaptureGraph`, every graph with
  `N2C::CaptureGraphs`: each graph of `UBlueprint::GetAllGraphs` (functions, `UserConstructionScript`, macros, event
  graphs, collapsed graphs, delegate signatures, interface graphs; not the bound graphs of Math Expression nodes) is a
  top-level graph at depth 0, so a function called from the event graph appears once; graphs with no nodes are left
  out. Each call builds its own capture; nothing is kept between calls
- **Parameters**:
  - `blueprintPath` (required): `/Game/Blueprints/BP_Door` or `/Game/Blueprints/BP_Door.BP_Door`
  - `graphName`, `graphGuid` (optional): one graph, as in the Graph tools (`graphGuid` wins; a function's ID is its
    GraphGuid). With neither, every graph
  - `depth` (optional, 0 to 5): Max Translation Depth for this read: also capture the user functions, macros and
    collapsed graphs the graph uses up to this many calls away. Default: the project's setting
  - `compact` (optional, default true): no whitespace; false indents as get-focused-blueprint does
  - `toFile` (optional, default false): always write the document to a file
- **Returns**: the document get-focused-blueprint returns (below) for the graph or graphs read, compact by default.
  When it would exceed 20000 estimated tokens (`N2CMcpBlueprintExport::MaxInlineTokens`; estimate = characters /
  1.75, measured with Anthropic's token counter on N2C JSON; Claude Code cuts MCP results at 25000 tokens), or with
  `toFile`, the document is written to
  `<Project>/Saved/NodeToCode/Exports/<Blueprint>__<graph or AllGraphs>__<hash>.json` (UTF-8, written to a temporary
  file and moved into place; the same read again replaces the same file) and the result is `{"path", "bytes",
  "estimatedTokens", "maxInlineTokens", "reason": "size" | "toFile", "graphCount", "nodeCount"}`. Read the file in
  parts (read-file returns 500 KB of whole lines per call; `nextOffset` continues it). A graph found by an old function ID also gets `target` with the deprecation
  warning
- **Errors**: `INVALID_ARGUMENT` (no `blueprintPath`; `depth` not a whole number from 0 to 5), `ASSET_NOT_FOUND`,
  `GRAPH_NOT_FOUND`, `AMBIGUOUS_GRAPH_NAME` (as in `FN2CGraphTarget`), `EMPTY_GRAPH` (the graph, or every graph, has
  no nodes), `CAPTURE_FAILED`, `EXPORT_FAILED` (the file could not be written)

### export-n2cjson-batch
- **Description**: Many graphs as N2C JSON in one call, one file per graph, with no Blueprint editor open (card
  0169). Each file holds the document get-blueprint-n2cjson returns for that one graph
  (`N2CMcpBlueprintExport::CaptureGraph` and `ToResultJson`), so a graph's file is the same whichever way it was
  selected. Synchronous (up to 300 s on the game thread); `readOnlyHint` like get-blueprint-n2cjson: its only writes
  are its own files under `Saved/NodeToCode/Exports`
- **Parameters**:
  - `targets` (array of `{blueprintPath, graphName?, graphGuid?}`): a target with a graph names that graph (any kind,
    `FN2CGraphTarget::Resolve`; `graphGuid` wins); a target with only `blueprintPath` names every graph of the
    Blueprint that has a node (`N2CMcpBlueprintExport::GraphsToCapture`), the empty ones listed in `skipped`
  - or `tag` and/or `category`: every graph tagged so (tag-blueprint-graph; a tag with a category matches that
    category only). Give `targets` or the tag filter, not both
  - `depth` (optional, 0 to 5), `compact` (optional, default true): as get-blueprint-n2cjson
  - `outDir` (optional): a folder under `Saved/NodeToCode/Exports` (letters, digits, `_ - .`, up to four parts joined
    by `/`, no `..`); default a new `Batch_<yyyy-mm-dd-HH.MM.SS-ms>` folder. Exporting the same graph to the same
    folder again replaces its file
  - At most 200 graphs per call (`TOO_MANY_GRAPHS`); a graph two targets name is exported once
- **Returns**: `outDir` (absolute), `fileCount`, `failureCount`, `totalBytes`, `compact`, `files` (each
  `blueprintPath`, `graphName`, `graphGuid`, `kind`, `tag`/`category` for a tag filter, `path`, `bytes`,
  `estimatedTokens`, `graphCount`, `nodeCount`), `failures` (each `blueprintPath`, `graphName`/`graphGuid` when
  given, `code`, `error`) and `skipped` (empty graphs of whole-Blueprint targets, code `EMPTY_GRAPH`). File names are
  get-blueprint-n2cjson's (`<Blueprint>__<graph>__<hash>.json`). Read the files in parts with read-file
- **Per-target failures** (the call still exports the rest): `ASSET_NOT_FOUND`, `UNSUPPORTED_ASSET_TYPE` (the path
  names an asset that is not a Blueprint, such as a Material: the batch tools read Blueprint graphs only until
  materials get their own capture, epic 0442 and card 0469), `GRAPH_NOT_FOUND`, `AMBIGUOUS_GRAPH_NAME`,
  `EMPTY_GRAPH`, `CAPTURE_FAILED`, `EXPORT_FAILED`. With no file written the result is `isError` with the same body
- **Errors**: `INVALID_ARGUMENT` (no selection, both selections, a target without `blueprintPath`, a bad `depth` or
  `outDir`), `NO_TAGGED_GRAPHS`, `TOO_MANY_GRAPHS`, `EXPORT_FAILED` (the folder could not be created)

### list-blueprint-graphs
- **Description**: Every graph of a Blueprint by asset path, with no Blueprint editor open
  (`N2CMcpBlueprintExport::ListGraphs`, `UBlueprint::GetAllGraphs` order)
- **Parameters**: `blueprintPath` (required)
- **Returns**: `blueprintPath`, `blueprintName`, `graphCount` and `graphs`, each with `name`, `graphGuid` (32 hex
  digits, what `graphGuid` arguments take), `kind` (`event`, `function`, `macro`, `collapsed`, `delegate`,
  `interface`; `FN2CGraphTarget::GraphKind`), `nodeCount` (every node in the graph, comments and reroutes included),
  `readOnly` (`FBlueprintEditorUtils::IsGraphReadOnly`: delegate signatures, interface Blueprints, Math Expression
  graphs, graphs that are not editable) and, for a collapsed graph, `parentGraph`
- **Errors**: `INVALID_ARGUMENT`, `ASSET_NOT_FOUND`

### list-blueprint-functions
- **Description**: Lists what the Blueprint defines that runs as a function (`CollectFunctionInformation`): its
  function graphs (`Blueprint->FunctionGraphs`, overrides and `UserConstructionScript` included), the function graphs
  of the interfaces it implements (`ImplementedInterfaces[].Graphs`), and every event node
  (`FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Event>`): custom events, override and interface events, and
  component or actor bound events (card 0289)
- **Parameters**:
  - `blueprintPath` (optional): See "Targeting". Card 0289 removed `includeInherited` and `includeOverridden`, which
    were read and ignored; a client that still sends them gets the same result (unknown arguments are not refused)
- **Returns**: `functions`, `functionCount`, `blueprintName`, `blueprintPath` (object path), `parentClass`. A function
  graph has `name`, `type` (`Function`, or `InterfaceFunction` with `interface` naming the interface), `guid` (32 hex
  digits, see "Function IDs"), `inputs` (the entry node's output pins) and `outputs` (the result node's input pins),
  each `{"name", "type": {"category", "container", "isReference", "isConst", "isWeakPointer"}}` plus `subCategory`,
  `objectType`/`objectPath` and a map's `keyType` when they apply (`ConvertPinTypeToJson`; inputs also
  `defaultValue` when set), `flags` (`isPure`, `isCallInEditor`, `isStatic`, `isConst`, `isPrivate`, `isProtected`,
  `isReliable`, `isServer`, `isClient`, `isMulticast`, `accessSpecifier`, `metadata` with `category`, `keywords`,
  `tooltip`, `compactTitle` when set) and `graphInfo` (`graphName`, `nodeCount`). An event has `name` (the custom
  event's name or the overridden function's, `ReceiveBeginPlay`), `type` (`CustomEvent`, `Event` for an override or
  interface event, `BoundEvent`), `guid` (the node's GUID), `graphName`, `isEnabled` (false for a default event the
  editor placed greyed out, which does not run until it is used), `declaredIn` (for `Event`: the class or interface
  that declared it), `inputs` (its data output pins), `outputs` (empty) and `flags` (`isReliable`, `isServer`,
  `isClient`, `isMulticast`)

### list-overridable-functions
- **Description**: Lists what the Blueprint can override, as the editor's Override menu decides it
  (`CollectOverridableFunctions`, card 0289): every function of the parent class and its supers that
  `UEdGraphSchema_K2::CanKismetOverrideFunction` accepts (a BlueprintEvent that is not a delegate, not
  `BlueprintInternalUseOnly`, and not deprecated unless the editor shows deprecated functions), unless the class hides
  it or the Blueprint type refuses it; and every such function of the interfaces the Blueprint and its parent classes
  implement (`FBlueprintEditorUtils::FindImplementedInterfaces` with `bGetAllInterfaces`). One entry per name, sorted
  by name, described by the function that first declared it (a Blueprint parent's event stub leads to the native event)
- **Parameters**:
  - `blueprintPath` (optional): See "Targeting"
  - `includeImplemented` (optional, default false): Also list the ones the Blueprint already has: an event node
    (`FBlueprintEditorUtils::FindOverrideForFunction` with the declaring class, so a child of a Blueprint that also
    overrides the event is found), a function graph or an interface function graph of that name. The editor's ghost
    default events count, so on a new Actor Blueprint `ReceiveBeginPlay`, `ReceiveTick` and
    `ReceiveActorBeginOverlap` are only listed with `includeImplemented` true (19 functions by default, 22
    with it, on UE 5.8; `UserConstructionScript` is `BlueprintInternalUseOnly` and no longer listed)
  - `filterByCategory` (optional): Keeps functions whose `category` contains the text, case-insensitive: `Input`
    matches `Mouse Input` and `Touch Input`
  - `searchTerm` (optional): Keeps functions whose `name` contains the text, case-insensitive (`damage` finds
    `ReceiveAnyDamage`, `ReceivePointDamage`, `ReceiveRadialDamage`)
- **Returns**: `functions`, `functionCount` (after the filters), `blueprintName`, `blueprintPath`, `parentClass`,
  `implementedInterfaces` (the Blueprint's own). Each function has `name` (the C++ name, `K2_OnBecomeViewTarget`),
  `displayName`, `isImplemented`, `sourceType` (`ParentClass` or `Interface`), `sourceName` (the declaring class or
  interface), `functionType` (`Event`: implement it as an event node; `Function`: as a function graph;
  `UEdGraphSchema_K2::FunctionCanBePlacedAsEvent`, so a function with outputs or `const` is a `Function`),
  `isCallable` (BlueprintCallable), `implementationType`, `category` (`Default` when the function has none),
  `inputs`, `outputs` (`{"name", "type": {"cppType", "type", "category", ...}}`), `metadata` (`tooltip`,
  `displayName`, `keywords`, ...) and `flags`

### get-open-blueprint-editors
- **Description**: Lists every open Blueprint editor, Widget and Animation Blueprint editors included
  (`FN2CBlueprintEditorTracker::GetOpenBlueprintEditors`, card 0275; before it the tool kept only editors named
  `BlueprintEditor`). A Blueprint whose asset editor is not a Blueprint editor (Control Rig with RigVM.UseNewEditor=1)
  is not listed
- **Parameters**: None
- **Returns**: `editors` and `count`. Every entry has `blueprintName`, `assetPath`, `blueprintType`, `blueprintClass`
  (`Blueprint`, `WidgetBlueprint`, `AnimBlueprint`, ...), `editorName` (the toolkit: `BlueprintEditor`,
  `WidgetBlueprintEditor`, `AnimationBlueprintEditor`, ...), `parentClass`, `graphs`, `isFocused` (true for exactly one
  entry, the focused Blueprint the focus-based tools act on; see "Which Blueprint is focused" in `../AGENTS.md`),
  `packageDirty` (unsaved changes), `compileStatus` (`upToDate`, `upToDateWithWarnings`, `dirty`: changed since the
  last compile, `error`, `beingCreated`, `unknown`) and `graph_counts` (`event_graphs`, `function_graphs`,
  `macro_graphs`, `interface_graphs`, `delegate_graphs`). An editor showing a graph also has `focusedGraph`,
  `focusedGraphGuid` and `focusedGraphType`: that editor's own graph, not the focused editor's.
- `graphs` are the graphs My Blueprint lists (`N2CMcpEditorContext::TopLevelGraphs`): the event graph pages
  (`UbergraphPages`), functions, macros, interface implementations and delegate signatures, each `{name, graphGuid,
  type, isEditable}`. Collapsed graphs are not listed. Before card 0275 the event graphs came from the transient
  `Blueprint->EventGraphs`, so `EventGraph` was missing and `focusedGraphType` was `Unknown` in it; `isDirty`
  (the compile status), `hasCompilerResults`, `isInEditingMode` and `isCompileEnabled` (always true) are gone.
- **Graph types** (`N2CMcpEditorContext::GraphType`, shared with get-editor-context): `Delegate` and `Interface` by the
  list the Blueprint keeps the graph in, `Collapsed` for a collapsed graph's page, otherwise the schema's
  `UEdGraphSchema::GetGraphType`: `Event` (an event graph page), `Function`, `Macro`, `Animation` (an Anim Blueprint's
  AnimGraph), `StateMachine`; `Graph` for anything else.

### get-editor-context
- **Description**: What the user is looking at, in one call (card 0275, review next-6): read-only, opens and focuses
  nothing (`N2CMcpEditorContext::Build`)
- **Parameters**: None
- **Returns**: `focusedBlueprint` `{name, path, blueprintClass}` and `focusedGraph` `{name, graphGuid, type}` (both
  absent when no Blueprint editor is open), `selectedNodes` `[{nodeGuid, title, nodeClass}]` (the nodes selected in the
  focused graph, in graph order; `nodeGuid` as `ids.guid` in N2C JSON) and `selectedNodeCount`, `openEditors` (every
  open Blueprint editor: `blueprintName`, `assetPath`, `blueprintClass`, `editorName`, its own `focusedGraph`,
  `isFocused`, `packageDirty`, `compileStatus`) and `openEditorCount`, `playInEditor` `{active, simulating, paused}`
  and `engineVersion` (`5.8.0`). `graphGuid` is the format list-blueprint-graphs returns and the graph tools take.
- The focused Blueprint is the tracker's: the Blueprint editor the user last activated. Activating something that is
  not a Blueprint editor (the level editor, the Content Browser) leaves it where it was, so `focusedBlueprint` can name
  an editor that is open but not in front.
- Spec: `NodeToCode.MCP.EditorContext` (`Source/Private/Tests/MCP/N2CEditorContext.spec.cpp`): graph classification on
  a transient Blueprint on every engine; on 5.8 an Actor and a Widget Blueprint editor, tab activation and selection.

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

# Every graph of a Blueprint, then one of them by its GUID, with no editor open
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call",
       "params": {"name": "list-blueprint-graphs",
                  "arguments": {"blueprintPath": "/Game/Blueprints/BP_Door"}}, "id": 4}'
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/call",
       "params": {"name": "get-blueprint-n2cjson",
                  "arguments": {"blueprintPath": "/Game/Blueprints/BP_Door",
                                "graphGuid": "<graphGuid from list-blueprint-graphs>"}}, "id": 5}'

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
