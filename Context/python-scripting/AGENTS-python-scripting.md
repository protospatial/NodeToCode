# NodeToCode Python Scripting

MCP Bridge and Python script management system for LLM-powered UE automation.

## Directory Structure

```
Content/Python/
├── mcp_bridge/
│   ├── nodetocode_bridge.py    - Stdio-to-HTTP MCP bridge
│   ├── launch_bridge.bat       - Windows launcher script
│   ├── launch_bridge.sh        - Mac/Linux launcher script
│   └── ue_python_finder.py     - UE Python installation finder
│
├── nodetocode/                 - Core Python module (import nodetocode as n2c)
│   ├── __init__.py             - Module exports (__all__ is the function list)
│   ├── blueprint.py            - Get, read, compile, save and load Blueprints
│   ├── graph.py                - Graph editing: search, add, connect, set, delete, find, comment
│   ├── functions.py            - Function input and return pins
│   ├── bridge.py               - Tagging, LLM provider info, opening Blueprints and functions
│   ├── scripts.py              - Script CRUD with dual-path search
│   └── utils.py                - Logging, result helpers, the bridge result parser
│
├── scripts/                    - Script library (bundled + user)
│   ├── script_registry.json    - Metadata index (the list of bundled scripts)
│   ├── core/                   - Core utility scripts
│   ├── utilities/              - Project utilities
│   ├── data/                   - Data manipulation scripts
│   ├── ui/                     - UI/Widget scripts
│   └── general/                - Empty; save_script's default category
│
└── tests/                      - CPython tests for the stdio bridge (test_mcp_bridge.py) and a graph-editing
                                  test to run inside the editor (test_graph_editing.py)
```

## MCP Bridge

### nodetocode_bridge.py

Stdio-to-HTTP bridge enabling MCP clients (Claude Desktop, etc.) to communicate with NodeToCode's MCP server.

