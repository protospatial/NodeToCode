"""
Python wrappers for NodeToCode-specific C++ bridge functions.

These expose unique NodeToCode features not available in the standard UE Python API:
- Custom Blueprint graph tagging system
- LLM provider information and discovery

Usage:
    import nodetocode as n2c

    # Tag the focused graph
    result = n2c.tag_graph("gameplay", category="Systems", description="Core gameplay logic")

    # List all tags
    tags = n2c.list_tags()

    # Get LLM provider info
    provider = n2c.get_active_provider()
"""

from typing import Dict, Any, Optional

import unreal

from .utils import make_success_result, make_error_result, log_info, log_warning, log_error, _parse_bridge_result


# ============== Tagging System ==============

def tag_graph(tag: str, category: str = "Default", description: str = "") -> Dict[str, Any]:
    """
    Tag the currently focused Blueprint graph.

    This uses NodeToCode's custom tagging system to organize and track Blueprint graphs.
    Tags persist across editor sessions and can be used for batch operations.

    Args:
        tag: Tag name to apply (required)
        category: Category for organization (default "Default")
        description: Optional description for the tag

    Returns:
        {success, data: {tag, category, description, graph_guid, graph_name, blueprint_name}, error}

    Example:
        result = tag_graph("player_controller", "Systems", "Main player input handling")
        if result['success']:
            print(f"Tagged graph: {result['data']['graph_name']}")
    """
    if not tag or not tag.strip():
        return make_error_result("Tag name cannot be empty")

    try:
        result = unreal.N2CPythonBridge.tag_focused_graph(tag.strip(), category.strip(), description.strip())
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"tag_graph failed: {e}")
        return make_error_result(str(e))


def list_tags(category: Optional[str] = None, tag: Optional[str] = None) -> Dict[str, Any]:
    """
    List tags with optional filters.

    Args:
        category: Filter by category (None for all)
        tag: Filter by tag name (None for all)

    Returns:
        {success, data: {tags: [...], count, total_categories, total_unique_tags}, error}

    Each tag entry contains:
        - tag: Tag name
        - category: Tag category
        - description: Optional description
        - graph_guid: GUID of the tagged graph
        - graph_name: Name of the graph
        - blueprint_path: Path to the owning Blueprint
        - timestamp: When the tag was applied

    Example:
        # List all tags
        all_tags = list_tags()

        # List tags in a category
        system_tags = list_tags(category="Systems")

        # Find graphs with a specific tag
        gameplay_graphs = list_tags(tag="gameplay")
    """
    try:
        result = unreal.N2CPythonBridge.list_tags(category or "", tag or "")
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"list_tags failed: {e}")
        return make_error_result(str(e))


def remove_tag(graph_guid: str, tag: str) -> Dict[str, Any]:
    """
    Remove a tag from a graph.

    Args:
        graph_guid: The graph's GUID (string format, from list_tags)
        tag: Tag name to remove

    Returns:
        {success, data: {removed: bool, removed_count, tag, graph_guid, remaining_tags}, error}

    Example:
        # Get tags first to find GUIDs
        tags = list_tags()
        for t in tags['data']['tags']:
            if t['tag'] == 'old_tag':
                remove_tag(t['graph_guid'], 'old_tag')
    """
    if not graph_guid or not graph_guid.strip():
        return make_error_result("graph_guid cannot be empty")

    if not tag or not tag.strip():
        return make_error_result("tag cannot be empty")

    try:
        result = unreal.N2CPythonBridge.remove_tag(graph_guid.strip(), tag.strip())
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"remove_tag failed: {e}")
        return make_error_result(str(e))


# ============== LLM Provider Info ==============

def get_llm_providers() -> Dict[str, Any]:
    """
    Get all available LLM providers and their configuration.

    This returns information about all LLM providers supported by NodeToCode,
    including which one is currently active.

    Returns:
        {success, data: {providers: [...], current_provider, provider_count}, error}

    Each provider entry contains:
        - name: Provider identifier (OpenAI, Anthropic, etc.)
        - display_name: Human-readable name
        - is_local: Whether this is a local provider (Ollama, LM Studio)
        - is_current: Whether this is the currently active provider

    Example:
        providers = get_llm_providers()
        if providers['success']:
            for p in providers['data']['providers']:
                status = " (active)" if p['is_current'] else ""
                print(f"{p['display_name']}{status}")
    """
    try:
        result = unreal.N2CPythonBridge.get_llm_providers()
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"get_llm_providers failed: {e}")
        return make_error_result(str(e))


def get_active_provider() -> Dict[str, Any]:
    """
    Get the currently active LLM provider.

    Returns detailed information about the currently configured LLM provider,
    including the model and endpoint.

    Returns:
        {success, data: {name, display_name, model, endpoint, is_local}, error}

    Example:
        provider = get_active_provider()
        if provider['success']:
            print(f"Using {provider['data']['display_name']}")
            print(f"Model: {provider['data']['model']}")
            if provider['data']['is_local']:
                print(f"Local endpoint: {provider['data']['endpoint']}")
    """
    try:
        result = unreal.N2CPythonBridge.get_active_provider()
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"get_active_provider failed: {e}")
        return make_error_result(str(e))


# ============== Blueprint Editor Navigation ==============

def open_blueprint(blueprint_path: str, focus_graph: str = "EventGraph") -> Dict[str, Any]:
    """
    Open a Blueprint asset in the editor and focus on a specific graph.

    IMPORTANT: A graph must be focused for other functions like get_focused_blueprint(),
    add_node_to_graph(), etc. to work. Defaults to "EventGraph" if not specified.

    Args:
        blueprint_path: Asset path of the Blueprint (e.g., "/Game/Blueprints/BP_MyActor")
        focus_graph: Graph name to focus. Defaults to "EventGraph". Can also be
                     "ConstructionScript" or any function name in the Blueprint.

    Returns:
        {success, data: {blueprintName, blueprintPath, focusedGraph, eventGraphCount, functionCount}, error}

    Example:
        # Open a Blueprint (focuses EventGraph by default)
        result = open_blueprint("/Game/Blueprints/BP_Player")

        # Open and focus on a specific function
        result = open_blueprint("/Game/Blueprints/BP_Player", "HandleInput")
    """
    if not blueprint_path or not blueprint_path.strip():
        return make_error_result("blueprint_path cannot be empty")

    try:
        result = unreal.N2CPythonBridge.open_blueprint(
            blueprint_path.strip(),
            (focus_graph or "EventGraph").strip()
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"open_blueprint failed: {e}")
        return make_error_result(str(e))


def open_blueprint_function(function_name: str) -> Dict[str, Any]:
    """
    Open/focus a function graph in the currently open Blueprint.

    The Blueprint must already be open in the editor. Use open_blueprint() first
    if needed.

    Args:
        function_name: Name of the function to open

    Returns:
        {success, data: {functionName, blueprintName, blueprintPath, graphGuid}, error}

    Example:
        # First open the Blueprint
        open_blueprint("/Game/Blueprints/BP_Player")

        # Then focus on a specific function
        result = open_blueprint_function("HandleDamage")
        if result['success']:
            print(f"Opened function: {result['data']['functionName']}")
    """
    if not function_name or not function_name.strip():
        return make_error_result("function_name cannot be empty")

    try:
        result = unreal.N2CPythonBridge.open_blueprint_function(function_name.strip())
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"open_blueprint_function failed: {e}")
        return make_error_result(str(e))
