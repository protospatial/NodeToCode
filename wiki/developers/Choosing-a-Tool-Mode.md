NodeToCode's MCP server has 67 tools in 12 toolsets. Two settings choose which of them a connected client sees:
**Tool Profile** picks the toolsets, and **Tool Exposure** picks whether `tools/list` names every tool or only three
search tools that reach them. The defaults serve every toolset and list every tool, which suits most clients; change
them when a client struggles with many tools or should only run Python.

All of them are in **Project Settings → Plugins → Node to Code → MCP Server**, are read when the server starts, and
need an editor restart; then reconnect your client. The list never changes while the editor runs, so a client that
reads it once (as most do) stays correct. Every client gets the same list.

## Tool Profile

| Tool Profile | What the client gets | Tools |
|---|---|---|
| **Default toolsets** (default) | Every toolset | 64, or 67 with **Allow MCP Python Execution** |
| **Python scripting only** | Only **PythonTools**: `run-python` and the script library (`list-python-scripts`, `search-python-scripts`, `get-python-script`, `get-script-functions`, `save-python-script`, `delete-python-script`) | 7, or 4 without **Allow MCP Python Execution** |
| **Custom toolsets** | The toolsets you pick in **Custom Toolsets** | the sum of their tools |

**Custom Toolsets** appears under **Tool Profile** when **Custom toolsets** is selected. Add one element per toolset
and pick its name from the list:

| Toolset | Tools | For |
|---|---|---|
| BlueprintDiscoveryTools | 8 | Reading Blueprints: the focused one, any one by path, graphs and functions as N2C JSON |
| BlueprintGraphTools | 6 | Adding, connecting and deleting nodes, pin values, comments |
| BlueprintFunctionTools | 7 | Creating and deleting functions and their pins |
| BlueprintVariableTools | 11 | Member and local variables, their defaults and their get and set nodes |
| BlueprintTagTools | 3 | Tagging graphs for batch work |
| BlueprintCompileTools | 2 | Compiling and saving |
| BlueprintClassTools | 2 | Creating Blueprint classes |
| BlueprintComponentTools | 3 | An Actor Blueprint's components |
| ContentBrowserTools | 10 | Searching, opening, copying, moving and renaming assets and folders |
| FileSystemTools | 2 | Reading text files and folders inside the project |
| PythonTools | 7 | `run-python` and the script library |
| TranslationTools | 5 | NodeToCode's own LLM translation, its providers and languages |

For example, BlueprintDiscoveryTools and TranslationTools give an agent 14 tools to read Blueprints and translate
them, and nothing that edits a Blueprint.

## Tool Exposure

| Tool Exposure | `tools/list` holds |
|---|---|
| **List every tool** (default) | Every tool of the profile, each with its own schema |
| **Tool search (list_toolsets, describe_toolset, call_tool)** | Only those three tools, whatever the profile |

Use **Tool search** when a client limits or slows down with many tools, for example a chat that warns a request has
too many. The agent then works in three steps, with the same names and arguments as UE 5.8's Unreal MCP:

```json
{"name": "list_toolsets", "arguments": {}}
{"name": "describe_toolset", "arguments": {"toolset_name": "BlueprintDiscoveryTools"}}
{"name": "call_tool", "arguments": {"toolset_name": "BlueprintDiscoveryTools", "tool_name": "get-focused-blueprint", "arguments": {}}}
```

`list_toolsets` names the profile's toolsets with a description, `describe_toolset` gives each tool's description
and input schema, and `call_tool` runs it, including a long translation with progress and cancel. A toolset the
profile does not serve is an error that names the ones it does. Keep **List every tool** for Epic's MCP Client
Toolset ([Using NodeToCode with Unreal MCP](Using-NodeToCode-with-Unreal-MCP)): it would nest these three inside its
own tool search.

## Overlapping Tools (UE 5.8)

On UE 5.8, Epic's EditorToolset has its own versions of 34 NodeToCode tools (Content Browser, components, classes,
compile and save, function pins, variable nodes). **Overlapping Tools** decides who serves them when the **Unreal
MCP** (ModelContextProtocol) and **EditorToolset** plugins are both enabled:

| Overlapping Tools | Effect |
|---|---|
| **Automatic (leave them to Unreal MCP on UE 5.8)** (default) | Left out of the **Default toolsets** profile, which then lists 30 tools (33 with Python execution); a Custom profile serves its toolsets as chosen |
| **Leave them to Unreal MCP when it is enabled** | Left out of every profile |
| **Always serve them** | Served, as on UE 5.6 and 5.7 |

A call to a left-out tool answers with the Epic tool to use instead. The pairs are in
[NodeToCode and Epic Tool Mapping](NodeToCode-and-Epic-Tool-Mapping). On UE 5.6 and 5.7 the setting does nothing.

## Python execution

`run-python`, `save-python-script` and `delete-python-script` exist only when **Allow MCP Python Execution** is on in
**Editor Preferences → Plugins → Node to Code (User) → MCP Server**. It is off by default and per user: a value in the
project's committed config files is ignored. With it on, any connected client can run any Python in your editor.
Python scripting only with it off leaves the 4 read-only script library tools, and the Output Log warns at start.

## While you play in the editor

During Play In Editor or Simulate, the 32 tools that edit or compile a Blueprint, or create, copy, move or rename an
asset or folder, answer `PIE_ACTIVE` until the session ends; every tool stays listed. Reading, translation, opening
editors, the script library and `run-python` keep working.

## Reading tool results

Every result has the JSON as text. These tools also declare an `outputSchema` in `tools/list` and return the same
object as `structuredContent`: `get-focused-blueprint`, `list-blueprint-graphs`, `get-open-blueprint-editors`, `get-editor-context`,
`translate-focused-blueprint`, `get-available-translation-targets`, `get-available-llm-providers`,
`get-translation-output-directory` and `read-path`. A translation over 200,000 characters is saved under
`Saved/NodeToCode/Exports/Translations`; the result then lists its graphs with `savedTo` and a `resource_link`.
A failed call is an `isError` result whose `_meta["nodetocode/error"]` holds a stable `code` (`INVALID_ARGUMENT`,
`TOOL_FAILED`, `PIE_ACTIVE`, ...) and, for a bad argument, its `field`; arguments that are not an object get one too.

## Settings from older versions

A project saved with **Enable Python Script-Only Mode** loads as **Python scripting only**, and one saved with
**Enable Dynamic Tool Discovery** loads as **Tool search** (when script-only mode was off). The old `assess-needed-tools`
tool is gone: use **Tool search** or a **Custom toolsets** profile instead.
