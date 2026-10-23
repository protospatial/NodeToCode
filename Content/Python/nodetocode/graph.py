"""
Python wrappers for Blueprint graph editing operations.

These functions enable programmatic Blueprint graph manipulation from Python:
- Search for and add Blueprint nodes
- Connect pins between nodes
- Delete nodes from graphs
- Set input pin default values
- Find/enumerate nodes in a graph
- Create comment nodes for organization

WORKFLOW - The typical pattern for building Blueprint logic:
1. search_blueprint_nodes() - Find available nodes matching your query
2. add_node_to_graph() - Add a node, returns nodeGuid and pinGuid values
3. connect_pins() - Wire nodes together using the GUIDs from step 2

Usage:
    import nodetocode as n2c

    # Step 1: Search for nodes
    results = n2c.search_blueprint_nodes("Print String")

    # Step 2: Add a node to the focused graph
    node = n2c.add_node_to_graph(
        results['data']['nodes'][0]['displayName'],
        results['data']['nodes'][0]['spawnMetadata']['actionIdentifier']
    )

    # Step 3: Connect pins between nodes (IMPORTANT: This is the next step after add_node_to_graph)
    # Use the nodeGuid and pinGuid values returned from add_node_to_graph
    n2c.connect_pins([{
        "from": {"nodeGuid": node['data']['nodeGuid'], "pinGuid": "..."},
        "to": {"nodeGuid": "...", "pinGuid": "..."}
    }])

    # Find existing nodes in the graph (use when you need GUIDs for existing nodes)
    found = n2c.find_nodes_in_graph(["Print"])
"""

import json
from typing import Dict, Any, List, Optional, Union

import unreal

from .utils import make_success_result, make_error_result, log_info, log_warning, log_error


def _parse_bridge_result(json_str: str) -> Dict[str, Any]:
    """
    Parse JSON result from C++ bridge functions.

    Args:
        json_str: JSON string from bridge function

    Returns:
        Parsed dictionary with success/data/error structure
    """
    if not json_str:
        return make_error_result("Empty response from bridge function")

    try:
        return json.loads(json_str)
    except json.JSONDecodeError as e:
        log_error(f"Failed to parse bridge response: {e}")
        return make_error_result(f"Failed to parse bridge response: {e}")


# ============== Node Search & Creation ==============

def search_blueprint_nodes(
    search_term: str,
    context_sensitive: bool = True,
    max_results: int = 20,
    category: str = "",
    exclude_vm_functions: bool = True
) -> Dict[str, Any]:
    """
    Search for Blueprint nodes/actions matching a search term.

    This searches through available Blueprint actions that can be added to graphs.
    Use the returned actionIdentifier with add_node_to_graph() to spawn nodes.

    Args:
        search_term: Text query to search for (e.g., "Print String", "Branch")
        context_sensitive: If True, filters results based on focused Blueprint context
        max_results: Maximum number of results to return (1-100)
        category: Optional category filter. Common categories:
            - "flowcontrol" - Branch, Sequence, ForLoop, etc.
            - "operators" - Math operators (+, -, *, /)
            - "struct" - Make/Break struct nodes
            - "casting" - Cast nodes
            - "math" - Math functions
        exclude_vm_functions: If True (default), excludes low-level VM math functions
            like Multiply_FloatFloat that are rarely used directly

    Returns:
        {
            success: bool,
            data: {
                nodes: [
                    {
                        name: str,
                        displayName: str,
                        spawnMetadata: {
                            actionIdentifier: str,
                            isContextSensitive: bool
                        }
                    },
                    ...
                ],
                count: int
            },
            error: str or None
        }

    Example:
        # Search for Branch in flow control category
        results = search_blueprint_nodes("Branch", category="flowcontrol")

        # Search for all print nodes
        results = search_blueprint_nodes("Print String")
        if results['success'] and results['data']['count'] > 0:
            node = results['data']['nodes'][0]
            print(f"Found: {node['displayName']}")
            print(f"ActionID: {node['spawnMetadata']['actionIdentifier']}")
    """
    if not search_term or not search_term.strip():
        return make_error_result("search_term cannot be empty")

    try:
        result = unreal.N2CPythonBridge.search_blueprint_nodes(
            search_term.strip(),
            context_sensitive,
            max_results,
            category,
            exclude_vm_functions
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"search_blueprint_nodes failed: {e}")
        return make_error_result(str(e))


