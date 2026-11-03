"""
Utility functions for NodeToCode Python module.

Provides common helpers for editor subsystem access, Blueprint validation,
and logging operations.
"""

import json
import os
import re
import sys
import types
import uuid

import unreal
from typing import Optional, Any, Callable, Dict, TypeVar

T = TypeVar('T')


def get_editor_subsystem(subsystem_class: type) -> Optional[Any]:
    """
    Safely get an editor subsystem instance.

    Args:
        subsystem_class: The unreal subsystem class to retrieve
                        (e.g., unreal.AssetEditorSubsystem)

    Returns:
        The subsystem instance or None if not available

    Example:
        subsystem = get_editor_subsystem(unreal.EditorAssetSubsystem)
        if subsystem:
            exists = subsystem.does_asset_exist('/Game/Blueprints/BP_Player')
    """
    try:
        return unreal.get_editor_subsystem(subsystem_class)
    except Exception:
        return None


def is_blueprint_valid(blueprint: Any) -> bool:
    """
    Check if an object is a valid Blueprint.

    Args:
        blueprint: The object to check

    Returns:
        True if the object is a valid Blueprint, False otherwise
    """
    if blueprint is None:
        return False

    try:
        return isinstance(blueprint, unreal.Blueprint)
    except Exception:
        return False


def get_project_content_dir() -> str:
    """
    Get the project's Content directory path.

    Returns:
        The absolute path to the project's Content directory
    """
    return unreal.Paths.project_content_dir()


def get_project_dir() -> str:
    """
    Get the project's root directory path.

    Returns:
        The absolute path to the project's root directory
    """
    return unreal.Paths.project_dir()


def log_info(message: str) -> None:
    """
    Log an info message to Unreal's output log.

    Args:
        message: The message to log
    """
    unreal.log(f"[NodeToCode] {message}")


def log_warning(message: str) -> None:
    """
    Log a warning message to Unreal's output log.

    Args:
        message: The message to log
    """
    unreal.log_warning(f"[NodeToCode] {message}")


def log_error(message: str) -> None:
    """
    Log an error message to Unreal's output log.

    Args:
        message: The message to log
    """
    unreal.log_error(f"[NodeToCode] {message}")


def make_success_result(data: Any = None) -> dict:
    """
    Create a standardized success result dictionary.

    Args:
        data: Optional data to include in the result

    Returns:
        dict with success=True, data, and error=None
    """
    return {
        'success': True,
        'data': data,
        'error': None
    }


def make_error_result(error: str, data: Any = None) -> dict:
    """
    Create a standardized error result dictionary.

    Args:
        error: Error message describing what went wrong
        data: Optional partial data to include

    Returns:
        dict with success=False, data, and error message
    """
    return {
        'success': False,
        'data': data,
        'error': error
    }


def _parse_bridge_result(json_str: str) -> Dict[str, Any]:
    """
    Parse the JSON a UN2CPythonBridge function returns.

    The bridge builds every response with one C++ helper (a TJsonWriter over an FJsonObject), so the text is
    always {"success": bool, "data": ... or null, "error": str or null}. This is the only parser; blueprint.py,
    bridge.py, functions.py and graph.py import it.

    Args:
        json_str: JSON string returned by the bridge function

    Returns:
        Parsed dictionary with success, data, and error keys, or an error result when the text is empty,
        not JSON, or not a JSON object
    """
    if not json_str:
        return make_error_result("Empty response from bridge function")

    try:
        result = json.loads(json_str)
    except json.JSONDecodeError as e:
        log_error(f"Failed to parse bridge response: {e}")
        return make_error_result(f"Failed to parse bridge response: {e}")

    if not isinstance(result, dict):
        log_error(f"Bridge response is not a JSON object: {type(result).__name__}")
        return make_error_result(f"Bridge response is not a JSON object: {type(result).__name__}")

    return result


# ============== Editor subsystem (card 0301) ==============
#
# UN2CEditorSubsystem holds NodeToCode's typed, path-based Blueprint functions: each returns a struct and raises a
# RuntimeError naming the path on failure. The nodetocode functions in SUBSYSTEM call it and keep returning
# {success, data, error} dicts.

# nodetocode function -> UN2CEditorSubsystem function it calls
SUBSYSTEM = {
    'get_blueprint_n2cjson': 'get_blueprint_n2c_json',
    'list_blueprint_graphs': 'list_blueprint_graphs',
}


def editor_subsystem() -> Any:
    """unreal.N2CEditorSubsystem, NodeToCode's typed Blueprint functions (card 0301)"""
    return unreal.get_editor_subsystem(unreal.N2CEditorSubsystem)


def _subsystem_error(error: Exception, path: str) -> str:
    """The text of an UN2CEditorSubsystem error from its code on ("GRAPH_NOT_FOUND: ..."), without the function and
    path it starts with ("<Function>: <path>: <code>: ...")"""
    text = str(error)
    marker = f'{path}: ' if path else ''
    index = text.find(marker) if marker else -1
    return text[index + len(marker):] if index >= 0 else text


