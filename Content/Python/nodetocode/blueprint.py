"""
Blueprint manipulation functions for NodeToCode.

These functions provide safe, error-handled wrappers around NodeToCode's C++ functionality
via the UN2CPythonBridge class. All functions return standardized result dictionaries
with 'success', 'data', and 'error' keys.
"""

import unreal
from typing import Dict, Any, Optional

from .utils import (
    make_success_result,
    make_error_result,
    log_info,
    log_error,
    _parse_bridge_result,
)


def get_focused_blueprint() -> Dict[str, Any]:
    """
    Get information about the currently focused Blueprint in the editor.

    Uses NodeToCode's C++ infrastructure to collect and serialize the Blueprint
    into N2CJSON format, which includes detailed node and connection information.
    Names in it are stable identifiers, the same in every editor language: each node's
    'class', 'member_parent' and 'member_name', and each pin's 'name' (its PinName, such
    as 'execute', 'then' or 'ReturnValue'). A node's 'title' and a pin's 'label' are
    English display hints, present only when they say more than the identifier.

    Like the MCP get-focused-blueprint tool, the editor's unwired default events (a new
    Actor Blueprint's greyed-out BeginPlay, ActorBeginOverlap and Tick) are included as
    nodes with 'disabled': True, so a script can find the default BeginPlay and wire it
    instead of adding a second one. A new Actor Blueprint's untouched EventGraph gives
    node_count 3 and those three nodes. Translation leaves them out.

    Returns:
        dict: {
            'success': bool,
            'data': {
                'name': str,           # Blueprint asset name
                'path': str,           # Full asset path
                'graph_name': str,     # Name of the focused graph
                'node_count': int,     # Number of nodes in the graph
                'n2c_json': dict,      # Full N2CJSON representation
            } or None,
            'error': str or None
        }

    Example:
        bp = get_focused_blueprint()
        if bp['success']:
            print(f"Editing: {bp['data']['name']}")
            print(f"Nodes: {bp['data']['node_count']}")
    """
    try:
        # Call the C++ bridge function
        result_json = unreal.N2CPythonBridge.get_focused_blueprint_json()
        return _parse_bridge_result(result_json)
    except Exception as e:
        log_error(f"get_focused_blueprint failed: {e}")
        return make_error_result(str(e))


def read_n2c_json(n2c_json: str) -> Dict[str, Any]:
    """
    Read N2C JSON text with NodeToCode's own reader, for example a saved N2C_BP_*.json translation file.

    Version 1.x documents (NodeToCode 1.2.x and earlier 2.0 builds) and 2.x documents are read into the current
    model and written back as N2C JSON of the current version (2.0.0), so a 1.x file comes back with the same nodes
    and flows and "version": "2.0.0". Exec flows are "N1.P2->N3.P4" (exec output pin -> exec input pin) in documents
    NodeToCode 2.0 writes; 1.x documents (and early 2.0 development builds) keep their node-only "N1->N2" entries.
    A 1.x node's display "name" is written back as its "title" (1.x has no "class"), and 1.x pin names, which are
    display names, stay as they are.

    Args:
        n2c_json: The N2C JSON text

    Returns:
        dict: {
            'success': bool,
            'data': {
                'source_version': str,  # The document's own "version"
                'version': str,         # The version of the model it was read into
                'valid': bool,          # Whether the model passes NodeToCode's validator
                'graphs': int,
                'nodes': int,
                'data_edges': int,      # Data connections (an output feeding two inputs counts twice)
                'exec_edges': int,      # Exec links ("N1.P2->N3.P4" is one; a 1.x chain "N1->N2->N3" is two)
                'n2c_json': dict,       # The document written again as current N2C JSON
            } or None,
            'error': str or None
        }

    Example:
        text = open(path, encoding='utf-8').read()
        result = read_n2c_json(text)
        if result['success']:
            print(result['data']['source_version'], '->', result['data']['version'])
    """
    try:
        result_json = unreal.N2CPythonBridge.read_n2c_json(n2c_json)
        return _parse_bridge_result(result_json)
    except Exception as e:
        log_error(f"read_n2c_json failed: {e}")
        return make_error_result(str(e))


