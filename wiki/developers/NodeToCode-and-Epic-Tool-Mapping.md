On UE 5.8, Epic's **Unreal MCP** with its **EditorToolset** does 34 things NodeToCode's MCP server also does:
Content Browser operations, components, Blueprint classes, compile and save, function pins, variable nodes and
member defaults, setting a pin value and adding a node. When both plugins are enabled, NodeToCode's server leaves
those 34 tools to Unreal MCP by default, so an agent connected to both servers sees one tool per operation. This page
lists each one with the Epic tool to use instead. On UE 5.6 and 5.7, or on 5.8 without those plugins, NodeToCode
serves every tool as before.

## When the tools are left out

All of these must hold when the editor starts:

- the engine is UE 5.8 or later;
- the **Unreal MCP** (`ModelContextProtocol`) and **EditorToolset** plugins are both enabled (**Edit → Plugins**);
- **Overlapping Tools** (**Project Settings → Plugins → Node to Code → MCP Server**) is **Automatic** (the default)
  and **Tool Profile** is **Default toolsets**, or **Overlapping Tools** is **Leave them to Unreal MCP** (any profile).

| Overlapping Tools | What happens with both plugins on UE 5.8 |
|---|---|
| **Automatic** (default) | Left out of the **Default toolsets** profile. A **Custom** profile serves its toolsets as chosen |
| **Leave them to Unreal MCP** | Left out of every profile |
| **Always serve them** | Served, as on UE 5.6 and 5.7 |

Restart the editor after changing it. The Output Log says at startup how many tools were left to Unreal MCP and why
(`MCP tools: 34 tools left to Unreal MCP ...`), or why none were.

With 64 tools by default (67 with **Allow MCP Python Execution**), NodeToCode lists 30 (33) when the 34 are left out.
In **Tool search** exposure, `list_toolsets` no longer shows **BlueprintCompileTools**, **BlueprintClassTools**,
**BlueprintComponentTools** and **ContentBrowserTools**, whose every tool is Epic's then. A client that still calls a
left-out tool gets `Unknown tool: <name>` with the Epic tool to use; `describe_toolset` and `call_tool` say the same.

The same tools are left out of what Epic's **MCP Client Toolset** mounts from NodeToCode's server (see
[Using NodeToCode with Unreal MCP](Using-NodeToCode-with-Unreal-MCP)).

## The mapping

Epic's tools are named `<Toolset>.<tool>` as Unreal MCP's `describe_toolset` shows them (EditorToolset's
**BlueprintTools**, **AssetTools**, **ObjectTools**, **ActorTools** and **EditorAppToolset**).

| NodeToCode tool | Epic counterpart |
|---|---|
| `list-blueprint-functions` | `BlueprintTools.list_functions`, `list_graphs` |
| `list-overridable-functions` | `BlueprintTools.list_functions`, `list_events` |
| `search-blueprint-nodes` | `BlueprintTools.find_node_types`, `find_node_categories`, `get_node_type_pins` |
| `add-bp-node-to-active-graph` | `BlueprintTools.create_node`, `write_graph_dsl` |
| `set-input-pin-value` | `BlueprintTools.set_pin_value` |
| `create-blueprint-function` | `BlueprintTools.add_function_graph` |
| `add-function-input-pin` | `BlueprintTools.add_function_param` |
| `add-function-return-pin` | `BlueprintTools.add_function_param` |
| `remove-function-entry-pin` | `BlueprintTools.remove_function_param` |
| `remove-function-return-pin` | `BlueprintTools.remove_function_param` |
| `create-get-member-variable-node` | `BlueprintTools.create_node` |
| `create-set-member-variable-node` | `BlueprintTools.create_node` |
| `create-get-local-function-variable-node` | `BlueprintTools.create_node` |
| `create-set-local-function-variable-node` | `BlueprintTools.create_node` |
| `get-blueprint-member-variables` | `BlueprintTools.list_variables` |
| `get-blueprint-function-local-variables` | `BlueprintTools.list_variables` |
| `set-member-variable-default-value` | `BlueprintTools.get_default_object`, `ObjectTools.set_properties` |
| `compile-blueprint` | `BlueprintTools.compile_blueprint` |
| `save-blueprint` | `AssetTools.save_assets` |
| `create-blueprint-class` | `BlueprintTools.create` |
| `search-blueprint-classes` | `ObjectTools.search_subclasses` |
| `add-component-class-to-actor` | `ActorTools.add_component` |
| `delete-component-in-actor` | `ActorTools.remove_component` |
| `list-components-in-actor` | `ActorTools.get_components` |
| `copy-asset` | `AssetTools.duplicate` |
| `create-folder` | `AssetTools.create_folder` |
| `move-asset` | `AssetTools.move` |
| `move-folder` | `AssetTools.move` |
| `rename-asset` | `AssetTools.move` |
| `rename-folder` | `AssetTools.move` |
| `open-blueprint-asset` | `EditorAppToolset.OpenEditorForAsset` |
| `open-content-browser-path` | `EditorAppToolset.SetContentBrowserPath` |
| `read-content-browser-path` | `AssetTools.list_folders`, `find_assets` |
| `search-content-browser` | `AssetTools.find_assets` |

Epic's tools take an explicit Blueprint or asset path; several NodeToCode tools act on the Blueprint focused in the
editor. Where a NodeToCode tool gave more (for example node GUIDs in N2C JSON), read the Blueprint with
`get-blueprint-n2cjson` first.

## What NodeToCode keeps serving

Everything else, including the tools Epic has no counterpart for or where NodeToCode's does more: N2C JSON
(`get-focused-blueprint`, `get-blueprint-n2cjson`, `export-n2cjson-batch`), translation (`translate-focused-blueprint`,
`translate-batch` and the provider, language and output-folder tools), graph tags, the Python script library and
`run-python`, `connect-pins`, `delete-blueprint-node`, `find-nodes-in-graph`, `create-comment-node`, variable creation
and types, local variable defaults, opening and deleting functions, `read-file` and `read-path`.

On UE 5.8 the [NodeToCodeTools](NodeToCodeTools) toolset also puts NodeToCode's Blueprint comprehension tools in
Epic's registry directly.