def add_node_to_graph(
    node_name: str,
    action_identifier: str,
    location_x: float = 0.0,
    location_y: float = 0.0
) -> Dict[str, Any]:
    """
    Add a Blueprint node to the currently focused graph.

    Use search_blueprint_nodes() first to get the actionIdentifier for the node.
    The response includes full pin information, allowing immediate connection
    without needing to call find_nodes_in_graph().

    NEXT STEP: After adding a node, use connect_pins() to wire the node's pins
    to other nodes using the nodeGuid and pinGuid values returned in this response.

    Args:
        node_name: Name of the node (used for action lookup)
        action_identifier: Identifier from search results spawnMetadata
        location_x: X position for node placement
        location_y: Y position for node placement

    Returns:
        {
            success: bool,
            data: {
                nodeGuid: str,
                nodeName: str,
                graphName: str,
                blueprintName: str,
                location: {x: float, y: float},
                inputPins: [
                    {pinGuid: str, pinName: str, displayName: str, type: str, defaultValue?: str},
                    ...
                ],
                outputPins: [
                    {pinGuid: str, pinName: str, displayName: str, type: str},
                    ...
                ],
                nextStep: {
                    function: "connect_pins",
                    description: str  # Guidance on using connect_pins()
                }
            },
            error: str or None
        }

    Example:
        # First search for the node
        results = search_blueprint_nodes("Print String")
        node_info = results['data']['nodes'][0]

        # Then add it
        added = add_node_to_graph(
            node_info['displayName'],
            node_info['spawnMetadata']['actionIdentifier'],
            location_x=400,
            location_y=200
        )
        if added['success']:
            print(f"Added node with GUID: {added['data']['nodeGuid']}")
            # Pin GUIDs are immediately available for connecting
            exec_pin = next(p for p in added['data']['outputPins'] if p['type'] == 'exec')
            print(f"Exec output pin: {exec_pin['pinGuid']}")

            # NEXT STEP: Connect this node to others using connect_pins()
            # connect_pins([{
            #     "from": {"nodeGuid": added['data']['nodeGuid'], "pinGuid": exec_pin['pinGuid']},
            #     "to": {"nodeGuid": "other_node_guid", "pinGuid": "other_pin_guid"}
            # }])
    """
    if not node_name or not node_name.strip():
        return make_error_result("node_name cannot be empty")

    if not action_identifier or not action_identifier.strip():
        return make_error_result("action_identifier cannot be empty")

    try:
        result = unreal.N2CPythonBridge.add_node_to_graph(
            node_name.strip(),
            action_identifier.strip(),
            location_x,
            location_y
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"add_node_to_graph failed: {e}")
        return make_error_result(str(e))


# ============== Pin Operations ==============

def connect_pins(
    connections: List[Dict[str, Any]],
    break_existing_links: bool = False
) -> Dict[str, Any]:
    """
    Connect pins between Blueprint nodes.

    Each connection specifies a source pin and target pin using node/pin GUIDs.

    Args:
        connections: List of connection specifications, each with:
            {
                "from": {
                    "nodeGuid": str,   # Source node GUID
                    "pinGuid": str,    # Source pin GUID
                    "pinName": str     # Optional fallback name
                },
                "to": {
                    "nodeGuid": str,   # Target node GUID
                    "pinGuid": str,    # Target pin GUID
                    "pinName": str     # Optional fallback name
                }
            }
        break_existing_links: If True, breaks existing connections before making new ones

    Returns:
        {
            success: bool,
            data: {
                succeeded: [{fromNode, fromPin, toNode, toPin}, ...],
                failed: [{error: str}, ...],
                summary: {succeeded: int, failed: int}
            },
            error: str or None
        }

    Example:
        result = connect_pins([
            {
                "from": {"nodeGuid": "ABC...", "pinGuid": "DEF..."},
                "to": {"nodeGuid": "GHI...", "pinGuid": "JKL..."}
            }
        ])
        if result['success']:
            print(f"Connected {result['data']['summary']['succeeded']} pins")
    """
    if not connections:
        return make_error_result("connections list cannot be empty")

    try:
        connections_json = json.dumps(connections)
        result = unreal.N2CPythonBridge.connect_pins(
            connections_json,
            break_existing_links
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"connect_pins failed: {e}")
        return make_error_result(str(e))


def set_pin_value(
    node_guid: str,
    pin_guid: str,
    value: str,
    pin_name: str = ""
) -> Dict[str, Any]:
    """
    Set the default value of an input pin on a Blueprint node.

    Only works on input pins that are not connected and not execution pins.

    Args:
        node_guid: GUID of the node containing the pin
        pin_guid: GUID of the pin to modify
        value: The value to set (as string, will be converted to appropriate type)
        pin_name: Optional pin name for fallback lookup if GUID fails

    Returns:
        {
            success: bool,
            data: {
                nodeGuid: str,
                pinGuid: str,
                pinName: str,
                oldValue: str,
                newValue: str
            },
            error: str or None
        }

    Example:
        result = set_pin_value(
            node_guid="ABC123...",
            pin_guid="DEF456...",
            value="Hello, World!"
        )
        if result['success']:
            print(f"Changed from '{result['data']['oldValue']}' to '{result['data']['newValue']}'")
    """
    if not node_guid or not node_guid.strip():
        return make_error_result("node_guid cannot be empty")

    if not pin_guid or not pin_guid.strip():
        return make_error_result("pin_guid cannot be empty")

    try:
        result = unreal.N2CPythonBridge.set_pin_value(
            node_guid.strip(),
            pin_guid.strip(),
            value,
            pin_name.strip() if pin_name else ""
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"set_pin_value failed: {e}")
        return make_error_result(str(e))


