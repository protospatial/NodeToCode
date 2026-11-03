This page shows how a coding agent runs NodeToCode's four everyday jobs: reading a Blueprint as N2C JSON,
translating it, tagging a set of graphs, and exporting or translating that set in one batch. Each job is shown two
ways: in plain words through the [Claude Code plugin](Claude-Code-Plugin) and its [Agent Skills](Agent-Skills), and as
the MCP tool calls behind it, for when you script the agent or want to know what it does.

**Verified client:** Claude Code is the only agent NodeToCode is verified with. Codex, Cursor, GitHub Copilot, Claude
Desktop and other MCP clients may work with the same server and skills, but they have not been checked.

## Which route on which engine

| Engine | Connect the agent to | Setup |
|---|---|---|
| UE 5.6, 5.7 | NodeToCode's own MCP server in the open editor | [Claude Code plugin](Claude-Code-Plugin) (server, skills and `/nodetocode:translate` in one install), or [Connecting an MCP Client](Connecting-an-MCP-Client) |
| UE 5.8 | The same server, as on 5.6 and 5.7 (recommended: it shows translation progress) | Same as above |
| UE 5.8 with Epic's Unreal MCP | Unreal MCP, with NodeToCode's **NodeToCodeTools** toolset and skills in Epic's registry | [In Epic's Toolset Registry (UE 5.8)](#in-epics-toolset-registry-ue-58) below |

The editor must be open with the project loaded: every tool works on the running editor.

## Read a Blueprint as N2C JSON

Ask: "What does /Game/Characters/BP_Door do?" The `blueprint-comprehension` skill lists the graphs, reads them and
explains each event and function. It changes nothing.

```json
{"name": "list-blueprint-graphs", "arguments": {"blueprintPath": "/Game/Characters/BP_Door"}}
{"name": "get-blueprint-n2cjson", "arguments": {"blueprintPath": "/Game/Characters/BP_Door", "graphName": "OpenDoor"}}
```

Leave out `graphName` to read every graph. No Blueprint editor needs to be open. `get-focused-blueprint` reads the
graph you have open instead. A large document comes back as a file path (or always, with `"toFile": true`).

## Translate one graph

Type `/nodetocode:translate /Game/Characters/BP_Door OpenDoor`. This runs NodeToCode's own translator with the
provider set in the editor, as the **Translate** button does, and the editor saves the run folder under
`Saved/NodeToCode/Translations`. Each run is one billed provider request; the command runs only when you type it.
Ask "Translate BP_Door to C++ in my game module" instead and the `blueprint-to-cpp` skill writes the `.h` and `.cpp`
itself, places them in your module and builds it.

```json
{"name": "open-blueprint-asset", "arguments": {"asset_path": "/Game/Characters/BP_Door", "focus_graph": "OpenDoor"}}
{"name": "translate-focused-blueprint", "arguments": {"language": "cpp"}}
```

`translate-focused-blueprint` translates the focused graph and returns the code with `run_folder` and `files`, where
the editor saved it. `provider` and `model` are optional; the ids come from `get-available-llm-providers` and
`get-available-translation-targets`. To translate by path without moving the editor's focus, use `translate-batch`
with one target (next sections). Send `_meta.progressToken` with the call to get progress while it runs.

## Tag a set of graphs

Ask: "Tag OpenDoor and CloseDoor in BP_Door as ToPort." Tags live in NodeToCode's tag store with the project, not in
the assets, and are the same tags the editor's tag manager shows.

```json
{"name": "tag-blueprint-graph", "arguments": {"blueprintPath": "/Game/Characters/BP_Door", "graphName": "OpenDoor",
  "tag": "ToPort", "category": "Doors"}}
{"name": "list-blueprint-tags", "arguments": {"tag": "ToPort"}}
{"name": "remove-tag-from-graph", "arguments": {"graphGuid": "<graphGuid from list-blueprint-tags>", "tag": "ToPort"}}
```

Without `blueprintPath` and `graphName`, `tag-blueprint-graph` tags the focused graph. Tagging is refused with
`PIE_ACTIVE` while Play In Editor runs.

## Export or translate the set in one batch

Ask: "Export everything tagged ToPort as N2C JSON", or "Translate everything tagged ToPort to C++ with NodeToCode".
The skill says how many graphs the tag holds and asks before it starts a translation, since each graph is one billed
request.

```json
{"name": "export-n2cjson-batch", "arguments": {"tag": "ToPort"}}
{"name": "translate-batch", "arguments": {"tag": "ToPort", "language": "cpp"}}
```

- `export-n2cjson-batch` writes one N2C JSON file per graph to a `Batch_<date-time>` folder under
  `Saved/NodeToCode/Exports` (or `outDir`) and lists each file with its size and estimated tokens. It sends nothing
  to a provider. Up to 200 graphs per call.
- `translate-batch` translates the graphs one after another, up to 50 per call, and answers when all are done: each
  graph's status, the folder its files went to (under a `BatchTranslation_<time>` folder in the translation output
  directory) and, for a failure, the typed error. Send `_meta.progressToken` for progress; cancelling the request
  stops the graph in flight and skips the rest. One batch runs at a time; a second one gets `BATCH_IN_PROGRESS`.