*   **Location**: `Content/Python/mcp_bridge/nodetocode_bridge.py`
*   **Purpose**: Forward JSON-RPC messages between MCP clients and UE5
*   **Key Features**:
    *   stdin and stdout are reconfigured to UTF-8 (the launchers also set `PYTHONUTF8=1`), so non-ASCII text is
        not decoded with the Windows ANSI code page
    *   Each stdin message is POSTed to `/mcp` with `Accept: application/json, text/event-stream`.
        A `text/event-stream` response (UE 5.8's streamed `translate-focused-blueprint`) is parsed per the SSE spec
        and each message in it is written to stdout as it arrives; the response to the request ends the call
    *   One thread reads stdin (`Bridge.submit`); every request except `initialize` runs on its own worker thread,
        so `ping` and a quick tool answer while a translation runs. `initialize` and notifications go in order on
        the reader thread; stdout writes are serialized under a lock
    *   `notifications/cancelled` is forwarded at once (and once more a second later if the call still runs, in
        case it overtook its request); from then on nothing is written for that request id (the editor sends no
        response, and the bridge's own "stream ended" error is dropped). A request cancelled before it was sent is
        never sent
    *   stdout carries only JSON-RPC messages, one per line: a transport failure or non-JSON body (an HTTP error
        page) becomes a JSON-RPC error with the request's id, and notifications never get a reply
    *   Editor selection (`EditorLocator`): `--url` or `--port` names the editor. Otherwise it reads the discovery
        entries the editor's MCP server writes (card 0112: `<UserSettingsDir>/NodeToCode/servers/<pid>.json`, that
        is `%LOCALAPPDATA%` on Windows, `~/Library/Application Support/Epic` on macOS, `~/.config/Epic` on Linux;
        plus `<Project>/Saved/NodeToCode/mcp-server.json` when `--project` is a path), skipping entries whose pid is
        gone. One editor: that one. `--project` / `NODETOCODE_PROJECT` matches the project name, `.uproject` path or
        folder; several editors and no selection is a JSON-RPC error listing them (`error.data.editors`).
        `NODETOCODE_DISCOVERY_DIR` replaces the index folder (tests)
    *   Lazy connect: the bridge starts without an editor and looks again on every request; until one answers, each
        request gets a JSON-RPC error with its id. When a discovered editor refuses the connection, the bridge looks
        again and, if the entries now name another editor, replays the client's `initialize` there and resends
    *   Session management with `Mcp-Session-Id` (and `MCP-Protocol-Version` after initialize); a 404 replays
        `initialize` and resends once; stdin EOF waits for calls in flight, then sends DELETE
    *   No read timeout by default (a held translation sends nothing until it finishes); `--timeout SECONDS` sets one
    *   Debug mode with `--debug` flag (logs go to stderr)
*   **Usage**:
    ```bash
    python nodetocode_bridge.py [--project NAME_OR_PATH] [--port PORT [--host HOST]] [--url URL] [--timeout SECONDS] [--debug]
    ```
*   **Tests**: `python -I -m unittest discover -s Content/Python/tests -p "test_mcp_bridge*.py"` (no editor; fake
    editors on http.server and fake discovery folders); live: `Tests/mcp_probe/live_stdio_bridge.py` and
    `Tests/mcp_probe/live_bridge_concurrency.py`

### launch_bridge.bat (Windows) and launch_bridge.sh (Mac/Linux)

Launchers that pick the Python through `ue_python_finder.py`:

1. Run `ue_python_finder.py` with `py -3`, `python3` or `python` (the `.sh`: `python3`, then `python`); it prints
   the bundled Python of the newest UE install it finds, else the Python that ran it
2. Without a Python on PATH, the bundled Python of a registered launcher install (Windows registry, any version) or
   of a `UE_*` folder in a common install folder (`.sh`) runs the finder
3. Set `PYTHONUTF8=1` and pass all arguments to the bridge script

### ue_python_finder.py

Python installation finder using the same methods as UnrealVersionSelector:

*   **Windows Registry**: `HKEY_CURRENT_USER\SOFTWARE\Epic Games\Unreal Engine\Builds`
*   **Epic Games Launcher**: `LauncherInstalled.dat` file
*   **Common paths**: every `UE_*` folder in standard UE installation directories (no fixed version list)
*   Installs are ordered by the version in their `Engine/Build/Build.version`; `--list` prints them all
*   **Functions**:
    *   `find_ue_python(prefer_newest=True)` - Find UE's bundled Python
    *   `find_ue_source(prefer_newest=True)` - Find Engine/Source path
    *   `find_python(prefer_ue=True)` - Find any Python, preferring UE's

## NodeToCode Python Module

Importable module providing Blueprint manipulation and script management. All functions return standardized dictionaries with `success`, `data`, and `error` keys.

### Quick Start

```python
import nodetocode as n2c

# Blueprint operations (a Blueprint graph must be focused: open one with n2c.open_blueprint(path))
bp = n2c.get_focused_blueprint()
if bp['success']:
    print(f"Blueprint: {bp['data']['name']}, graph {bp['data']['graph_name']}, {bp['data']['node_count']} nodes")

n2c.compile_blueprint()
n2c.save_blueprint()

# Script management
matches = n2c.search_scripts("blueprint")
result = n2c.run_script("my_script", max_hp=100)   # a saved script; its `result` is in result['data']['result']

# Tagging system
n2c.tag_graph("player_controller", category="Systems")
tags = n2c.list_tags()
```

### Module Files

| File | Purpose |
|------|---------|
| `__init__.py` | Module exports (`__all__`), version info (`__version__ = "1.0.0"`) |
| `blueprint.py` | Blueprint get/read/compile/save/load via `UN2CPythonBridge` |
| `graph.py` | Graph editing via `UN2CPythonBridge`: node search, add, connect, pin values, delete, find, comments |
| `functions.py` | Function input and return pins via `UN2CPythonBridge` |
| `bridge.py` | Tagging, LLM provider info, `open_blueprint` and `open_blueprint_function` |
| `scripts.py` | Script CRUD with dual-path search (plugin + project) |
| `utils.py` | Logging (`log_info`, `log_warning`, `log_error`), result helpers, `_parse_bridge_result` |

### Available Functions

The authoritative list is `__all__` in `Content/Python/nodetocode/__init__.py`; each function's docstring gives its
arguments and the keys of its `data`. Grouped:

**Blueprint Operations:**
*   `get_focused_blueprint()` - The focused graph as N2C JSON: `data` has `name`, `path`, `graph_name`, `node_count`
    and `n2c_json` (version 2.0.0, stable names: each node's `class`, `member_parent`, `member_name`, each pin's
    `name` such as `execute`, `then` or `ReturnValue`; `title` and `label` are English display hints). Like the MCP
    `get-focused-blueprint` tool it keeps the editor's unwired default events (a new Actor Blueprint's greyed-out
    BeginPlay, ActorBeginOverlap and Tick) as `"disabled": true` nodes, so a script finds the default BeginPlay and
    wires it instead of adding a second one: a new Actor Blueprint's untouched EventGraph gives `node_count` 3 and
    those three nodes (card 0433). Translation leaves them out, and translating a graph of only those events fails
    with "Nothing to capture: the graph holds only unwired default events (ghost nodes)"