# ============== Tool catalog (card 0342) ==============
#
# The nodetocode functions below run NodeToCode's MCP tools through UN2CPythonBridge::InvokeTool, so Python gets
# the tools' own behaviour (pin matching, compile reporting, the Play In Editor rule) and a fix lands once. Each
# function returns what the tool returns, plus the keys the 1.x bridge copy returned, for one release.

# nodetocode function -> MCP tool it runs (Context/mcp-server/tools.generated.md)
TOOL_NAMES = {
    'search_blueprint_nodes': 'search-blueprint-nodes',
    'add_node_to_graph': 'add-bp-node-to-active-graph',
    'connect_pins': 'connect-pins',
    'set_pin_value': 'set-input-pin-value',
    'delete_nodes': 'delete-blueprint-node',
    'find_nodes_in_graph': 'find-nodes-in-graph',
    'create_comment_node': 'create-comment-node',
    'add_function_input_pin': 'add-function-input-pin',
    'add_function_return_pin': 'add-function-return-pin',
    'remove_function_entry_pin': 'remove-function-entry-pin',
    'remove_function_return_pin': 'remove-function-return-pin',
    'compile_blueprint': 'compile-blueprint',
    'save_blueprint': 'save-blueprint',
    'tag_graph': 'tag-blueprint-graph',
    'open_blueprint': 'open-blueprint-asset',
    'open_blueprint_function': 'open-blueprint-function',
    'get_blueprint_dependencies': 'get-blueprint-dependencies',
}

# nodetocode functions that still call their own UN2CPythonBridge function, and why (card 0343 retires those)
BRIDGE_ONLY = {
    'get_focused_blueprint': 'get-focused-blueprint returns N2C JSON with ids objects, not the n2c_json document',
    'list_tags': 'list-blueprint-tags cannot filter by Blueprint (blueprint_path)',
    'remove_tag': 'remove-tag-from-graph takes no Blueprint, so a duplicated Blueprint\'s tags cannot be told apart',
    'get_llm_providers': 'get-available-llm-providers lists configured providers only',
}

LEGACY_NOTE = ("the 1.x key is kept for one release; read the tool's key instead (Context/mcp-server/tools.generated.md)")

_warned_legacy = set()


def _warn_legacy(function: str, key: str, replacement: str) -> None:
    """Logs once per function and key that a script read a deprecated 1.x result key"""
    if (function, key) in _warned_legacy:
        return
    _warned_legacy.add((function, key))
    log_warning(f"nodetocode.{function}: '{key}' is deprecated, {LEGACY_NOTE}: use {replacement}")


class LegacyKeys(dict):
    """A tool result dict that also holds the 1.x bridge keys (card 0342). Reading one of those with [] or get()
    logs a deprecation warning once; iterating, json.dumps and 'in' see them as ordinary keys."""

    def __init__(self, data: Dict[str, Any], function: str, legacy: Dict[str, Any]):
        super().__init__(data)
        self._function = function
        self._legacy = {}
        for key, (value, replacement) in legacy.items():
            if key not in data:
                dict.__setitem__(self, key, value)
                self._legacy[key] = replacement

    def __getitem__(self, key):
        if key in self._legacy:
            _warn_legacy(self._function, key, self._legacy[key])
        return dict.__getitem__(self, key)

    def get(self, key, default=None):
        if key in self._legacy and key in self:
            _warn_legacy(self._function, key, self._legacy[key])
        return dict.get(self, key, default)

    def __reduce__(self):
        return (dict, (dict(self),))


def legacy_guid(guid: Any) -> str:
    """A GUID as the 1.x bridge wrote it: 32 hex digits, no hyphens"""
    return str(guid or '').replace('-', '').replace('{', '').replace('}', '').upper()


def _is_guid(text: str) -> bool:
    try:
        uuid.UUID(text)
        return True
    except (ValueError, AttributeError, TypeError):
        return False


def _target_args(blueprint_path: str = "", graph: str = "") -> Dict[str, str]:
    """A tool's target arguments (card 0274) from a wrapper's blueprint_path and graph: graph is a GUID (graphGuid)
    or a name (graphName). Only the ones given, so a call without them acts on the focused graph."""
    args = {}
    if blueprint_path and blueprint_path.strip():
        args['blueprintPath'] = blueprint_path.strip()
    graph_text = (graph or '').strip()
    if graph_text:
        args['graphGuid' if _is_guid(graph_text) else 'graphName'] = graph_text
    return args


