# Blueprint Graph Manipulation Tools

This directory contains the MCP tools that find, add, wire, set, comment and delete nodes in a Blueprint graph:
`search-blueprint-nodes` ('Blueprint Discovery'), `add-bp-node-to-active-graph`, `connect-pins`,
`set-input-pin-value`, `delete-blueprint-node`, `create-comment-node` ('Blueprint Graph Editing') and
`find-nodes-in-graph` (no category, so dynamic tool discovery cannot enable it).

## Targeting and IDs

Every tool here acts on one graph, chosen by the shared graph resolver (`FN2CGraphTarget`,
`MCP/Utils/N2CMcpGraphTarget.h`, card 0152) from three optional arguments every tool here takes:

- `blueprintPath`: the Blueprint's asset path, `/Game/Blueprints/BP_Door.BP_Door` (or `/Game/Blueprints/BP_Door`);
- `graphName`: a graph of that Blueprint by name, ignoring case: the event graph (`EventGraph`), a function, a macro,
  a collapsed graph, a delegate signature or an interface graph (every graph `UBlueprint::GetAllGraphs` returns);
- `graphGuid`: a graph by its `UEdGraph::GraphGuid`, 32 hex digits or hyphenated (`target.graphGuid` of an earlier
  result). A function's `functionGuid` is its graph GUID. `graphGuid` wins over `graphName` when both are given.

Which graph:

- none of the three: the graph focused in the Blueprint editor (as before card 0152);
- `graphName` or `graphGuid` without `blueprintPath`: that graph of the focused Blueprint;
- `blueprintPath` alone: that Blueprint's focused graph when one of its graphs is focused, else its event graph.

A `GraphGuid` is unique only inside one Blueprint (a duplicated Blueprint keeps its graphs' GUIDs), so an agent that
works without the editor's focus, or next to other agents, passes `blueprintPath` with `graphGuid`. No editor needs to
be open: `NodeToCode.MCP.GraphTarget` runs these tools on a Blueprint built in code. `add-bp-node-to-active-graph`
keeps its name, but adds to the target graph like the others.

Every result after the graph has resolved carries `target`: `blueprintPath`, `blueprintName`, `graphName`,
`graphGuid`, `graphKind` (`event`, `function`, `macro`, `collapsed`, `delegate`, `interface` or `graph`) and
`resolvedBy` (`explicit` for a graph named by `graphName` or `graphGuid`, `focus`, or `eventGraph` for
`blueprintPath` alone). It is a field of the JSON result, or a second text item `{"target": {...}}` after an error
text. A function found by an old name-hash ID adds `deprecatedId` and `warning` (see "Function GUIDs" in
`../Functions/AGENTS.md`).

Errors from the resolver (the result text starts with the code):

- `ASSET_NOT_FOUND`: `blueprintPath` loads no Blueprint;
- `GRAPH_NOT_FOUND`: no graph of that name or GUID (`GRAPH_NOT_FOUND: no graph 'Foo' in /Game/BP_Door.BP_Door.
  Graphs: EventGraph (event, <GUID>), ...`), or `blueprintPath` alone and the Blueprint has no event graph;
- `AMBIGUOUS_GRAPH_NAME`: two graphs have the name (two collapsed graphs in different graphs can) or the GUID; the
  message lists each with its GUID: pass `graphGuid`;
- `READ_ONLY_GRAPH`: an edit (every tool here except search-blueprint-nodes and find-nodes-in-graph) to a graph the
  Blueprint editor shows read-only (`FBlueprintEditorUtils::IsGraphReadOnly`: interface graphs, delegate signatures,
  math expressions, graphs that are not editable). Nothing is changed;
- `NO_FOCUSED_GRAPH`: no target argument and no graph focused, or `graphName`/`graphGuid` without `blueprintPath` and
  no Blueprint focused.

Nodes and pins are addressed by GUID:

- **Node GUID** (`UEdGraphNode::NodeGuid`): `ids.guid` of a node in find-nodes-in-graph or get-focused-blueprint
  results, `nodeGuid` from add-bp-node-to-active-graph, `nodeId` from create-get-local-function-variable-node.
- **Pin GUID** (`UEdGraphPin::PinId`): `ids.guid` of a pin, `pinGuid` from add-bp-node-to-active-graph, `pinId`
  from the function pin tools, `id` in the variable node tools' `pins`.
- Both forms are accepted: 32 hex digits (`C834682D4F58EDB4003504B4BBAFA0E2`, as the add tools return them) or
  hyphenated (`157CF7BA-49F8-CE45-71C9-61AAEA82547F`, as `ids.guid` shows them).
- The short IDs in N2C JSON (`ids.short` such as `N4` and `P9`) belong to one capture: every call numbers the nodes
  again (cards 0133, 0134), so after a node is added or deleted the same node can get another short ID. No tool takes
  a short ID.
- **Pin names are stable**: a pin's `name` is its `PinName` (`execute` and `then` for exec pins, `ReturnValue`, and
  the parameter names such as `InString`), not the display name the editor language changes. connect-pins and
  set-input-pin-value accept a pin name when the pin GUID does not match, so `"pinName": "then"` with any
  `pinGuid` wires an exec output.
