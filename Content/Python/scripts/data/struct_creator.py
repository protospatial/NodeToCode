"""
UserDefinedStruct creation utilities for Unreal Engine.

Create and modify UserDefinedStruct assets programmatically, enabling
automated DataTable row structure creation without manual editor interaction.

Usage:
    from scripts.data.struct_creator import create_struct_with_variables, create_datatable_row_struct

    # Create a simple struct with variables
    result = create_struct_with_variables(
        '/Game/Data',
        'F_MyStruct',
        [
            ('Name', 'FString'),
            ('Value', 'float'),
            ('Count', 'int32')
        ]
    )

    # Create a DataTable row struct and the DataTable in one call
    result = create_datatable_row_struct(
        '/Game/Data',
        'SpawnConfig',
        [
            ('SpawnClassName', 'FString'),
            ('SpawnDelay', 'float'),
            ('MaxSpawns', 'int32'),
            ('SpawnLocationTag', 'FString'),
            ('WaveNumber', 'int32')
        ]
    )

Functions return standardized dicts: {success: bool, data: {...}, error: str|None}

Supported variable types:
    - Primitives: bool, int32, int64, float, double, byte
    - Strings: FString, FName, FText
    - Vectors: FVector, FVector2D, FRotator, FTransform
    - Colors: FColor, FLinearColor
    - Objects: Full path like '/Script/Engine.Actor' or class name like 'Actor'
    - Structs: Full path like '/Script/CoreUObject.Vector' or struct name
"""

import unreal
from typing import List, Tuple, Dict, Any, Optional


def create_struct_with_variables(
    package_path: str,
    struct_name: str,
    variables: List[Tuple[str, str]],
    remove_default_variable: bool = True,
    save_after_creation: bool = True
) -> Dict[str, Any]:
    """
    Create a UserDefinedStruct with the specified variables.

    Args:
        package_path: Content folder path (e.g., '/Game/Data')
        struct_name: Name for the struct (e.g., 'F_MyStruct')
        variables: List of (name, type) tuples defining the struct members
        remove_default_variable: Remove the auto-created default variable
        save_after_creation: Save the struct asset after creation

    Returns:
        {success, data: {struct_path, struct, variables_added}, error}
    """
    if not package_path:
        return _make_error("Package path cannot be empty")

    if not struct_name:
        return _make_error("Struct name cannot be empty")

    if not variables:
        return _make_error("Variables list cannot be empty")

    try:
        # Create the struct
        # UE Python returns: (return_value, out_param1, out_param2)
        # So order is: (struct, bSuccess, errorMessage)
        struct, success, error = unreal.N2CStructUtilsLibrary.create_user_defined_struct(
            package_path, struct_name)

        if not success or not struct:
            return _make_error(f"Failed to create struct: {error}")

        # Add all the specified variables
        added_vars = []
        failed_vars = []

        for var_name, var_type in variables:
            var_success, var_error = unreal.N2CStructUtilsLibrary.add_struct_variable(
                struct, var_name, var_type)

            if var_success:
                added_vars.append({'name': var_name, 'type': var_type})
            else:
                failed_vars.append({'name': var_name, 'type': var_type, 'error': var_error})

        # Remove the default variable if requested. Only after the others are added: a struct cannot be empty, so
        # the default member cannot be removed while it is the only one.
        if remove_default_variable and added_vars:
            rm_success, rm_error = unreal.N2CStructUtilsLibrary.remove_default_variable(struct)
            if not rm_success:
                # Log warning but continue
                unreal.log_warning(f"Could not remove default variable: {rm_error}")

        # Save the struct
        if save_after_creation:
            unreal.N2CStructUtilsLibrary.save_struct(struct)

        struct_path = f"{package_path}/{struct_name}"

        if failed_vars:
            return _make_success({
                'struct_path': struct_path,
                'variables_added': added_vars,
                'variables_failed': failed_vars,
                'partial_success': True
            })

        return _make_success({
            'struct_path': struct_path,
            'variables_added': added_vars,
            'variables_failed': []
        })

    except Exception as e:
        return _make_error(f"Error creating struct: {e}")