*   `get_blueprint_n2cjson(blueprint_path, graph_name='', graph_guid='', depth=-1, to_file=False, compact=True)` -
    Any Blueprint's graphs by asset path, with no editor open (`UN2CPythonBridge::GetBlueprintN2CJson`, card 0157):
    `data` has `blueprintPath`, `blueprintName`, `graphCount`, `nodeCount`, `estimatedTokens`, `target` (when a graph
    was named) and `n2c_json`, the document the MCP get-blueprint-n2cjson tool returns; every graph when no graph is
    named. There is no size limit here: `to_file=True` writes the document under `Saved/NodeToCode/Exports` and
    returns `path` and `bytes` instead. Errors start with a code (`ASSET_NOT_FOUND`, `GRAPH_NOT_FOUND`, `EMPTY_GRAPH`)
*   `list_blueprint_graphs(blueprint_path)` - Every graph of a Blueprint by asset path, as list-blueprint-graphs
    returns it (`UN2CPythonBridge::ListBlueprintGraphs`): `graphs` with `name`, `graphGuid`, `kind`, `nodeCount`,
    `readOnly` and, for collapsed graphs, `parentGraph`
*   `read_n2c_json(text)` - Read N2C JSON 1.x or 2.x (for example a saved `N2C_BP_*.json`) with the plugin's reader
    (`UN2CPythonBridge::ReadN2CJson`); returns its source version, counts (`data_edges`, `exec_edges`) and the document
    as current (2.0.0) N2C JSON; exec flows are "N1.P2->N3.P4" pin pairs, and a 1.x file keeps its "N1->N2" node pairs;
    a 1.x node's display `name` comes back as `title` (no `class`), its pin names stay the display names it has.
    A later 2.x document keeps its version, its unknown node, pin, graph and blueprint types and its unknown fields
    (card 0445), each listed in `warnings` (`code`, `path`, `value`, `message`)
*   `compile_blueprint(path=None)` - Compile the focused Blueprint (a `path` is ignored for now: open the Blueprint
    first); `data`: `blueprint_name`, `status`, `had_errors`, `had_warnings`, `error_count`, `warning_count` and
    `messages` (each `severity`, `message`, and `nodeGuid`, `nodeTitle`, `graph`, `pin` when it names a node)
*   `save_blueprint(path=None, only_if_dirty=True)` - Save the focused Blueprint (a `path` is ignored the same way);
    `data`: `blueprint_name`, `was_dirty`, `was_saved`
*   `load_blueprint(path)` - Load Blueprint by asset path; `data`: `name`, `path`, `class_name`, `parent_class`
    (the parent's name, such as `Actor`)
*   `open_blueprint(path, focus_graph="EventGraph")` - Open a Blueprint editor and focus a graph by its exact name
    (`EventGraph`, `UserConstructionScript`, a function or macro name); an unknown name opens the Blueprint with
    `focusedGraph` empty
*   `open_blueprint_function(name)` - Focus a function graph of the open Blueprint

**Graph Editing (the focused graph):**
*   `search_blueprint_nodes(term, context_sensitive=True, max_results=20, category="", exclude_vm_functions=True)` -
    Actions to add; each has `menuName` (also in `name` and `displayName`), `category`, `tooltip`, `keywords` and
    `spawnMetadata` (`actionIdentifier`: the stable spawner-signature ID, card 0277; `nodeName`). `category` filters
    on the menu category, ignoring case and spaces. `context_sensitive` True and False search the same list, the
    focused graph's (the editor's node menu with Context Sensitive off), and set only `isContextSensitive`; both need
    a focused graph (card 0434)
