"""
Python wrappers for Blueprint function pin management.

These functions enable programmatic manipulation of function parameters:
- Add input parameters to functions
- Add return values to functions
- Remove input parameters
- Remove return values

Usage:
    import nodetocode as n2c

    # Add an input parameter to the focused function
    result = n2c.add_function_input_pin(
        pin_name="TargetActor",
        type_identifier="/Script/Engine.Actor",
        is_pass_by_reference=True
    )

    # Add a return value
    result = n2c.add_function_return_pin(
        pin_name="bSuccess",
        type_identifier="bool"
    )

    # Remove pins
    n2c.remove_function_entry_pin("OldParameter")
    n2c.remove_function_return_pin("UnusedReturn")
"""

from typing import Dict, Any, Optional

import unreal

from .utils import make_success_result, make_error_result, log_info, log_warning, log_error, _parse_bridge_result, _target_kwargs


# ============== Function Input Pins ==============

def add_function_input_pin(
    pin_name: str,
    type_identifier: str,
    default_value: str = "",
    is_pass_by_reference: bool = False,
    tooltip: str = "",
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Add an input parameter to the currently focused Blueprint function.

    The function graph must be focused in the Blueprint editor.

    Args:
        pin_name: Name for the new input parameter
        type_identifier: Type identifier, e.g.:
            - Primitives: "bool", "int32", "float", "FString", "FName", "FText"
            - Vectors: "FVector", "FVector2D", "FRotator", "FTransform"
            - Objects: "/Script/Engine.Actor", "/Script/Engine.StaticMeshComponent"
        default_value: Optional default value for the parameter
        is_pass_by_reference: Whether the parameter is passed by reference
        tooltip: Optional tooltip description for the parameter, kept as an "@param <name> <text>" line of the
            function's tooltip (shown on the pin of every call node)
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                pinName: str,       # Requested name
                actualName: str,    # Actual name (may differ if made unique)
                pinGuid: str,       # Unique pin identifier
                functionName: str,  # Name of the function
                typeIdentifier: str # Resolved type
            },
            error: str or None
        }

    Example:
        # Add a simple boolean parameter
        result = add_function_input_pin("bEnabled", "bool", default_value="true")

        # Add an Actor reference parameter
        result = add_function_input_pin(
            pin_name="TargetActor",
            type_identifier="/Script/Engine.Actor",
            is_pass_by_reference=True,
            tooltip="The actor to affect"
        )
    """
    if not pin_name or not pin_name.strip():
        return make_error_result("pin_name cannot be empty")

    if not type_identifier or not type_identifier.strip():
        return make_error_result("type_identifier cannot be empty")

    try:
        result = unreal.N2CPythonBridge.add_function_input_pin(
            pin_name.strip(),
            type_identifier.strip(),
            default_value,
            is_pass_by_reference,
            tooltip,
            **_target_kwargs(blueprint_path, graph)
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"add_function_input_pin failed: {e}")
        return make_error_result(str(e))


def remove_function_entry_pin(pin_name: str, blueprint_path: str = "", graph: str = "") -> Dict[str, Any]:
    """
    Remove an input parameter from the currently focused Blueprint function.

    Args:
        pin_name: Name of the pin to remove (internal name or display name)
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                removedPin: str,
                functionName: str
            },
            error: str or None
        }

    Example:
        result = remove_function_entry_pin("OldParameter")
        if result['success']:
            print(f"Removed pin from {result['data']['functionName']}")
    """
    if not pin_name or not pin_name.strip():
        return make_error_result("pin_name cannot be empty")

    try:
        result = unreal.N2CPythonBridge.remove_function_entry_pin(pin_name.strip(), **_target_kwargs(blueprint_path, graph))
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"remove_function_entry_pin failed: {e}")
        return make_error_result(str(e))


# ============== Function Return Pins ==============

def add_function_return_pin(
    pin_name: str,
    type_identifier: str,
    tooltip: str = "",
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Add a return value to the currently focused Blueprint function.

    Blueprint functions can have multiple return values (unlike C++).
    The value is added to every Return node of the function (a Return node wired from the entry is created when
    there is none), and the function's call nodes are rebuilt against the new signature.
    The function graph must be focused in the Blueprint editor.

    Args:
        pin_name: Name for the new return value
        type_identifier: Type identifier, e.g.:
            - Primitives: "bool", "int32", "float", "FString"
            - Vectors: "FVector", "FRotator", "FTransform"
            - Objects: "/Script/Engine.Actor"
        tooltip: Optional tooltip description for the return value, kept as for add_function_input_pin
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                pinName: str,       # Requested name
                actualName: str,    # Actual name (may differ if made unique)
                pinGuid: str,       # Unique pin identifier
                functionName: str,  # Name of the function
                typeIdentifier: str # Resolved type
            },
            error: str or None
        }

    Example:
        # Add a boolean return value
        result = add_function_return_pin(
            pin_name="bSuccess",
            type_identifier="bool",
            tooltip="True if the operation succeeded"
        )

        # Add a found actor return value
        result = add_function_return_pin(
            pin_name="FoundActor",
            type_identifier="/Script/Engine.Actor"
        )
    """
    if not pin_name or not pin_name.strip():
        return make_error_result("pin_name cannot be empty")

    if not type_identifier or not type_identifier.strip():
        return make_error_result("type_identifier cannot be empty")

    try:
        result = unreal.N2CPythonBridge.add_function_return_pin(
            pin_name.strip(),
            type_identifier.strip(),
            tooltip,
            **_target_kwargs(blueprint_path, graph)
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"add_function_return_pin failed: {e}")
        return make_error_result(str(e))


def remove_function_return_pin(pin_name: str, blueprint_path: str = "", graph: str = "") -> Dict[str, Any]:
    """
    Remove a return value from the currently focused Blueprint function.

    Args:
        pin_name: Name of the return pin to remove (internal name or display name)
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                removedPin: str,
                functionName: str
            },
            error: str or None
        }

    Example:
        result = remove_function_return_pin("UnusedReturn")
        if result['success']:
            print(f"Removed return pin from {result['data']['functionName']}")
    """
    if not pin_name or not pin_name.strip():
        return make_error_result("pin_name cannot be empty")

    try:
        result = unreal.N2CPythonBridge.remove_function_return_pin(pin_name.strip(), **_target_kwargs(blueprint_path, graph))
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"remove_function_return_pin failed: {e}")
        return make_error_result(str(e))