- **Default events**: a new Blueprint's editor-placed events (Event BeginPlay, Event Tick, ...) are disabled
  "ghost" nodes. find-nodes-in-graph and get-focused-blueprint include them with `"disabled": true` and the editor's
  comment (cards 0137, 0433). Wire one of them instead of adding a second event of the same kind; the link enables it.

## Available Tools

### search-blueprint-nodes
- **Description**: Searches the Blueprint action menu for nodes. Read-only: it describes each result from the
  spawner's template node and never adds anything to the graph (card 0050)
- **Parameters**:
  - `searchTerm` (required): Split on spaces; an action matches when every word appears, case-insensitively, in its
    full search text (`SearchActions`). Math symbols such as `+` or `*` find the promotable operators
  - `contextSensitive` (optional, default true): Build the action list for the target graph (see "Targeting and
    IDs"); false searches without a graph and ignores the target arguments
  - `blueprintPath`, `graphName`, `graphGuid` (optional): The graph to search for, as for every tool here
  - `blueprintContext` (optional): The older form of the target, used only when none of `blueprintPath`, `graphName`
    and `graphGuid` is given, and resolved by the same resolver: `owningBlueprintPath` (Blueprint asset path) and `graphPath`
    (`<Blueprint path>:<GraphName>`, or a graph name of `owningBlueprintPath`). Without `graphPath`: the focused graph
    when it belongs to that Blueprint, else its event graph. A Blueprint that does not load is an error
    (`ASSET_NOT_FOUND`), and so is a graph it does not have (`GRAPH_NOT_FOUND: no graph '<name>' in <path>. Graphs:
    ...`); neither falls back to the focused graph or a global search (`GetContextFromPaths`, cards 0277, 0152)
  - `maxResults` (optional, default 20): Clamped to 1-100
  - `category` (optional): Keep actions whose menu category (`FEdGraphSchemaAction::GetCategory`) contains this
    text, ignoring case and spaces (`N2CNodeActions::MatchesCategory`): `flowcontrol` finds `Utilities|Flow Control`
  - `excludeVMFunctions` (optional, default true): Leave out type-specific math functions such as
    `Multiply_FloatFloat`
- **Returns**: `nodes`, each an N2C JSON node (`id` `SearchNode_1`, `SearchNode_2`, ... numbered per search, `type`,
  `class`, `member_parent`, `member_name`, the optional `title`, `input_pins`, `output_pins` with their default
  values) plus `spawnMetadata` (`actionIdentifier`, `nodeName` (the menu name), `category` (the menu category) and
  `isContextSensitive`). The result is marked as untrusted Blueprint data: its first key is `untrusted_content`
  (card 0124), and a context-sensitive search has `target`
- **actionIdentifier** (card 0277): a stable ID, the node spawner's signature
  (`UBlueprintNodeSpawner::GetSpawnerSignature`, `FBlueprintNodeSignature::ToString`, `N2CNodeActions::GetActionId`).
  Print String's is `(FieldName="/Script/Engine.KismetSystemLibrary:PrintString",NodeName="/Script/BlueprintGraph.K2Node_CallFunction")`.
  It names the node class and the field or asset the node is made from, never display text, so the same action has
  the same ID in every search, in any editor language, with or without a context graph, and after a capture or a
  compile; it holds no carriage return or other control character. Where the engine's signature is not enough to
  tell actions apart, NodeToCode adds to it: the variable of a member-variable Get or Set (`FieldName`), and the
  template node's own object and member references for spawners that only name a node class (Get `<Subsystem>`,
  the instanced-struct functions). An Actor Blueprint's event graph lists about 40,000 actions on UE 5.8 with no
  two spawners sharing an ID (`NodeToCode.MCP.NodeActionIds`). An action without a spawner gets
  `menu:<category>|<menu name>`