*   `add_node_to_graph(node_name, action_identifier, location_x=0, location_y=0)` - Add a node (pass a result's
    `spawnMetadata.nodeName` and `spawnMetadata.actionIdentifier`; pre-0277 search-text identifiers still work);
    returns `nodeGuid` and `inputPins`/`outputPins` with `pinGuid`, `pinName`
*   `connect_pins(connections, break_existing_links=False)` - `[{"from": {"nodeGuid", "pinGuid"}, "to": {...}}]`;
    without `break_existing_links` a connection that would replace a link (a linked data input, also through a
    conversion node, or a linked exec output) fails and names that link
*   `set_pin_value(node_guid, pin_guid, value, pin_name="")` - Default value of an unlinked input pin
*   `delete_nodes(node_guids, preserve_connections=False, force=False)` - Delete nodes
*   `find_nodes_in_graph(search_terms, search_type="keyword", case_sensitive=False, max_results=50)` - A keyword
    matches the node's title (as shown and its source text), class or member name; or `search_type="guid"`
*   `create_comment_node(node_guids, comment_text="Comment", color=None, font_size=18, move_mode="group", padding=50)`

Pick pins by `pinName` (`execute`, `then`, `ReturnValue`, `InString`, ...): it is the stable pin name, while
`displayName` is the editor's label.

Compiling (card 0151): the edit calls above and the function pin calls below compile the Blueprint once per call,
after the call's undo transaction, and return the compiler's diagnostics in `data.compile` (`success`,
`errorCount`, `warningCount`, `messages` with `nodeGuid` and `graph` when a message names a node). Inside
`run-python` they compile nothing themselves: the run-python tool call compiles each edited Blueprint once when the
script ends and reports it as `compile` in its own result, so a script that adds and wires 30 nodes compiles once.
`create_comment_node` never compiles; searches and finds compile nothing and refresh nothing.

**Function Pins (a function graph focused):**
*   `add_function_input_pin(name, type, default_value="", is_pass_by_reference=False, tooltip="")`,
    `add_function_return_pin(name, type, tooltip="")`, `remove_function_entry_pin(name)`,
    `remove_function_return_pin(name)`
    They run the MCP pin tools' own path (card 0156): return values go on every Return node, the function's call nodes
    are rebuilt after the skeleton class is regenerated, a refused edit leaves the Blueprint untouched, and the tooltip
    is kept as an `@param` line of the function's tooltip. See "Signature Edits and Call Sites" in
    `Source/Private/MCP/Tools/Implementations/Blueprint/Functions/AGENTS.md`

