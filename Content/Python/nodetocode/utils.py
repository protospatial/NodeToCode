"""
Utility functions for NodeToCode Python module.

Provides common helpers for editor subsystem access, Blueprint validation,
and logging operations.
"""

import json

import unreal
from typing import Optional, Any, Dict, TypeVar

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
        subsystem = get_editor_subsystem(unreal.AssetEditorSubsystem)
        if subsystem:
            assets = subsystem.get_all_edited_assets()
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