- **Known issue (card 0434):** with `contextSensitive` false the search returns no nodes (UE 5.8: `print string`,
  `branch`, `add`, `string` all return an empty `nodes`). Use the default

### add-bp-node-to-active-graph
- **Description**: Adds the node of one search result to the target graph
- **Parameters**:
  - `nodeName` (required): The result's `spawnMetadata.nodeName`, e.g. `Print String`. Only used when different
    actions share the ID (none in the stock engine; see above): the one with this menu name, ignoring case and spaces
  - `actionIdentifier` (required): From `spawnMetadata.actionIdentifier`, unchanged. It is looked up in the target
    graph's action list, the same list search-blueprint-nodes builds (`N2CNodeActions::ResolveAction`). The
    identifiers search returned before card 0277 (the action's search text with `>` between lines) are still
    accepted, with or without their `\r` characters, now including actions whose text holds `>` such as
    `Greater ( > )`; the editor log warns that the identifier is old. This compatibility is planned for removal
    after one release
  - `location` (optional): `{ "x": number, "y": number }` in graph coordinates (default 0, 0). The view's zoom and
    pan play no part; the engine snaps the node to the 16-unit grid, so `{ "x": 400, "y": 200 }` gives
    `{ "x": 400, "y": 192 }`
- **Returns**: `success`, `nodeGuid`, `nodeName` (list title), `graphName`, `blueprintName`, `location` (after
  snapping), `inputPins` and `outputPins` (visible pins only; each `pinGuid`, `pinName`, `displayName`, `type`, and
  `defaultValue` on inputs with one), `nextStep`
- **Errors**: `ACTION_NOT_FOUND: no action with the actionIdentifier <id> can be added to this graph. ...` (an ID
  from a search in another kind of graph, or one this graph's context cannot use); `AMBIGUOUS_ACTION: <n> actions
  have the actionIdentifier <id> (<menu names>). Pass the one you want as nodeName.`

### connect-pins
- **Description**: Makes one or more pin connections in the target graph, in one transaction
- **Parameters**:
  - `connections` (required): Array of `{ "from": {...}, "to": {...} }`. Each side has:
    - `nodeGuid` (required): A node of the target graph
    - `pinGuid` (required): The pin's GUID. When it matches no pin on the node, `pinName` is tried
    - `pinName` (optional): `PinName` or display name, compared case-insensitively (`FindPinRobustly`)
    - `pinDirection` (optional): `EGPD_Input` or `EGPD_Output`; filters the `pinName` lookup
  - `options` (optional): `transactionName` (default `NodeToCode: Connect Pins`) and `breakExistingLinks` (default
    false)
- **Rules** (the schema's `CanCreateConnection`, then `N2CPinConnectPolicy::Allow`):
  - A data output and an exec input take any number of links: a second event or Then wired into the same `execute`
    pin is allowed and keeps the first link
  - A data input and an exec output take one link. A connection that would replace it fails with
    `WOULD_BREAK_EXISTING_LINK` ("Connecting would replace the existing link on then (currently linked to
    PrintString.execute). Pass breakExistingLinks=true to replace it.") unless `breakExistingLinks` is true. A link
    that needs a conversion node into an already linked data input counts as a replace too (cards 0046, 0415)
  - A connection the schema refuses fails with `SCHEMA_VALIDATION_FAILED` and the schema's message
- **Returns**: `succeeded` and `failed` (each with `from` and `to` as sent; failed ones add `errorCode` and `reason`)
  and `summary` (`totalRequested`, `succeeded`, `failed`). Failed connections do not make the call an error result
  and do not undo the successful ones: read `summary.failed`. Error codes: `INVALID_GUID` (a `nodeGuid` that is not a
  GUID), `NODE_NOT_FOUND`, `PIN_NOT_FOUND`, `WOULD_BREAK_EXISTING_LINK`, `SCHEMA_VALIDATION_FAILED`,
  `CONNECTION_FAILED`
- **Known issue (card 0276):** an entry missing `from`, `to`, or a `nodeGuid` or `pinGuid` on either side is skipped
  without a trace (`ParseConnectionRequests`): it is not in `failed` and not counted in `totalRequested`. Always send
  `pinGuid`, even when you rely on `pinName`

### set-input-pin-value
- **Description**: Sets the default value of an input pin
- **Parameters**:
  - `nodeGuid` (required), `pinGuid` (required): The pin is found by GUID, then by `pinName`
  - `pinName` (optional): `PinName` or display name, used when `pinGuid` matches no pin
  - `value` (required): The value as text
- **Accepted values** (`ValidateAndFormatValue`): booleans `true`/`false`/`1`/`0`/`yes`/`no` (empty is false);
  integers; reals; `FVector`/`FRotator` as `1.0,2.0,3.0` and `FVector2D` as `1.0,2.0` (no parentheses or `=`);
  other structs in the engine's text form, e.g. `(R=1,G=0,B=0,A=1)` for a Linear Color; enums by entry name or index;
  strings, names, text and object paths as given
- **Refused** (`ValidatePinForDefaultValue`): output pins, hidden pins, pins whose default the schema hides (exec
  pins: `This pin does not accept default values`), container pins, by-reference pins, and connected pins (`Cannot
  set default value on connected pin. Disconnect it first.`)
- **Returns**: `message`, `nodeGuid`, `nodeName`, `pinGuid` (as sent), `pinName` (display name), `pinType`,
  `oldValue`, `newValue`, `subType`

### delete-blueprint-node
- **Description**: Deletes nodes from the target graph
- **Parameters**:
  - `nodeGuids` (required): Array of node GUIDs. All are checked first: an unknown GUID (`Node with GUID '<guid>' not
    found in graph`) or a protected node fails the call and nothing is deleted
  - `preserveConnections` (optional, default false): See the known issue below
  - `force` (optional, default false): Also delete protected nodes: function entry and result nodes, and tunnel nodes
    with both inputs and outputs (`IsNodeDeletable`)
- **Returns**: `success`, `blueprintName`, `transactionId` (a new random GUID, not a handle to the undo transaction),
  `deletedCount`, `deletedNodes` (each `guid`, `title`, `type`, `graph`)
- **Known issue (card 0279):** `preserveConnections` has no effect. `PreserveNodeConnections` checks an input's
  source pin against the node's own output pin with `ArePinsCompatible`, two output pins that never match, so no
  link is made and no `preservedConnections` is returned. Deleting the middle Print String of BeginPlay -> Print
  String -> Print String leaves BeginPlay unconnected. Reconnect with connect-pins

### create-comment-node
- **Description**: Adds a comment box around nodes of the target graph
- **Parameters**:
  - `nodeGuids` (required): Node GUIDs to enclose. GUIDs that are not in the graph are listed in `missingGuids`;
    comment nodes are skipped; with no valid node the call fails
  - `commentText` (optional, default `Comment`)
  - `color` (optional): `{ "r", "g", "b" }`, 0-1. Without `color` the comment is white; with it, a missing component
    is 0.075 (`ParseArguments`), so `{ "r": 0.2 }` gives (0.2, 0.075, 0.075)
  - `fontSize` (optional, default 18): Clamped to 1-1000
  - `moveMode` (optional, default `group`): `group` moves the enclosed nodes with the comment; any other value gives
    no group movement
  - `padding` (optional, default 50): Added on every side of the nodes' bounds. The bounds use the stored node size
    (`NodeWidth` and `NodeHeight` stay 0 on ordinary nodes; only resizable nodes such as comments store a size), so
    each node counts as 150 x 50 from its position (`CalculateBoundingBox`)
- **Returns**: `success`, `commentNode` (`guid`, `text`, `x`, `y`, `width`, `height`, `color`, `fontSize`,
  `moveMode`), `includedNodes` (`guid`, `title`), `includedCount`, `missingGuids` (when any), `message`

### find-nodes-in-graph
- **Description**: Finds nodes of the target graph by keyword or GUID and returns them as N2C JSON with GUIDs
- **Parameters**:
  - `searchTerms` (required): Array of keywords or node GUIDs; a node matches when any term matches
  - `searchType` (optional, default `keyword`): `keyword` or `guid`
  - `caseSensitive` (optional, default false): For keywords
  - `maxResults` (optional, default 50): Clamped to 1-200
- **Matching**: a keyword matches the node's title as the editor shows it and in English, its UK2Node class, its
  function or variable name (`N2CStableNames::NodeSearchText`) and its menu category, as a substring: `PrintString`,
  `print string` and `K2Node_CallFunction` all find a Print String node in any editor language (card 0138). A GUID
  term matches the node GUID with or without hyphens, case-insensitively
- **Returns**: `metadata` (`blueprintName`, `graphName`, `totalFound`, `totalInGraph`) and `nodes`. Each node is the
  same N2C JSON get-focused-blueprint returns (`type`, `class`, `title`, `member_parent`, `member_name`, `comment`,
  `disabled`, `input_pins`, `output_pins`), with `ids` (`short`, `guid`) in place of `id`, and each pin with `ids`
  (`short`, `guid`, `name`). The whole graph is captured with the default events included (card 0137). With no
  match: `{"nodes":[],"metadata":{"totalFound":0}}`. The result is marked as untrusted Blueprint data
  (`untrusted_content`)

## Node Search and Add Workflow

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

### Step 1: Search for Nodes
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
        "maxResults": 5
      }
    },
    "id": 1
  }'
```

### Step 2: Add Found Node
```bash
# nodeName and actionIdentifier copied unchanged from the result's spawnMetadata. Without blueprintPath and graphName
# (or graphGuid) the node goes into the focused graph
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
        "actionIdentifier": "(FieldName=\"/Script/Engine.KismetSystemLibrary:PrintString\",NodeName=\"/Script/BlueprintGraph.K2Node_CallFunction\")",
        "location": {"x": 400, "y": 200},
        "blueprintPath": "/Game/Blueprints/BP_Door.BP_Door",
        "graphName": "EventGraph"
      }
    },
    "id": 2
  }'
# The result's target: {"blueprintPath": "/Game/Blueprints/BP_Door.BP_Door", "blueprintName": "BP_Door",
#   "graphName": "EventGraph", "graphGuid": "<32 hex digits>", "graphKind": "event", "resolvedBy": "explicit"}
```

## Pin Connection Example

```bash
# Event BeginPlay (its GUID from find-nodes-in-graph) -> the new Print String (nodeGuid and pinGuid from step 2).
# "then" and "execute" are the stable exec pin names; pinGuid is still required but may be any value here.
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
              "nodeGuid": "157CF7BA-49F8-CE45-71C9-61AAEA82547F",
              "pinGuid": "00000000000000000000000000000000",
              "pinName": "then"
            },
            "to": {
              "nodeGuid": "C834682D4F58EDB4003504B4BBAFA0E2",
              "pinGuid": "0F84CF304BC3158E5EB4BD9B391399C6"
            }
          }
        ],
        "options": {
          "transactionName": "Wire up print logic"
        }
      }
    },
    "id": 3
  }'
```

## Setting Pin Values

```bash
# String
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "C834682D4F58EDB4003504B4BBAFA0E2",
        "pinGuid": "3816AA754BFEE034ED699495F9A9C480",
        "value": "Hello, World!"
      }
    },
    "id": 1
  }'

# Float, found by pin name
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "C834682D4F58EDB4003504B4BBAFA0E2",
        "pinGuid": "00000000000000000000000000000000",
        "pinName": "Duration",
        "value": "3.5"
      }
    },
    "id": 2
  }'

# Boolean
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "set-input-pin-value",
      "arguments": {
        "nodeGuid": "C834682D4F58EDB4003504B4BBAFA0E2",
        "pinGuid": "00000000000000000000000000000000",
        "pinName": "bPrintToScreen",
        "value": "false"
      }
    },
    "id": 3
  }'
```

## Node Deletion

```bash
# One node; add more GUIDs to the array to delete several in one transaction
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "delete-blueprint-node",
      "arguments": {
        "nodeGuids": ["C834682D4F58EDB4003504B4BBAFA0E2"]
      }
    },
    "id": 1
  }'

# Protected nodes (function entry and result) need force; use with caution
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
    "id": 2
  }'
```

## Comment Node Creation

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
          "C834682D4F58EDB4003504B4BBAFA0E2",
          "E53BA7E5495E43514793BAABDBDDDE98"
        ],
        "commentText": "Player Input Handling",
        "fontSize": 24,
        "moveMode": "group"
      }
    },
    "id": 1
  }'
