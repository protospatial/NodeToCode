"""
NodeToCode Python Module for Unreal Engine

Provides Blueprint manipulation and script management utilities accessible from
the run-python MCP tool. All functions return standardized dictionaries with
'success', 'data', and 'error' keys.

Quick Start:
    import nodetocode as n2c

    # Get the currently focused blueprint
    bp = n2c.get_focused_blueprint()
    if bp['success']:
        print(f"Blueprint: {bp['data']['name']}")
        print(f"Graph: {bp['data']['graph_name']} ({bp['data']['node_count']} nodes)")

    # Compile the blueprint
    result = n2c.compile_blueprint()
    if result['success']:
        print(f"Compile status: {result['data']['status']}")

    # Save the blueprint
    result = n2c.save_blueprint()
    if result['success'] and result['data']['was_saved']:
        print("Saved to disk")

Script Management:
    # Search for existing scripts
    matches = n2c.search_scripts("health system")

    # Run a saved script
    result = n2c.run_script("create_health_system", max_hp=100)

    # Save a new script for reuse
    n2c.save_script("my_script", code, "Description", tags=["gameplay"])

Graph Editing:
    # Search for and add nodes
    results = n2c.search_blueprint_nodes("Print String")
    node = results['data']['nodes'][0]
    added = n2c.add_node_to_graph(node['displayName'], node['spawnMetadata']['actionIdentifier'])

    # Connect nodes: pins are picked by their stable pinName ('execute', 'then', 'ReturnValue', ...)
    begin = n2c.find_nodes_in_graph(["BeginPlay"])['data']['nodes'][0]
    n2c.connect_pins([{
        "from": {"nodeGuid": begin['nodeGuid'],
                 "pinGuid": next(p['pinGuid'] for p in begin['outputPins'] if p['pinName'] == 'then')},
        "to": {"nodeGuid": added['data']['nodeGuid'],
               "pinGuid": next(p['pinGuid'] for p in added['data']['inputPins'] if p['pinName'] == 'execute')},
    }])

    # Find and delete nodes
    found = n2c.find_nodes_in_graph(["PrintString"])
    n2c.delete_nodes([found['data']['nodes'][0]['nodeGuid']])

Function Pin Management (with a function graph focused, for example after open_blueprint_function):
    # Add function parameters
    n2c.add_function_input_pin("Target", "/Script/Engine.Actor")
    n2c.add_function_return_pin("bSuccess", "bool")

Return Format:
    All functions return a dictionary with:
    - 'success': bool - True if the operation succeeded
    - 'data': dict or None - Operation-specific result data
    - 'error': str or None - Error message if success is False

Available Functions (the authoritative list is __all__ below):
    Blueprint Operations:
    - get_focused_blueprint() - Get info about the focused Blueprint
    - read_n2c_json(text) - Read N2C JSON (1.x or 2.x) into the current version
    - compile_blueprint(path=None) - Compile the focused Blueprint (path is ignored for now)
    - save_blueprint(path=None, only_if_dirty=True) - Save the focused Blueprint (path is ignored for now)
    - load_blueprint(path) - Load a Blueprint by path
    - open_blueprint(path, focus_graph) - Open Blueprint in editor
    - open_blueprint_function(name) - Focus a function in the open Blueprint

    Script Management:
    - list_scripts(category, limit) - List available scripts
    - search_scripts(query, limit) - Search scripts by name/description/tags
    - get_script(name) - Load script code and metadata
    - get_script_functions(name) - Get function signatures (token-efficient)
    - run_script(name, **kwargs) - Execute a saved script
    - save_script(name, code, description, tags, category) - Save new script
    - delete_script(name) - Remove a script
    - get_script_stats() - Get script library statistics

    Graph Editing:
    - search_blueprint_nodes(term, context_sensitive, max) - Search for nodes to add
    - add_node_to_graph(name, action_id, x, y) - Add a node to focused graph
    - connect_pins(connections, break_existing_links) - Connect pins between nodes
    - set_pin_value(node_guid, pin_guid, value) - Set input pin default value
    - delete_nodes(guids, preserve_connections, force) - Delete nodes
    - find_nodes_in_graph(terms, type, case_sensitive, max) - Find nodes by keyword/GUID
    - create_comment_node(guids, text, color, font_size) - Create comment around nodes

    Function Pin Management:
    - add_function_input_pin(name, type, default, by_ref, tooltip) - Add input param
    - add_function_return_pin(name, type, tooltip) - Add return value
    - remove_function_entry_pin(name) - Remove input parameter
    - remove_function_return_pin(name) - Remove return value

    NodeToCode-Specific (Tagging & LLM):
    - tag_graph(tag, category, description) - Tag the focused graph
    - list_tags(category, tag) - List all tags with optional filters
    - remove_tag(graph_guid, tag) - Remove a tag from a graph
    - get_llm_providers() - Get all available LLM providers
    - get_active_provider() - Get current LLM provider info

    Utilities:
    - get_editor_subsystem(class) - Get an editor subsystem
    - is_blueprint_valid(obj) - Check if object is a Blueprint
    - get_project_content_dir(), get_project_dir() - Project paths
    - log_info/warning/error(msg) - Log messages
    - make_success_result(data), make_error_result(error) - Build the standard result dict
"""

