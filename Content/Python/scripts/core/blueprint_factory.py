"""
Blueprint creation utilities for Unreal Engine.

Create Blueprints from various parent classes with proper initialization.
Supports all common Blueprint types with convenience functions.

Usage:
    from scripts.core.blueprint_factory import create_blueprint, create_actor_blueprint

    # Create an Actor Blueprint
    bp = create_blueprint('MyActor', '/Game/Blueprints', 'Actor')

    # Create a Widget Blueprint (convenience function)
    widget = create_widget_blueprint('MyWidget', '/Game/UI')

    # Create with full class path
    custom = create_blueprint('MyClass', '/Game/BP', '/Script/MyModule.MyClass')

Functions return standardized dicts: {success: bool, data: {...}, error: str|None}
"""

import unreal
from typing import Dict, Any, Optional


def create_blueprint(
    name: str,
    package_path: str,
    parent_class: str,
    open_editor: bool = True,
    compile_on_create: bool = True,
    save_on_create: bool = True
) -> Dict[str, Any]:
    """
    Create a new Blueprint asset.

    Args:
        name: Blueprint name, used as given (no prefix is added here; create_actor_blueprint and the
            other create_*_blueprint helpers add their prefix, such as BP_ or WBP_, when it is missing)
        package_path: Content folder path (e.g., '/Game/Blueprints')
        parent_class: Parent class - can be:
            - Simple name: 'Actor', 'Pawn', 'Character', 'UserWidget'
            - Full path: '/Script/Engine.Actor'
        open_editor: Open the Blueprint editor after creation
        compile_on_create: Compile immediately after creation
        save_on_create: Save the Blueprint after creation

    Returns:
        {success, data: {blueprint_path, blueprint_name, parent_class, ...}, error}
    """
    if not name:
        return _make_error("Blueprint name cannot be empty")

    if not package_path:
        return _make_error("Package path cannot be empty")

    if not parent_class:
        return _make_error("Parent class cannot be empty")

    try:
        # Ensure package path exists
        if not unreal.EditorAssetLibrary.does_directory_exist(package_path):
            unreal.EditorAssetLibrary.make_directory(package_path)

        # Clean up name
        clean_name = name.strip()

        # Resolve parent class
        parent_class_obj = _resolve_parent_class(parent_class)
        if parent_class_obj is None:
            return _make_error(f"Could not find parent class: {parent_class}")

        # Build full asset path
        asset_path = f"{package_path}/{clean_name}"

        # Check if already exists
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            return _make_error(f"Asset already exists at: {asset_path}")

        # Create the Blueprint
        blueprint = unreal.BlueprintEditorLibrary.create_blueprint_asset_with_parent(
            asset_path,
            parent_class_obj
        )

        if blueprint is None:
            return _make_error(f"Failed to create Blueprint at: {asset_path}")

        # Compile if requested
        if compile_on_create:
            unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)

        # Save if requested
        if save_on_create:
            unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)

        # Open editor if requested
        if open_editor:
            _open_blueprint_editor(blueprint)

        return _make_success({
            "blueprint_path": asset_path,
            "blueprint_name": clean_name,
            "parent_class": parent_class,
            "parent_class_path": parent_class_obj.get_path_name() if parent_class_obj else "",
            "compiled": compile_on_create,
            "saved": save_on_create,
            "editor_opened": open_editor
        })

    except Exception as e:
        return _make_error(f"Failed to create Blueprint: {e}")


def create_actor_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create an Actor Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    # Add BP_ prefix if not present
    if not name.upper().startswith('BP_'):
        name = f"BP_{name}"

    return create_blueprint(name, package_path, 'Actor', open_editor)


def create_pawn_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a Pawn Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('BP_'):
        name = f"BP_{name}"

    return create_blueprint(name, package_path, 'Pawn', open_editor)


def create_character_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a Character Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('BP_'):
        name = f"BP_{name}"

    return create_blueprint(name, package_path, 'Character', open_editor)


def create_widget_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a Widget Blueprint (UserWidget for UI).

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    # Widget Blueprints typically use WBP_ prefix
    if not name.upper().startswith('WBP_') and not name.upper().startswith('W_'):
        name = f"WBP_{name}"

    return create_blueprint(name, package_path, 'UserWidget', open_editor)


def create_game_mode_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a GameModeBase Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('GM_') and not name.upper().startswith('BP_'):
        name = f"GM_{name}"

    return create_blueprint(name, package_path, 'GameModeBase', open_editor)


def create_game_state_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a GameStateBase Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('GS_') and not name.upper().startswith('BP_'):
        name = f"GS_{name}"

    return create_blueprint(name, package_path, 'GameStateBase', open_editor)


def create_player_controller_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a PlayerController Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('PC_') and not name.upper().startswith('BP_'):
        name = f"PC_{name}"

    return create_blueprint(name, package_path, 'PlayerController', open_editor)


def create_actor_component_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create an ActorComponent Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('AC_') and not name.upper().startswith('BP_'):
        name = f"AC_{name}"

    return create_blueprint(name, package_path, 'ActorComponent', open_editor)