```

The tool's description asks agents not to set `color` unless the user wants one.

## Finding Nodes in Graph

```bash
# By keywords
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "find-nodes-in-graph",
      "arguments": {
        "searchTerms": ["BeginPlay", "print string"],
        "maxResults": 10
      }
    },
    "id": 1
  }'

# By node GUID (with or without hyphens)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "find-nodes-in-graph",
      "arguments": {
        "searchTerms": ["157CF7BA-49F8-CE45-71C9-61AAEA82547F"],
        "searchType": "guid"
      }
    },
    "id": 2
  }'
```

## Common Patterns

### A Print on BeginPlay in a new Actor Blueprint
Pass the same `blueprintPath` and `graphName` `EventGraph` (or the `graphGuid` the first result's `target` gives) to
every step to work without the editor's focus.

1. `find-nodes-in-graph` `["BeginPlay"]`: the default Event BeginPlay is there, `"disabled": true`; keep its `ids.guid`
2. `search-blueprint-nodes` `print string`, then `add-bp-node-to-active-graph` with its `actionIdentifier`
3. `connect-pins` BeginPlay `then` -> Print String `execute`
4. `set-input-pin-value` on `InString`

### Working with Variables
Local variables: `create-get-local-function-variable-node` / `create-set-local-function-variable-node` return a node
GUID and pin GUIDs to wire. Member variables: search for the variable's Get or Set action with
`search-blueprint-nodes` and add it with `add-bp-node-to-active-graph`, which returns usable GUIDs; the member
variable node tools cannot be wired yet (card 0155, `../Variables/AGENTS.md`).

## Undo, Compile and Save

- Each editing call is one editor undo step: connect-pins, set-input-pin-value, delete-blueprint-node and
  create-comment-node open an `FScopedTransaction`; add-bp-node-to-active-graph uses the engine's own "Add Node"
  transaction (`FBlueprintActionMenuItem::PerformAction`).
- search-blueprint-nodes and find-nodes-in-graph are read-only.
- Every successful edit marks the Blueprint modified and its package dirty (`FN2CMcpBlueprintUtils::MarkModified`),
  and the tool call compiles it once, after the edit's transaction has closed (card 0151): connect-pins with 20
  connections runs one compile, not 20. create-comment-node only dirties the package and never compiles. The
  search and find tools compile nothing and queue no action-database refresh. Nothing is saved: use `save-blueprint`.
  Every edit tool's result has a `compile` object: `blueprint`, `blueprintPath`, `status`, `success`, `errorCount`,
  `warningCount`, `noteCount`, `compileSeconds` and `messages` (each `severity` and `message`, plus `nodeGuid`,
  `nodeTitle`, `graph` and `pin` when the compiler names a node). A failed compile does not make the call an error:
  the edit was made, and `compile.success` is false.

## Implementation Notes

- Node search reads the Blueprint action database through `FBlueprintActionMenuBuilder`; add-bp-node-to-active-graph
  rebuilds the same list for the target graph, finds the action by its ID and calls `PerformAction` on it. Both, and
  the Python bridge's `search_blueprint_nodes` and `add_node_to_graph`, share `MCP/Utils/N2CMcpNodeActions.h`
- Pin connections are checked by the graph schema (`CanCreateConnection`) and made with `TryCreateConnection`
- Every tool here resolves its graph through `FN2CGraphTarget::Resolve` (`MCP/Utils/N2CMcpGraphTarget.h`), and so do
  the Python bridge's graph and function calls (`N2CPythonBridge.cpp`), which take no target arguments and use the
  focused graph; they refuse read-only graphs (`READ_ONLY_GRAPH`) the same way