__version__ = "1.0.0"
__author__ = "Protospatial"

# Blueprint operations
from .blueprint import (
    get_focused_blueprint,
    read_n2c_json,
    compile_blueprint,
    save_blueprint,
    load_blueprint,
)

# Script management
from .scripts import (
    list_scripts,
    search_scripts,
    get_script,
    get_script_functions,
    run_script,
    save_script,
    delete_script,
    get_script_stats,
)

# NodeToCode-specific features (tagging, LLM info, navigation)
from .bridge import (
    tag_graph,
    list_tags,
    remove_tag,
    get_llm_providers,
    get_active_provider,
    open_blueprint,
    open_blueprint_function,
)

# Graph editing operations
from .graph import (
    search_blueprint_nodes,
    add_node_to_graph,
    connect_pins,
    set_pin_value,
    delete_nodes,
    find_nodes_in_graph,
    create_comment_node,
)

# Function pin management
from .functions import (
    add_function_input_pin,
    add_function_return_pin,
    remove_function_entry_pin,
    remove_function_return_pin,
)

# Utility functions
from .utils import (
    get_editor_subsystem,
    is_blueprint_valid,
    get_project_content_dir,
    get_project_dir,
    log_info,
    log_warning,
    log_error,
    make_success_result,
    make_error_result,
)

__all__ = [
    # Version info
    '__version__',
    '__author__',
    # Blueprint operations
    'get_focused_blueprint',
    'read_n2c_json',
    'compile_blueprint',
    'save_blueprint',
    'load_blueprint',
    # Script management
    'list_scripts',
    'search_scripts',
    'get_script',
    'get_script_functions',
    'run_script',
    'save_script',
    'delete_script',
    'get_script_stats',
    # Graph editing
    'search_blueprint_nodes',
    'add_node_to_graph',
    'connect_pins',
    'set_pin_value',
    'delete_nodes',
    'find_nodes_in_graph',
    'create_comment_node',
    # Function pin management
    'add_function_input_pin',
    'add_function_return_pin',
    'remove_function_entry_pin',
    'remove_function_return_pin',
    # NodeToCode-specific (tagging, LLM info, navigation)
    'tag_graph',
    'list_tags',
    'remove_tag',
    'get_llm_providers',
    'get_active_provider',
    'open_blueprint',
    'open_blueprint_function',
    # Utilities
    'get_editor_subsystem',
    'is_blueprint_valid',
    'get_project_content_dir',
    'get_project_dir',
    'log_info',
    'log_warning',
    'log_error',
    'make_success_result',
    'make_error_result',
]