def create_scene_component_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a SceneComponent Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('SC_') and not name.upper().startswith('BP_'):
        name = f"SC_{name}"

    return create_blueprint(name, package_path, 'SceneComponent', open_editor)


def create_hud_blueprint(
    name: str,
    package_path: str,
    open_editor: bool = True
) -> Dict[str, Any]:
    """
    Create a HUD Blueprint.

    Args:
        name: Blueprint name
        package_path: Content folder path
        open_editor: Open editor after creation

    Returns:
        Result dict from create_blueprint
    """
    if not name.upper().startswith('HUD_') and not name.upper().startswith('BP_'):
        name = f"HUD_{name}"

    return create_blueprint(name, package_path, 'HUD', open_editor)


def get_common_parent_classes() -> Dict[str, str]:
    """
    Get commonly used parent classes for Blueprints.

    Returns:
        Dict mapping simple names to full class paths
    """
    return {
        # Core gameplay
        'Actor': '/Script/Engine.Actor',
        'Pawn': '/Script/Engine.Pawn',
        'Character': '/Script/Engine.Character',

        # Controllers
        'PlayerController': '/Script/Engine.PlayerController',
        'AIController': '/Script/AIModule.AIController',

        # Game framework
        'GameModeBase': '/Script/Engine.GameModeBase',
        'GameMode': '/Script/Engine.GameMode',
        'GameStateBase': '/Script/Engine.GameStateBase',
        'PlayerState': '/Script/Engine.PlayerState',

        # Components
        'ActorComponent': '/Script/Engine.ActorComponent',
        'SceneComponent': '/Script/Engine.SceneComponent',

        # UI
        'UserWidget': '/Script/UMG.UserWidget',
        'HUD': '/Script/Engine.HUD',

        # Data
        'DataAsset': '/Script/Engine.DataAsset',
        'PrimaryDataAsset': '/Script/Engine.PrimaryDataAsset',

        # Animation
        'AnimInstance': '/Script/Engine.AnimInstance',

        # Subsystems
        'GameInstanceSubsystem': '/Script/Engine.GameInstanceSubsystem',
        'WorldSubsystem': '/Script/Engine.WorldSubsystem',
        'LocalPlayerSubsystem': '/Script/Engine.LocalPlayerSubsystem',
    }


def blueprint_exists(asset_path: str) -> bool:
    """
    Check if a Blueprint exists at the given path.

    Args:
        asset_path: Full asset path

    Returns:
        True if Blueprint exists
    """
    return unreal.EditorAssetLibrary.does_asset_exist(asset_path)


def load_blueprint(asset_path: str) -> Dict[str, Any]:
    """
    Load a Blueprint asset.

    Args:
        asset_path: Full asset path

    Returns:
        {success, data: {blueprint, path, class_name}, error}
    """
    if not asset_path:
        return _make_error("Asset path cannot be empty")

    try:
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            return _make_error(f"Blueprint does not exist: {asset_path}")

        blueprint = unreal.EditorAssetLibrary.load_asset(asset_path)

        if blueprint is None:
            return _make_error(f"Failed to load Blueprint: {asset_path}")

        return _make_success({
            "blueprint": blueprint,
            "path": asset_path,
            "class_name": blueprint.get_class().get_name() if blueprint else "Unknown"
        })

    except Exception as e:
        return _make_error(f"Error loading Blueprint: {e}")


# ============================================================================
# Private Helper Functions
# ============================================================================

def _resolve_parent_class(class_identifier: str) -> Optional[unreal.Class]:
    """
    Resolve a class identifier to a UClass object.

    Args:
        class_identifier: Simple name or full path

    Returns:
        UClass object or None if not found
    """
    # If it's already a full path, try to load it
    if class_identifier.startswith('/'):
        try:
            return unreal.EditorAssetLibrary.load_blueprint_class(class_identifier)
        except:
            # Try loading as a regular class
            try:
                obj = unreal.load_object(None, class_identifier)
                if isinstance(obj, unreal.Class):
                    return obj
            except:
                pass
        return None

    # Check common class mappings
    common_classes = get_common_parent_classes()
    class_key = class_identifier.strip()

    # Try exact match first
    if class_key in common_classes:
        class_path = common_classes[class_key]
        try:
            return unreal.load_object(None, class_path)
        except:
            pass

    # Try case-insensitive match
    for name, path in common_classes.items():
        if name.lower() == class_key.lower():
            try:
                return unreal.load_object(None, path)
            except:
                pass

    # Try loading as a Blueprint class
    try:
        # Assume it might be a Blueprint path
        bp_class = unreal.EditorAssetLibrary.load_blueprint_class(class_identifier)
        if bp_class:
            return bp_class
    except:
        pass

    return None


def _open_blueprint_editor(blueprint: unreal.Blueprint) -> bool:
    """Open the Blueprint editor for the given Blueprint."""
    try:
        asset_editor = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
        if asset_editor:
            return asset_editor.open_editor_for_assets([blueprint])
        return False
    except:
        return False


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