def invoke_tool(tool_name: str, arguments: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """
    Run one of NodeToCode's MCP tools by name, with the tool's own arguments (card 0342).

    Every registered tool runs, whatever the MCP server serves, except run-python (you are in Python already). A translation tool is refused with code NOT_RUNNABLE_HERE: it runs longer than a call.

    Args:
        tool_name: The tool's name, e.g. 'list-blueprint-functions' (Context/mcp-server/tools.generated.md)
        arguments: The tool's arguments as a dict, e.g. {'blueprintPath': '/Game/BP_Door'}

    Returns:
        {success, data: the tool's JSON value (or its text when it is not JSON), error: the tool's error text,
         code: 'UNKNOWN_TOOL' | 'INVALID_ARGUMENT' | 'PIE_ACTIVE' | 'NOT_RUNNABLE_HERE' | 'TOOL_FAILED' | ... on a
         failure}

    Example:
        functions = invoke_tool('list-blueprint-functions', {'blueprintPath': '/Game/BP_Door'})
    """
    if not tool_name or not str(tool_name).strip():
        return make_error_result("tool_name cannot be empty")
    try:
        arguments_json = json.dumps(arguments or {})
    except (TypeError, ValueError) as e:
        return make_error_result(f"arguments are not JSON: {e}")
    try:
        return _parse_bridge_result(unreal.N2CPythonBridge.invoke_tool(str(tool_name).strip(), arguments_json))
    except Exception as e:
        log_error(f"invoke_tool({tool_name}) failed: {e}")
        return make_error_result(str(e))


def _invoke(function: str, arguments: Dict[str, Any], legacy: Optional[Callable[[Dict[str, Any]], Dict[str, Any]]] = None) -> Dict[str, Any]:
    """
    Runs the tool TOOL_NAMES gives for a nodetocode function and returns {success, data, error}.

    A tool that reports a failure inside its value ("success": false, as compile-blueprint does for compile errors)
    gives success False, its message as the error and its value as data. legacy(data) returns the 1.x bridge keys
    as {key: (value, replacement)}; they are added to data (LegacyKeys) and never replace the tool's own keys.
    """
    result = invoke_tool(TOOL_NAMES[function], arguments)
    data = result.get('data')
    if not isinstance(data, dict):
        return result
    if result.get('success') and data.get('success') is False:
        result['success'] = False
        result['error'] = data.get('message') or data.get('error') or f"{TOOL_NAMES[function]} failed"
    if legacy is not None:
        try:
            result['data'] = LegacyKeys(data, function, legacy(data))
        except Exception as e:  # the tool's own result is never lost over the compatibility keys
            log_warning(f"nodetocode.{function}: the 1.x result keys could not be added: {e}")
    return result


# ============== Agent Skill scripts (card 0306) ==============

SKILL_NAME_RE = re.compile(r'^[a-z0-9]+(-[a-z0-9]+)*$')
SCRIPT_NAME_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')


def skills_dir() -> str:
    """The Agent Skills NodeToCode ships: <plugin>/Agents/claude-code/nodetocode/skills (the nodetocode package is
    <plugin>/Content/Python/nodetocode), wherever the plugin is installed. The package's real path is used, so a
    plugin whose Content folder is a link (build.ps1's host projects link only Source, Content, Resources and
    ThirdParty) still finds the Agents folder beside the real one."""
    plugin_root = os.path.normpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..', '..'))
    return os.path.join(plugin_root, 'Agents', 'claude-code', 'nodetocode', 'skills')


def skill_script(skill: str, script: Optional[str] = None):
    """
    Loads the Python script of one of NodeToCode's Agent Skills and returns it as a module (card 0306), so a
    run-python call can use it:

        validator = nodetocode.skill_script('asset-validator')
        issues = validator.check_naming('/Game/Characters')

    Args:
        skill: The skill's name ('asset-iterator', 'asset-validator', 'project-folder-scaffold')
        script: The file in the skill's scripts/ folder without .py; default the skill name with '_' for '-'

    Returns:
        The module (loaded once per editor session; reloaded when the file changed). Raises ValueError for a name
        that is not a skill or script name and FileNotFoundError when the file does not exist.
    """
    if not isinstance(skill, str) or not SKILL_NAME_RE.match(skill):
        raise ValueError(f"'{skill}' is not an Agent Skill name (lowercase letters, digits and hyphens)")
    name = script if script else skill.replace('-', '_')
    if not isinstance(name, str) or not SCRIPT_NAME_RE.match(name):
        raise ValueError(f"'{script}' is not a script name (a Python identifier, no .py)")
    path = os.path.join(skills_dir(), skill, 'scripts', name + '.py')
    if not os.path.isfile(path):
        raise FileNotFoundError(f'no script {name}.py in the {skill} skill ({path})')
    module_name = f'nodetocode_skill_{skill.replace("-", "_")}_{name}'
    mtime = os.path.getmtime(path)
    module = sys.modules.get(module_name)
    if module is not None and getattr(module, '__n2c_mtime__', None) == mtime:
        return module
    with open(path, encoding='utf-8') as f:
        source = f.read()
    module = types.ModuleType(module_name)
    module.__file__ = path
    sys.modules[module_name] = module
    try:
        # Compiled here, not through an import loader, so no __pycache__ is written into the plugin's skills folder
        exec(compile(source, path, 'exec'), module.__dict__)
    except BaseException:
        sys.modules.pop(module_name, None)
        raise
    module.__n2c_mtime__ = mtime
    return module
