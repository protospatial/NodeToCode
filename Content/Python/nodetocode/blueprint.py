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
                'parent_class': str,
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

        # Get parent class info
        parent_class_name = ''
        if blueprint.parent_class:
            parent_class_name = blueprint.parent_class.get_name()

        return make_success_result({
            'name': blueprint.get_name(),
            'path': blueprint.get_path_name(),
            'class_name': blueprint.get_class().get_name(),
            'parent_class': parent_class_name,
        })

    except Exception as e:
        log_error(f"load_blueprint failed: {e}")
        return make_error_result(str(e))
