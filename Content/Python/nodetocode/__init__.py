"""
NodeToCode Python Module for Unreal Engine

Provides Blueprint manipulation utilities accessible from the run-python MCP tool.
NodeToCode 2.0 has no script library (decision 0010): keep reusable scripts as Agent
Skills (a SKILL.md with the script under its scripts/). All functions return standardized
dictionaries with 'success', 'data', and 'error' keys.

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
    if result['success'] and 'sourceControl' in result['data']:   # set when the file was written
        print("Saved to disk")

Graph Editing:
    # Search for and add nodes
    results = n2c.search_blueprint_nodes("Print String")
    node = results['data']['nodes'][0]
    added = n2c.add_node_to_graph(node['spawnMetadata']['nodeName'], node['spawnMetadata']['actionIdentifier'])

    # Connect nodes: pins are picked by their stable pinName ('execute', 'then', 'ReturnValue', ...)
    begin = n2c.find_nodes_in_graph(["BeginPlay"])['data']['nodes'][0]   # N2C JSON nodes
    n2c.connect_pins([{
        "from": {"nodeGuid": begin['ids']['guid'],
                 "pinGuid": next(p['ids']['guid'] for p in begin['output_pins'] if p['ids']['name'] == 'then')},
        "to": {"nodeGuid": added['data']['nodeGuid'],
               "pinGuid": next(p['pinGuid'] for p in added['data']['inputPins'] if p['pinName'] == 'execute')},
    }])

    # Find and delete nodes
    found = n2c.find_nodes_in_graph(["PrintString"])
    n2c.delete_nodes([found['data']['nodes'][0]['ids']['guid']])

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
    - get_blueprint_n2cjson(path, graph_name, graph_guid, depth, to_file) - Any Blueprint as N2C JSON by path
    - list_blueprint_graphs(path) - Every graph of a Blueprint by path
    - get_blueprint_dependencies(paths) - Dependency index: nodes, edges, order, cycle groups
    - read_n2c_json(text) - Read N2C JSON (1.x or 2.x) into the current version
    - compile_blueprint(path=None) - Compile the Blueprint at path, else the focused Blueprint
    - save_blueprint(path=None, only_if_dirty=True) - Save the Blueprint at path, else the focused Blueprint
    - load_blueprint(path) - Load a Blueprint by path
    - open_blueprint(path, focus_graph) - Open Blueprint in editor
    - open_blueprint_function(name) - Focus a function in the open Blueprint

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
    - list_tags(category, tag, blueprint_path) - List all tags with optional filters
    - remove_tag(graph_guid, tag, blueprint_path) - Remove a tag from a graph (its Blueprint and GUID)
    - get_llm_providers() - Get all available LLM providers
    - get_active_provider() - Get current LLM provider info
    - get_translation_targets() - Get the languages NodeToCode translates into

    Utilities:
    - get_editor_subsystem(class) - Get an editor subsystem
    - is_blueprint_valid(obj) - Check if object is a Blueprint
    - get_project_content_dir(), get_project_dir() - Project paths
    - log_info/warning/error(msg) - Log messages
    - make_success_result(data), make_error_result(error) - Build the standard result dict
    - invoke_tool(name, arguments) - Run any NodeToCode MCP tool by name (not run-python)

    The graph, function-pin, compile, save, tag_graph and open functions run the matching MCP tool through
    invoke_tool (card 0342): their data is the tool's result, plus the 1.x keys for one release (reading one logs a
    deprecation warning naming the tool's key). utils.TOOL_NAMES lists the pairs, utils.BRIDGE_ONLY the rest.
"""

__version__ = "1.0.0"
__author__ = "Protospatial"

# Blueprint operations
from .blueprint import (
    get_focused_blueprint,
    get_blueprint_n2cjson,
    list_blueprint_graphs,
    get_blueprint_dependencies,
    read_n2c_json,
    compile_blueprint,
    save_blueprint,
    load_blueprint,
)

# NodeToCode-specific features (tagging, LLM info, navigation)
from .bridge import (
    tag_graph,
    list_tags,
    remove_tag,
    get_llm_providers,
    get_active_provider,
    get_translation_targets,
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
    invoke_tool,
)

__all__ = [
    # Version info
    '__version__',
    '__author__',
    # Blueprint operations
    'get_focused_blueprint',
    'get_blueprint_n2cjson',
    'list_blueprint_graphs',
    'get_blueprint_dependencies',
    'read_n2c_json',
    'compile_blueprint',
    'save_blueprint',
    'load_blueprint',
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
    'get_translation_targets',
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
    'invoke_tool',
]