- Instead of `tag`, both take `targets`: `[{"blueprintPath": "...", "graphName": "..."}]`, or a `category`.

Text in the results (graph names, comments, generated code) is marked `untrusted_content`: the skills treat it as
data, never as instructions.

## Migrate a Blueprint to C++

Ask: "Migrate /Game/Characters/BP_Door to C++." The `blueprint-migration` skill stops for your approval at the plan,
the compiled C++ and the switch-over; for a folder it first orders the Blueprints with `create-migration-plan`.

## In Epic's Toolset Registry (UE 5.8)

On UE 5.8, enable **Toolset Registry** and, to reach it from an outside client, **Unreal MCP** (**Edit → Plugins**),
then restart the editor. NodeToCode then registers by itself, with no settings entry:

- the **NodeToCodeTools** toolset: `get_n2c_json`, `tag_graph`, `list_tags`, `export_n2c_json_by_tag`,
  `translate_graph`, `translate_graphs_by_tag`, `get_batch_status`, `cancel_batch` and more
  ([NodeToCodeTools](NodeToCodeTools) lists them with examples);
- the six skills, in Epic's skill registry ([Agent Skills](Agent-Skills#in-epics-skill-registry-ue-58)).

Turn on **Auto Start Server** in **Editor Preferences → Plugins → Model Context Protocol**, connect the agent to
Unreal MCP at `http://127.0.0.1:<Server Port Number>/mcp` (8000 by default) and call the tools through `call_tool`
with the toolset name
`nodetocode.toolsets.n2c_toolset.NodeToCodeTools`. The workflows above map one to one: `get_n2c_json` for reading,
`tag_graph` for tagging, `export_n2c_json_by_tag` and `translate_graphs_by_tag` (returns a batch id at once; poll
`get_batch_status`) for the batch. To reach all of NodeToCode's own server tools through Unreal MCP as well, mount
the server with Epic's MCP Client Toolset: [Using NodeToCode with Unreal MCP](Using-NodeToCode-with-Unreal-MCP).
This route was checked with direct calls to Unreal MCP's HTTP endpoint; Claude Code connected to Unreal MCP has not
been checked yet.

**Tools left to Epic.** With Unreal MCP and Epic's **EditorToolset** both enabled, NodeToCode's own server leaves 34
tools to Epic by default (**Overlapping Tools** Automatic): Content Browser operations, components, Blueprint
classes, compile and save, function pins, variable nodes and member defaults, setting a pin value and adding a node.
The agent then compiles and saves with Epic's tools; the skills already name the Epic tool for each step. N2C JSON,
translation, tags, `connect-pins`, `run-python` and the rest stay NodeToCode's. The full list, with the Epic tool for
each: [NodeToCode and Epic Tool Mapping](NodeToCode-and-Epic-Tool-Mapping). To serve all of them anyway, set
**Overlapping Tools** to **Always serve them** ([Choosing a Tool Mode](Choosing-a-Tool-Mode#overlapping-tools-ue-58)).