def create_datatable_row_struct(
    package_path: str,
    base_name: str,
    variables: List[Tuple[str, str]],
    create_datatable: bool = True
) -> Dict[str, Any]:
    """
    Create a DataTable row struct and optionally the DataTable itself.

    This is a convenience function that creates both the row struct (F_{base_name})
    and a DataTable (DT_{base_name}) that uses it.

    Args:
        package_path: Content folder path (e.g., '/Game/Data')
        base_name: Base name for assets (struct will be F_{name}, DataTable will be DT_{name})
        variables: List of (name, type) tuples defining the struct members
        create_datatable: Whether to also create a DataTable using the struct

    Returns:
        {success, data: {struct_path, datatable_path, struct, datatable}, error}
    """
    if not package_path:
        return _make_error("Package path cannot be empty")

    if not base_name:
        return _make_error("Base name cannot be empty")

    struct_name = f"F_{base_name}"
    datatable_name = f"DT_{base_name}"

    try:
        # Create the struct
        struct_result = create_struct_with_variables(
            package_path, struct_name, variables,
            remove_default_variable=True,
            save_after_creation=True
        )

        if not struct_result['success']:
            return struct_result

        struct_path = struct_result['data']['struct_path']

        # Load the struct we just created
        struct = unreal.N2CStructUtilsLibrary.load_user_defined_struct(struct_path)
        if not struct:
            return _make_error(f"Failed to load created struct at: {struct_path}")

        result_data = {
            'struct_path': struct_path,
            'variables_added': struct_result['data']['variables_added']
        }

        # Create the DataTable if requested
        if create_datatable:
            asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
            dt_factory = unreal.DataTableFactory()
            dt_factory.struct = struct

            datatable = asset_tools.create_asset(
                datatable_name, package_path, None, dt_factory)

            if datatable:
                dt_path = f"{package_path}/{datatable_name}"
                unreal.EditorAssetLibrary.save_asset(dt_path)

                result_data['datatable_path'] = dt_path
                result_data['datatable_created'] = True
            else:
                return _make_error("Struct created but DataTable creation failed")

        return _make_success(result_data)

    except Exception as e:
        return _make_error(f"Error creating DataTable row struct: {e}")


def add_variables_to_struct(
    struct_path: str,
    variables: List[Tuple[str, str]],
    save_after: bool = True
) -> Dict[str, Any]:
    """
    Add variables to an existing UserDefinedStruct.

    Args:
        struct_path: Full asset path to the struct (e.g., '/Game/Data/F_MyStruct')
        variables: List of (name, type) tuples to add
        save_after: Save the struct after adding variables

    Returns:
        {success, data: {struct_path, variables_added, variables_failed}, error}
    """
    if not struct_path:
        return _make_error("Struct path cannot be empty")

    if not variables:
        return _make_error("Variables list cannot be empty")

    try:
        struct = unreal.N2CStructUtilsLibrary.load_user_defined_struct(struct_path)

        if not struct:
            return _make_error(f"Struct not found at: {struct_path}")

        added_vars = []
        failed_vars = []

        for var_name, var_type in variables:
            var_success, var_error = unreal.N2CStructUtilsLibrary.add_struct_variable(
                struct, var_name, var_type)

            if var_success:
                added_vars.append({'name': var_name, 'type': var_type})
            else:
                failed_vars.append({'name': var_name, 'type': var_type, 'error': var_error})

        if save_after:
            unreal.N2CStructUtilsLibrary.save_struct(struct)

        return _make_success({
            'struct_path': struct_path,
            'variables_added': added_vars,
            'variables_failed': failed_vars
        })

    except Exception as e:
        return _make_error(f"Error adding variables: {e}")


def get_struct_info(struct_path: str) -> Dict[str, Any]:
    """
    Get information about a UserDefinedStruct.

    Args:
        struct_path: Full asset path to the struct

    Returns:
        {success, data: {struct_path, variable_names, variable_count}, error}
    """
    if not struct_path:
        return _make_error("Struct path cannot be empty")

    try:
        if not unreal.N2CStructUtilsLibrary.struct_exists(struct_path):
            return _make_error(f"Struct not found at: {struct_path}")

        struct = unreal.N2CStructUtilsLibrary.load_user_defined_struct(struct_path)

        if not struct:
            return _make_error(f"Failed to load struct: {struct_path}")

        var_names = unreal.N2CStructUtilsLibrary.get_struct_variable_names(struct)

        return _make_success({
            'struct_path': struct_path,
            'variable_names': list(var_names),
            'variable_count': len(var_names)
        })

    except Exception as e:
        return _make_error(f"Error getting struct info: {e}")


# ============================================================================
# Private Helper Functions
# ============================================================================

def _make_success(data: Any) -> Dict[str, Any]:
    """Create a success result."""
    return {
        "success": True,
        "data": data,
        "error": None
    }


def _make_error(message: str) -> Dict[str, Any]:
    """Create an error result."""
    return {
        "success": False,
        "data": None,
        "error": message
    }