# ============== Node Operations ==============

def delete_nodes(
    node_guids: List[str],
    preserve_connections: bool = False,
    force: bool = False
) -> Dict[str, Any]:
    """
    Delete nodes from the focused Blueprint graph.

    Args:
        node_guids: List of node GUIDs to delete
        preserve_connections: If True, attempts to bridge connections around deleted nodes
        force: If True, bypasses protection checks for entry/result nodes

    Returns:
        {
            success: bool,
            data: {
                deletedNodes: [{guid: str, name: str}, ...],
                deletedCount: int
            },
            error: str or None
        }

    Example:
        result = delete_nodes(["ABC123...", "DEF456..."])
        if result['success']:
            print(f"Deleted {result['data']['deletedCount']} nodes")
    """
    if not node_guids:
        return make_error_result("node_guids list cannot be empty")

    try:
        guids_json = json.dumps(node_guids)
        result = unreal.N2CPythonBridge.delete_nodes(
            guids_json,
            preserve_connections,
            force
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"delete_nodes failed: {e}")
        return make_error_result(str(e))


def find_nodes_in_graph(
    search_terms: List[str],
    search_type: str = "keyword",
    case_sensitive: bool = False,
    max_results: int = 50
) -> Dict[str, Any]:
    """
    Find nodes in the focused Blueprint graph by keywords or GUIDs.

    Args:
        search_terms: List of keywords or GUIDs to search for
        search_type: Either "keyword" or "guid"
        case_sensitive: Whether keyword search is case-sensitive
        max_results: Maximum nodes to return (1-200)

    Returns:
        {
            success: bool,
            data: {
                nodes: [
                    {
                        nodeGuid: str,
                        nodeName: str,
                        nodeClass: str,
                        posX: float,
                        posY: float,
                        inputPins: [{pinGuid, pinName, displayName, type}, ...],
                        outputPins: [{pinGuid, pinName, displayName, type}, ...]
                    },
                    ...
                ],
                metadata: {
                    blueprintName: str,
                    graphName: str,
                    totalFound: int,
                    totalInGraph: int
                }
            },
            error: str or None
        }

    Example:
        # Find all Print nodes
        result = find_nodes_in_graph(["Print"])

        # Find specific node by GUID
        result = find_nodes_in_graph(
            ["ABC123-DEF456-..."],
            search_type="guid"
        )
    """
    if not search_terms:
        return make_error_result("search_terms list cannot be empty")

    if search_type not in ("keyword", "guid"):
        return make_error_result("search_type must be 'keyword' or 'guid'")

    try:
        terms_json = json.dumps(search_terms)
        result = unreal.N2CPythonBridge.find_nodes_in_graph(
            terms_json,
            search_type,
            case_sensitive,
            max_results
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"find_nodes_in_graph failed: {e}")
        return make_error_result(str(e))


def create_comment_node(
    node_guids: List[str],
    comment_text: str = "Comment",
    color: Optional[Dict[str, float]] = None,
    font_size: int = 18,
    move_mode: str = "group",
    padding: float = 50.0
) -> Dict[str, Any]:
    """
    Create a comment node around specified Blueprint nodes.

    Args:
        node_guids: List of node GUIDs to encompass in the comment
        comment_text: Text for the comment
        color: RGB color dict with r, g, b values 0-1 (default: dark gray)
        font_size: Font size (1-1000)
        move_mode: "group" (nodes move with comment) or "none"
        padding: Extra padding around nodes in pixels

    Returns:
        {
            success: bool,
            data: {
                commentGuid: str,
                commentText: str,
                includedNodeCount: int,
                position: {x: float, y: float},
                size: {width: float, height: float}
            },
            error: str or None
        }

    Example:
        # Create a green comment around nodes
        result = create_comment_node(
            node_guids=["ABC123...", "DEF456..."],
            comment_text="Input Handling",
            color={"r": 0.2, "g": 0.8, "b": 0.2},
            font_size=24
        )
    """
    if not node_guids:
        return make_error_result("node_guids list cannot be empty")

    # Default color is dark gray
    r = color.get("r", 0.075) if color else 0.075
    g = color.get("g", 0.075) if color else 0.075
    b = color.get("b", 0.075) if color else 0.075

    try:
        guids_json = json.dumps(node_guids)
        result = unreal.N2CPythonBridge.create_comment_node(
            guids_json,
            comment_text,
            r, g, b,
            font_size,
            move_mode,
            padding
        )
        return _parse_bridge_result(result)
    except Exception as e:
        log_error(f"create_comment_node failed: {e}")
        return make_error_result(str(e))