**Script Management:**
*   `list_scripts(category=None, limit=20)` - List scripts with metadata
*   `search_scripts(query, limit=10)` - Search by name/description/tags
*   `get_script(name)` - Get full script code and metadata
*   `get_script_functions(name)` - AST-based function signature extraction
*   `run_script(name, **kwargs)` - Execute script with parameters (keyword arguments become locals, and `params`);
    `data`: `script_name`, `result` (the script's `result` variable), `parameters_used`
*   `save_script(name, code, description, tags, category, parameters)` - Save new script
*   `delete_script(name)` - Remove script (project scripts only)
*   `get_script_stats()` - Library statistics

**NodeToCode-Specific:**
*   `tag_graph(tag, category="Default", description="")` - Tag focused graph
*   `list_tags(category=None, tag=None)` - List tags with filters
*   `remove_tag(graph_guid, tag)` - Remove tag from graph
*   `get_llm_providers()` - Get available LLM providers
*   `get_active_provider()` - Get current LLM provider info

**Utilities:**
*   `get_editor_subsystem(class)` - Safely get editor subsystem
*   `is_blueprint_valid(obj)` - Check if object is Blueprint
*   `get_project_content_dir()`, `get_project_dir()` - Path helpers
*   `log_info/warning/error(msg)` - Logging to UE output
*   `make_success_result(data)`, `make_error_result(error)` - Result formatting

## Dual-Path Script Search

The script system searches two locations, with project scripts taking precedence:

1. **Plugin bundled scripts** (read-only): `Plugins/NodeToCode/Content/Python/scripts/`
2. **Project user scripts** (read-write): `Content/Python/scripts/`

*   All list/search/get operations search **BOTH** locations
*   Project scripts override plugin scripts with the same name
*   Save/delete operations only affect **project** scripts
*   Each script entry includes `source: "plugin"` or `source: "project"`

## Script Registry

JSON index for fast script discovery:

```json
{
  "version": "1.0",
  "scripts": {
    "script_name": {
      "path": "category/script_name.py",
      "description": "Brief description",
      "tags": ["tag1", "tag2"],
      "category": "core",
      "parameters": []
    }
  },
  "categories": ["core", "utilities", "data", "ui"],
  "stats": {
    "total_scripts": 13,
    "last_updated": "2026-01-16T23:30:00Z"
  }
}
```

Writing and recovery (project registry; same pattern as `FN2CFileUtils`):

*   `_save_registry` writes `script_registry.json.tmp`, copies the previous file to `script_registry.json.bak` and
    `os.replace`s the temporary file over it. Unknown top-level fields are kept.
*   A project registry that does not parse (or is not an object with a `scripts` object) is copied to
    `script_registry.json.corrupt-<UTC yyyymmdd-hhmmss>` (once per distinct content), left in place, and
    `RegistryUnreadableError` is raised: list/search/get/save/delete return an error naming the file, `save_script`
    writes no `.py` file, and nothing writes over the registry until it is repaired or deleted. An unreadable file
    (locked, no permission) is refused the same way without a copy.
*   A complete `.tmp` left by an interrupted save, with no registry beside it, is moved into place on the next load.
*   The bundled plugin registry is read-only: if it does not parse, it is logged and treated as empty.
*   Headless check: `Tests/mcp_probe/e05_registry_check.py` (run with `-ExecutePythonScript`).
*   CPython tests, no editor: `Tests/python` (pytest with a stub `unreal` module; `build.ps1 -Test` runs them, and
    AGENTS.md gives the `uv run` command). `test_scripts.py` covers this module and `test_registry_integrity.py`
    checks that every bundled registry path exists and every bundled `.py` file is registered, so a script added
    under `Content/Python/scripts` needs its registry entry in the same change.

### Bundled Scripts

The list, with each script's path, description and tags, is `Content/Python/scripts/script_registry.json` (13
scripts in 4 categories when this was written; `test_registry_integrity.py` keeps it in step with the files), and
`n2c.list_scripts()` / `get-script-functions` read it at run time. By category:

*   **core**: `asset_iterator`, `blueprint_factory`, `variable_batch_creator`, `function_scaffolder`,
    `component_adder`, `ue5_compat` (UE5 Python API workarounds; run-python's error `hint` points at it)
*   **utilities**: `project_folder_scaffold`, `asset_duplicator`, `asset_validator`
*   **data**: `datatable_populator` (merges rows from JSON/CSV; `clear_existing=True` replaces, saves only with
    `save=True`), `struct_creator` (UserDefinedStruct row structs through `UN2CStructUtilsLibrary`)
*   **ui**: `utility_widget_launcher`, `asset_editor_opener`

`blueprint_factory.create_blueprint(name, ...)` uses the name as given; `create_actor_blueprint` and the other
`create_*_blueprint` helpers add their prefix (`BP_`, `WBP_`, ...) when it is missing.

## Token-Efficient Function Discovery

`get_script_functions(name)` uses AST parsing to extract function signatures without loading full implementation:

```python
result = n2c.get_script_functions("blueprint_factory")
# Returns: function names, parameters, type hints, docstrings, line numbers
# ~80% token savings vs get_script() for discovery phase
```

## C++ Bridge (UN2CPythonBridge)

Bridge class exposing NodeToCode functionality to Python via `unreal.N2CPythonBridge`:

*   **Location**: `Source/Private/MCP/Python/N2CPythonBridge.h/.cpp`
*   **UFunction Category**: `NodeToCode|Python`
*   **Functions**: every public `UFUNCTION` in the header, and each has a `nodetocode` wrapper (call the wrapper,
    which parses the JSON and checks arguments): `GetFocusedBlueprintJson`, `GetBlueprintN2CJson`,
    `ListBlueprintGraphs`, `ReadN2CJson`, `CompileFocusedBlueprint`,
    `SaveFocusedBlueprint` (`blueprint.py`); `TagFocusedGraph`, `ListTags`, `RemoveTag`, `GetLLMProviders`,
    `GetActiveProvider`, `OpenBlueprint`, `OpenBlueprintFunction` (`bridge.py`); `SearchBlueprintNodes`,
    `AddNodeToGraph`, `ConnectPins`, `SetPinValue`, `DeleteNodes`, `FindNodesInGraph`, `CreateCommentNode`
    (`graph.py`); `AddFunctionInputPin`, `AddFunctionReturnPin`, `RemoveFunctionEntryPin`, `RemoveFunctionReturnPin`
    (`functions.py`). The header comments become the Python docstrings (`help(unreal.N2CPythonBridge.open_blueprint)`).

All functions return JSON strings in format: `{"success": bool, "data": {...} or null, "error": "..." or null}`,
built by one helper with `TJsonWriter` (`MakeResultJson`), so every value is escaped; `utils._parse_bridge_result`
is the one parser.

## Struct Library (UN2CStructUtilsLibrary)

`unreal.N2CStructUtilsLibrary` (`Source/Public/BlueprintLibraries/N2CStructUtilsLibrary.h`) creates
UserDefinedStruct assets, for example DataTable row structs; `scripts/data/struct_creator.py` wraps it. Its
functions report through out parameters, and in Python a function with out parameters returns a tuple: the return
value first (when there is one), then the out parameters in order.

```python
import unreal

lib = unreal.N2CStructUtilsLibrary

# Create a new struct (the return value comes first, then the out parameters)
struct, success, error = lib.create_user_defined_struct('/Game/Data', 'F_MyRowStruct')
if not success:
    raise RuntimeError(error)

# Add variables to the struct; each call returns (success, error)
for name, var_type in [('MyString', 'FString'), ('MyFloat', 'float'), ('MyInt', 'int32')]:
    ok, error = lib.add_struct_variable(struct, name, var_type)
    if not ok:
        raise RuntimeError(error)

# A new struct starts with one default member; remove it once the struct has others, then save
lib.remove_default_variable(struct)
lib.save_struct(struct)
```

`add_struct_variables_batch` returns `(count, success, error)`; `get_struct_variable_names` returns the members'
internal names (`MyFloat_2_<GUID digits>`).

## MCP Python Tools

C++ tools in `Source/Private/MCP/Tools/Implementations/Python/`:

| Tool | Class | Description |
|------|-------|-------------|
| `run-python` | `FN2CMcpRunPythonTool` | Execute Python code in UE environment |
| `list-python-scripts` | `FN2CMcpListPythonScriptsTool` | List scripts by category |
| `search-python-scripts` | `FN2CMcpSearchPythonScriptsTool` | Search by name/description/tags |
| `get-python-script` | `FN2CMcpGetPythonScriptTool` | Get full script code and metadata |
| `get-script-functions` | `FN2CMcpGetScriptFunctionsTool` | Get function signatures via AST |
| `save-python-script` | `FN2CMcpSavePythonScriptTool` | Save script to library |
| `delete-python-script` | `FN2CMcpDeletePythonScriptTool` | Remove script from library |

**Base Class**: `FN2CMcpPythonScriptToolBase` - Common Python execution infrastructure

All Python tools require Game Thread execution (`RequiresGameThread() = true`).

Each tool answers with one text item holding JSON `{"success", "data", "error", ...}`. The script tools return the
`nodetocode` function's own `{success, data, error}` dict as it is (not wrapped in another `data`). When `success`
is false (the script raised or did not compile, or a script function reported an error) the
CallToolResult also has `isError: true`, so MCP clients see a tool execution failure; run-python's body then keeps
the error, `traceback`, `output`, `stderr` and a `hint`. Python being unavailable is an `isError` result whose text
is a plain message.

run-python returns what the code assigns to `result` as `data` (it must be JSON-serializable: anything else makes
the call fail with the `TypeError` as its error); printed text comes back as `output`. Its `timeout` argument
(default 60, at most 300) is accepted but not enforced: tool calls already run on the game thread, so the code runs
until it returns and blocks the editor meanwhile (card 0300).

Known limitation: the code goes to `ExecPythonCommandEx` in `ExecuteFile` mode, which treats a command whose first
`.py` is followed by whitespace or the end of the text as a file to run. So code whose first `.py` is, for example,
`see struct_creator.py for` in a comment fails with "Could not load Python file ..."; a `.py` followed by a quote,
`)` or another character (`"struct_creator.py"`) is fine. Keep `.py ` out of comments until that is fixed.

## Python-Only Mode

Setting: Project Settings > Plugins > Node to Code > MCP Server > **Tool Profile** = **Python scripting only**
(`UN2CSettings::McpToolProfile = EN2CMcpToolProfile::PythonScripting`, default `DefaultToolsets`; read when the MCP
server starts, so it needs an editor restart). It serves the `PythonTools` toolset only; Tool Exposure still applies
(profiles, toolsets and tool search are described in `Source/Private/MCP/Tools/AGENTS.md`). A config that saved the
former **Enable Python Script-Only Mode** (`bEnablePythonScriptOnlyMode=True`) loads as this profile (card 0165).

When enabled:
*   Only the `PythonTools` toolset's seven tools are served (`FN2CMcpToolManager::Configure`): `run-python`,
    `list-python-scripts`, `search-python-scripts`, `get-python-script`, `get-script-functions`,
    `save-python-script` and `delete-python-script`
*   No translation, LLM provider, Blueprint, Content Browser or file tools: `get-focused-blueprint` and
    `translate-focused-blueprint` answer "Unknown tool"
*   Without Allow MCP Python Execution (below) only four remain: list, search, get and `get-script-functions`, and
    the editor logs a warning at start
*   LLMs write/reuse Python scripts via `run-python` instead of using C++ tools
*   Designed for use with Context7 MCP for UE Python API documentation

## python-scripting Prompt

MCP prompt (`FN2CMcpPythonScriptingPrompt`) enforcing proper workflow:

*   **Location**: `Source/Public/MCP/Prompts/Implementations/N2CMcpCodeGenerationPrompt.h`
*   **Name**: `python-scripting`
*   **Arguments**:
    *   `task` (required) - What the script should accomplish
    *   `save_script` (optional) - Whether to save for reuse (yes/no/ask)
    *   `category` (optional) - Category for saved scripts

**Enforced Workflow:**
1. **Search existing scripts first** - Reuse over rewrite
2. **Research UE Python API** - Use Context7 to lookup `radial-hks/unreal-python-stubhub`
3. **Import and compose** - Use existing script functions
4. **Structure as modules** - Write reusable, well-documented scripts
5. **Save useful scripts** - Grow the library

**With Python execution off** (the per-user **Allow MCP Python Execution**, default off; card 0124): the prompt checks
`FN2CMcpToolManager::IsToolRegistered("run-python")` and, when it is not registered, opens with a "run-python is
disabled on this server" section naming the setting (Project Settings > Plugins > Node to Code (User) > MCP Server,
editor restart), says run-python, save-python-script and delete-python-script are unavailable, and tells the agent to
give the script to the user to run and not to claim it ran. Steps 1, 4 and 5 and the tool list say the same.

