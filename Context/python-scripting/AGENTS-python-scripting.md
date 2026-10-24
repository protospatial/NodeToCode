# NodeToCode Python Scripting

MCP Bridge and Python script management system for LLM-powered UE automation.

## Directory Structure

```
Content/Python/
├── mcp_bridge/
│   ├── nodetocode_bridge.py    - Stdio-to-HTTP MCP bridge
│   ├── launch_bridge.sh        - Mac/Linux launcher script
│   └── ue_python_finder.py     - UE Python installation finder
│
├── nodetocode/                 - Core Python module (import nodetocode as n2c)
│   ├── __init__.py             - Module exports
│   ├── scripts.py              - Script CRUD with dual-path search
│   ├── bridge.py               - C++ bridge wrappers (tagging, LLM info)
│   ├── blueprint.py            - Blueprint operations
│   └── utils.py                - Logging and result formatting
│
└── scripts/                    - Script library (bundled + user)
    ├── script_registry.json    - Metadata index
    ├── core/                   - Core utility scripts
    ├── utilities/              - Project utilities
    ├── data/                   - Data manipulation scripts
    └── ui/                     - UI/Widget scripts
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

# Blueprint operations
bp = n2c.get_focused_blueprint()
if bp['success']:
    print(f"Blueprint: {bp['data']['name']}")

n2c.compile_blueprint()
n2c.save_blueprint()

# Script management
matches = n2c.search_scripts("health system")
result = n2c.run_script("create_health_system", max_hp=100)

# Tagging system
n2c.tag_graph("player_controller", category="Systems")
tags = n2c.list_tags()
```

### Module Files

| File | Purpose |
|------|---------|
| `__init__.py` | Module exports, version info (`__version__ = "1.0.0"`) |
| `scripts.py` | Script CRUD with dual-path search (plugin + project) |
| `bridge.py` | C++ bridge wrappers for tagging and LLM provider info |
| `blueprint.py` | Blueprint get/compile/save/load via `UN2CPythonBridge` |
| `utils.py` | Logging (`log_info`, `log_warning`, `log_error`), result helpers |

### Available Functions

**Blueprint Operations:**
*   `get_focused_blueprint()` - Get N2CJSON for focused Blueprint
*   `compile_blueprint(path=None)` - Compile Blueprint (focused if no path)
*   `save_blueprint(path=None, only_if_dirty=True)` - Save to disk
*   `load_blueprint(path)` - Load Blueprint by asset path

**Script Management:**
*   `list_scripts(category=None, limit=20)` - List scripts with metadata
*   `search_scripts(query, limit=10)` - Search by name/description/tags
*   `get_script(name)` - Get full script code and metadata
*   `get_script_functions(name)` - AST-based function signature extraction
*   `run_script(name, **kwargs)` - Execute script with parameters
*   `save_script(name, code, description, tags, category, parameters)` - Save new script
*   `delete_script(name)` - Remove script (project scripts only)
*   `get_script_stats()` - Library statistics

**NodeToCode-Specific:**
*   `tag_graph(tag, category, description)` - Tag focused graph
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
    "total_scripts": 10,
    "last_updated": "2026-01-06T00:00:00Z"
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

### Bundled Scripts (10 scripts, 4 categories)

| Script | Category | Description |
|--------|----------|-------------|
| `asset_iterator` | core | Find and iterate assets by type/path/filter |
| `blueprint_factory` | core | Create Blueprints from any parent class |
| `variable_batch_creator` | core | Add multiple member variables to Blueprints |
| `function_scaffolder` | core | Add function graphs with configuration |
| `component_adder` | core | Add/manage components in Actor Blueprints |
| `project_folder_scaffold` | utilities | Create standardized folder structures |
| `asset_duplicator` | utilities | Clone assets with renaming/batch support |
| `asset_validator` | utilities | Validate naming conventions/compilation |
| `datatable_populator` | data | Merge rows into DataTables from JSON/CSV (`clear_existing=True` replaces; saves only with `save=True`) |
| `utility_widget_launcher` | ui | Launch Editor Utility Widgets programmatically |

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
*   **Functions**:
    *   `GetFocusedBlueprintJson()` - Get Blueprint as N2CJSON
    *   `CompileFocusedBlueprint()` - Compile focused Blueprint
    *   `SaveFocusedBlueprint(bOnlyIfDirty)` - Save to disk
    *   `TagFocusedGraph(Tag, Category, Description)` - Apply tag
    *   `ListTags(Category, Tag)` - List/filter tags
    *   `RemoveTag(GraphGuid, Tag)` - Remove tag
    *   `GetLLMProviders()` - Get provider list
    *   `GetActiveProvider()` - Get current provider

All functions return JSON strings in format: `{"success": bool, "data": {...}, "error": "..."}`

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
the error, `traceback`, `output`, `stderr` and a `hint`. A timeout, or Python being unavailable, is an
`isError` result whose text is a plain message.

## Python-Only Mode

Setting in Plugin Settings: `bEnablePythonScriptOnlyMode` (default: `true`)

When enabled:
*   Most C++ MCP tools are disabled
*   Only essential tools remain: `run-python`, translation tools, LLM provider tools
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
**Allow MCP Python Execution** (`UN2CUserSettings::bAllowMcpPythonExecution`) is on; it is off by default and
needs an editor restart. Python script-only mode without it leaves only the list, search and get script tools.