def compile_blueprint(blueprint_path: Optional[str] = None) -> Dict[str, Any]:
    """
    Compile a Blueprint. If no path is provided, compiles the focused Blueprint.

    Note: Currently only supports compiling the focused Blueprint.
    Path-based compilation will be added in a future update.

    Args:
        blueprint_path: Optional asset path (currently ignored, uses focused Blueprint)

    Returns:
        dict: {
            'success': bool,        # True if compilation succeeded without errors
            'data': {
                'blueprint_name': str,
                'status': str,       # Post-compilation status
                'had_errors': bool,
                'had_warnings': bool,
            } or None,
            'error': str or None
        }

    Example:
        result = compile_blueprint()
        if result['success']:
            print("Compilation successful!")
        elif result['data'] and result['data']['had_errors']:
            print("Compilation failed with errors")
    """
    try:
        if blueprint_path:
            log_info(f"Note: blueprint_path parameter not yet implemented, using focused Blueprint")

        # Call the C++ bridge function
        result_json = unreal.N2CPythonBridge.compile_focused_blueprint()
        return _parse_bridge_result(result_json)
    except Exception as e:
        log_error(f"compile_blueprint failed: {e}")
        return make_error_result(str(e))


def save_blueprint(blueprint_path: Optional[str] = None,
                   only_if_dirty: bool = True) -> Dict[str, Any]:
    """
    Save a Blueprint to disk. If no path is provided, saves the focused Blueprint.

    Note: Currently only supports saving the focused Blueprint.
    Path-based saving will be added in a future update.

    Args:
        blueprint_path: Optional asset path (currently ignored, uses focused Blueprint)
        only_if_dirty: If True, only saves if the Blueprint has unsaved changes.
                      Default is True to avoid unnecessary disk writes.

    Returns:
        dict: {
            'success': bool,
            'data': {
                'blueprint_name': str,
                'was_dirty': bool,
                'was_saved': bool,
            } or None,
            'error': str or None
        }

    Example:
        result = save_blueprint()
        if result['success'] and result['data']['was_saved']:
            print("Blueprint saved to disk")
    """
    try:
        if blueprint_path:
            log_info(f"Note: blueprint_path parameter not yet implemented, using focused Blueprint")

        # Call the C++ bridge function
        result_json = unreal.N2CPythonBridge.save_focused_blueprint(only_if_dirty)
        return _parse_bridge_result(result_json)
    except Exception as e:
        log_error(f"save_blueprint failed: {e}")
        return make_error_result(str(e))


def load_blueprint(blueprint_path: str) -> Dict[str, Any]:
    """
    Load a Blueprint asset by path.

    Args:
        blueprint_path: Asset path to the Blueprint (e.g., '/Game/BP_MyActor')

    Returns:
        dict: {
            'success': bool,
            'data': {
                'name': str,
                'path': str,
                'class_name': str,
                'parent_class': str,   # the parent class's name, e.g. 'Actor' ('' if unknown)
            } or None,
            'error': str or None
        }

    Example:
        bp = load_blueprint('/Game/Blueprints/BP_Character')
        if bp['success']:
            print(f"Loaded: {bp['data']['name']}")
    """
    try:
        if not blueprint_path:
            return make_error_result('blueprint_path is required')

        blueprint = unreal.load_asset(blueprint_path)
        if not blueprint or not isinstance(blueprint, unreal.Blueprint):
            return make_error_result(f'Blueprint not found at path: {blueprint_path}')

        return make_success_result({
            'name': blueprint.get_name(),
            'path': blueprint.get_path_name(),
            'class_name': blueprint.get_class().get_name(),
            'parent_class': _parent_class_name(blueprint),
        })

    except Exception as e:
        log_error(f"load_blueprint failed: {e}")
        return make_error_result(str(e))


def _parent_class_name(blueprint) -> str:
    """
    The name of a Blueprint's parent class ('Actor'), or '' when it cannot be read.

    UBlueprint::ParentClass is not exposed to Python ('blueprint.parent_class' and
    get_editor_property('parent_class') both fail), so this uses
    BlueprintEditorLibrary.get_blueprint_parent_class where it exists (UE 5.8) and otherwise the
    asset's 'ParentClass' registry tag ("/Script/CoreUObject.Class'/Script/Engine.Actor'").
    """
    library = getattr(unreal, 'BlueprintEditorLibrary', None)
    get_parent = getattr(library, 'get_blueprint_parent_class', None) if library else None
    if get_parent:
        parent = get_parent(blueprint)
        return parent.get_name() if parent else ''

    asset_data = unreal.EditorAssetLibrary.find_asset_data(blueprint.get_path_name())
    tag = str(asset_data.get_tag_value('ParentClass') or '')
    # "/Script/CoreUObject.Class'/Script/Engine.Actor'" -> "Actor"; a bare "/Script/Engine.Actor" also works
    path = tag.split("'")[1] if tag.count("'") >= 2 else tag
    return path.rsplit('.', 1)[-1] if path and path != 'None' else ''