## Script Structure Guidelines

Scripts should follow this pattern:

```python
"""
Brief description of what the script does.

Usage:
    from scripts.category.script_name import main_function
    result = main_function(arg1, arg2)

Functions return standardized dicts: {success: bool, data: {...}, error: str|None}
"""

import unreal
from typing import Dict, Any, Optional


def main_function(arg1: str, arg2: int = 10) -> Dict[str, Any]:
    """
    Docstring describing the function.

    Args:
        arg1: Description of arg1
        arg2: Description of arg2 (default: 10)

    Returns:
        {success, data: {...}, error}
    """
    try:
        # Implementation
        return _make_success({"result": "value"})
    except Exception as e:
        return _make_error(str(e))


def _make_success(data: Any) -> Dict[str, Any]:
    return {"success": True, "data": data, "error": None}


def _make_error(message: str) -> Dict[str, Any]:
    return {"success": False, "data": None, "error": message}
```

## Enabling run-python over MCP

`run-python`, `save-python-script` and `delete-python-script` are only registered when the per-user setting
**Allow MCP Python Execution** (`UN2CUserSettings::bAllowMcpPythonExecution`, read through
`UN2CUserSettings::IsMcpPythonExecutionAllowed()`) is on; it is off by default, needs an editor restart, and only the
user's own Saved/Config value counts (a project's committed config cannot turn it on, card 0423). The Python scripting only profile without it leaves only the list, search, get and
`get-script-functions` tools.
