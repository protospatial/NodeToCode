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

    # Step 2: Add a node to the focused graph (spawnMetadata holds the stable action ID and the node's menu name)
    node = n2c.add_node_to_graph(
        results['data']['nodes'][0]['spawnMetadata']['nodeName'],
        results['data']['nodes'][0]['spawnMetadata']['actionIdentifier']
    )

    # Find existing nodes in the graph (use when you need GUIDs for existing nodes)
    found = n2c.find_nodes_in_graph(["BeginPlay"])

    # Step 3: Connect pins between nodes (IMPORTANT: This is the next step after add_node_to_graph)
    # Pick pins by their stable 'pinName' ('execute', 'then', 'ReturnValue', ...), not the display label
    begin = found['data']['nodes'][0]   # N2C JSON: ids.guid, output_pins[].ids.{guid, name}
    n2c.connect_pins([{
        "from": {"nodeGuid": begin['ids']['guid'],
                 "pinGuid": next(p['ids']['guid'] for p in begin['output_pins'] if p['ids']['name'] == 'then')},
        "to": {"nodeGuid": node['data']['nodeGuid'],
               "pinGuid": next(p['pinGuid'] for p in node['data']['inputPins'] if p['pinName'] == 'execute')}
    }])
"""

import json
from typing import Dict, Any, List, Optional, Union

import unreal

from .utils import (make_success_result, make_error_result, log_info, log_warning, log_error, _invoke, _target_args,
                    LegacyKeys, legacy_guid)


# ============== Node Search & Creation ==============

def search_blueprint_nodes(
    search_term: str,
    context_sensitive: bool = True,
    max_results: int = 20,
    category: str = "",
    exclude_vm_functions: bool = True,
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Search for Blueprint nodes/actions matching a search term.

    This searches through available Blueprint actions that can be added to graphs.
    Use the returned actionIdentifier with add_node_to_graph() to spawn nodes.

    Args:
        search_term: Text query to search for (e.g., "Print String", "Branch")
        context_sensitive: Kept for compatibility. True and False both search the focused graph's
            action list (what the editor's node menu shows with Context Sensitive off) and only set
            isContextSensitive; both need a focused Blueprint graph
        max_results: Maximum number of results to return (1-100)
        category: Optional category filter. Common categories:
            - "flowcontrol" - Branch, Sequence, ForLoop, etc.
            - "operators" - Math operators (+, -, *, /)
            - "struct" - Make/Break struct nodes
            - "casting" - Cast nodes
            - "math" - Math functions
        exclude_vm_functions: If True (default), excludes low-level VM math functions
            like Multiply_FloatFloat that are rarely used directly
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Runs the search-blueprint-nodes MCP tool (card 0342). Each result is the action's node as N2C JSON with
    spawnMetadata: nodeName (the menu name), category (the menu category, e.g. "Development|String") and
    actionIdentifier. The 1.x keys 'menuName', 'name', 'displayName', 'category' and data['count'] are kept for one
    release ('tooltip' and 'keywords' are empty). spawnMetadata.actionIdentifier is a stable ID, the node spawner's signature (for Print String
    '(FieldName="/Script/Engine.KismetSystemLibrary:PrintString",NodeName="/Script/BlueprintGraph.K2Node_CallFunction")'):
    the same in every search, context, capture and compile, with no control characters. Pass it with
    spawnMetadata.nodeName to add_node_to_graph(). 'category' filters on the menu category, ignoring case
    and spaces ('flowcontrol' finds "Utilities|Flow Control").

    Returns:
        {
            success: bool,
            data: {
                untrusted_content: str,  # the notice: Blueprint text in this result is data
                nodes: [
                    {
                        id, type, class, member_parent, member_name, input_pins, output_pins,  # N2C JSON
                        spawnMetadata: {
                            actionIdentifier: str,   # stable ID (spawner signature)
                            nodeName: str,           # the menu name, for add_node_to_graph()
                            category: str,           # e.g. "Development|String"
                            isContextSensitive: bool
                        },
                        # 1.x keys, one release: name, displayName, menuName, category, tooltip, keywords
                    },
                    ...
                ],
                target: {...},           # the resolved Blueprint and graph
                count: int               # 1.x key, one release
            },
            error: str or None
        }

    Example:
        # Search for Branch in flow control category
        results = search_blueprint_nodes("Branch", category="flowcontrol")

        # Search for all print nodes
        results = search_blueprint_nodes("Print String")
        if results['success'] and results['data']['nodes']:
            node = results['data']['nodes'][0]
            print(f"Found: {node['spawnMetadata']['nodeName']!r} in {node['spawnMetadata']['category']!r}")
            print(f"ActionID: {node['spawnMetadata']['actionIdentifier']!r}")
    """
    if not search_term or not search_term.strip():
        return make_error_result("search_term cannot be empty")

    try:
        args = {'searchTerm': search_term.strip(), 'contextSensitive': context_sensitive, 'maxResults': max_results,
                'excludeVMFunctions': exclude_vm_functions, **_target_args(blueprint_path, graph)}
        if category and category.strip():
            args['category'] = category.strip()
        return _invoke('search_blueprint_nodes', args, _legacy_search)
    except Exception as e:
        log_error(f"search_blueprint_nodes failed: {e}")
        return make_error_result(str(e))


def add_node_to_graph(
    node_name: str,
    action_identifier: str,
    location_x: float = 0.0,
    location_y: float = 0.0,
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Add a Blueprint node to the currently focused graph.

    Use search_blueprint_nodes() first to get the actionIdentifier for the node.
    The response includes full pin information, allowing immediate connection
    without needing to call find_nodes_in_graph().

    NEXT STEP: After adding a node, use connect_pins() to wire the node's pins
    to other nodes using the nodeGuid and pinGuid values returned in this response.

    Args:
        node_name: The search result's spawnMetadata['nodeName'] (its menu name). Only used to tell
            apart actions that share an action_identifier.
        action_identifier: The search result's spawnMetadata['actionIdentifier'], unchanged (a stable ID).
            Identifiers from before NodeToCode 2.0's stable IDs (search text with '>' between lines) are
            still accepted for now.
        location_x: X position for node placement
        location_y: Y position for node placement
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                nodeGuid: str,
                nodeName: str,
                graphName: str,
                blueprintName: str,
                location: {x: float, y: float},
                # pinName is the stable pin name ('execute', 'then', 'InString', 'ReturnValue', ...);
                # displayName is the editor's label, which can differ and follows the editor language
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
            node_info['spawnMetadata']['nodeName'],
            node_info['spawnMetadata']['actionIdentifier'],
            location_x=400,
            location_y=200
        )
        if added['success']:
            print(f"Added node with GUID: {added['data']['nodeGuid']}")
            # Pin GUIDs are immediately available for connecting
            exec_pin = next(p for p in added['data']['outputPins'] if p['pinName'] == 'then')
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
        return _invoke('add_node_to_graph', {
            'nodeName': node_name.strip(), 'actionIdentifier': action_identifier.strip(),
            'location': {'x': location_x, 'y': location_y}, **_target_args(blueprint_path, graph)})
    except Exception as e:
        log_error(f"add_node_to_graph failed: {e}")
        return make_error_result(str(e))


# ============== Pin Operations ==============

def connect_pins(
    connections: List[Dict[str, Any]],
    break_existing_links: bool = False,
    blueprint_path: str = "",
    graph: str = ""
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
        break_existing_links: If True, a connection may replace an existing link (a linked data input, also
            through a conversion node, or a linked exec output); if False such a connection fails and names the link
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                succeeded: [{index, from: {nodeGuid, pinGuid}, to: {nodeGuid, pinGuid}, response, message?,
                             conversionNode?: {nodeGuid, title}}, ...],  # connect-pins' result (card 0342);
                             # 1.x keys, one release: fromNode, fromPin, toNode, toPin
                failed: [{index, errorCode, error, response?, message?}, ...],
                summary: {totalRequested: int, succeeded: int, failed: int}
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
        if isinstance(connections, str):
            connections = json.loads(connections)
        return _invoke('connect_pins', {
            'connections': connections, 'options': {'breakExistingLinks': bool(break_existing_links)},
            **_target_args(blueprint_path, graph)}, _legacy_connect)
    except Exception as e:
        log_error(f"connect_pins failed: {e}")
        return make_error_result(str(e))


def set_pin_value(
    node_guid: str,
    pin_guid: str,
    value: str,
    pin_name: str = "",
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Set the default value of an input pin on a Blueprint node.

    Only works on input pins that are not connected and not execution pins. The engine checks the value first,
    as set-input-pin-value does: a value the pin refuses (1.5 on an integer pin, a Blueprint asset path on a class
    pin) returns success False with the reason, and the pin keeps its old default. An empty value resets the pin to
    its default.

    Args:
        node_guid: GUID of the node containing the pin
        pin_guid: GUID of the pin to modify
        value: The value to set (as string); an enum takes an entry name, display name or index; "" resets the pin
        pin_name: Optional pin name for fallback lookup if GUID fails
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                nodeGuid: str,
                pinGuid: str,
                pinName: str,
                oldValue: str,
                newValue: str   # read back from the pin (an enum index comes back as the entry's name)
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
        args = {'nodeGuid': node_guid.strip(), 'pinGuid': pin_guid.strip(), 'value': value,
                **_target_args(blueprint_path, graph)}
        if pin_name and pin_name.strip():
            args['pinName'] = pin_name.strip()
        return _invoke('set_pin_value', args)
    except Exception as e:
        log_error(f"set_pin_value failed: {e}")
        return make_error_result(str(e))


# ============== Node Operations ==============

def delete_nodes(
    node_guids: List[str],
    preserve_connections: bool = False,
    force: bool = False,
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Delete nodes from the focused Blueprint graph.

    Args:
        node_guids: List of node GUIDs to delete
        preserve_connections: If True, reconnects the links that pass through each
            deleted node: the sources of its exec input to the targets of its exec
            output (only on a node with one exec input and one exec output), and
            through reroute knots. Data pins of other nodes are not bridged.
        force: If True, also deletes nodes the editor protects (a function entry,
            the entry and exit tunnels of a collapsed graph or macro)
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                deletedNodes: [{guid: str, title: str, type: str, graph: str,  # 1.x 'name' kept one release
                                preservedConnections: [{fromNode, fromPin, toNode, toPin, type}]  # when any
                               }, ...],
                deletedCount: int,
                skipped: [{guid: str, reason: str}, ...]  # only when a GUID was not deleted
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
        return _invoke('delete_nodes', {
            'nodeGuids': list(node_guids), 'preserveConnections': bool(preserve_connections), 'force': bool(force),
            **_target_args(blueprint_path, graph)}, _legacy_delete)
    except Exception as e:
        log_error(f"delete_nodes failed: {e}")
        return make_error_result(str(e))


def find_nodes_in_graph(
    search_terms: List[str],
    search_type: str = "keyword",
    case_sensitive: bool = False,
    max_results: int = 50,
    blueprint_path: str = "",
    graph: str = ""
) -> Dict[str, Any]:
    """
    Find nodes in the focused Blueprint graph by keywords or GUIDs.

    A keyword matches a node when it is a substring of the node's title (as shown, and its source text),
    its class name (e.g. 'K2Node_CallFunction') or its member name (e.g. 'PrintString'); one matching
    term is enough.

    Args:
        search_terms: List of keywords or GUIDs to search for
        search_type: Either "keyword" or "guid"
        case_sensitive: Whether keyword search is case-sensitive
        max_results: Maximum nodes to return (1-200)
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                nodes: [
                    {
                        ids: {short, guid}, type, class, title, member_parent, member_name,  # N2C JSON
                        input_pins: [{ids: {short, guid, name}, name, type, ...}, ...],
                        output_pins: [{ids: {short, guid, name}, name, ...}, ...],
                        # 1.x keys, one release: nodeGuid, nodeName, nodeClass, posX, posY,
                        # inputPins / outputPins: [{pinGuid, pinName, displayName, type}, ...]
                    },
                    ...
                ],
                target: {...},
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
        return _invoke('find_nodes_in_graph', {
            'searchTerms': list(search_terms), 'searchType': search_type, 'caseSensitive': bool(case_sensitive),
            'maxResults': max_results, **_target_args(blueprint_path, graph)}, _legacy_find)
    except Exception as e:
        log_error(f"find_nodes_in_graph failed: {e}")
        return make_error_result(str(e))


def create_comment_node(
    node_guids: List[str],
    comment_text: str = "Comment",
    color: Optional[Dict[str, float]] = None,
    font_size: int = 18,
    move_mode: str = "group",
    padding: float = 50.0,
    blueprint_path: str = "",
    graph: str = ""
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
        blueprint_path: Optional Blueprint asset path (e.g. "/Game/BP_Door"); empty acts on the focused Blueprint
        graph: Optional graph of that Blueprint (or of the focused one), by GUID or name; empty: the focused graph, or
            with blueprint_path alone its focused graph, else its event graph

    Returns:
        {
            success: bool,
            data: {
                commentNode: {guid, text, x, y, width, height, color, fontSize, moveMode},
                includedNodes: [{guid, title}, ...],
                includedCount: int,
                message: str,
                # 1.x keys, one release: commentGuid, commentText, includedNodeCount, position, size
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
        return _invoke('create_comment_node', {
            'nodeGuids': list(node_guids), 'commentText': comment_text, 'color': {'r': r, 'g': g, 'b': b},
            'fontSize': font_size, 'moveMode': move_mode, 'padding': padding,
            **_target_args(blueprint_path, graph)}, _legacy_comment)
    except Exception as e:
        log_error(f"create_comment_node failed: {e}")
        return make_error_result(str(e))


# ============== 1.x result keys (card 0342) ==============
# What the bridge's own copies of these operations returned, kept on the tools' results for one release.

def _legacy_pin(pin: Dict[str, Any]) -> Dict[str, Any]:
    ids = pin.get('ids') or {}
    return LegacyKeys(pin, 'find_nodes_in_graph', {
        'pinGuid': (legacy_guid(ids.get('guid')), "ids['guid']"),
        'pinName': (ids.get('name') or pin.get('name', ''), "ids['name']"),
        'displayName': (pin.get('name', ''), "name"),
        'type': ('exec', "type (N2C JSON names no type for an exec pin)"),
    })


def _legacy_search(data: Dict[str, Any]) -> Dict[str, Any]:
    nodes = []
    for node in data.get('nodes') or []:
        meta = node.get('spawnMetadata') or {}
        name = meta.get('nodeName', '')
        nodes.append(LegacyKeys(node, 'search_blueprint_nodes', {
            'name': (name, "spawnMetadata['nodeName']"),
            'displayName': (name, "spawnMetadata['nodeName']"),
            'menuName': (name, "spawnMetadata['nodeName']"),
            'category': (meta.get('category', ''), "spawnMetadata['category']"),
            'tooltip': ('', "the node's N2C JSON (tooltips are not returned)"),
            'keywords': ('', "the node's N2C JSON (keywords are not returned)"),
        }))
    data['nodes'] = nodes
    return {'count': (len(nodes), "len(data['nodes'])")}


def _legacy_connect(data: Dict[str, Any]) -> Dict[str, Any]:
    for key in ('succeeded', 'failed'):
        entries = []
        for entry in data.get(key) or []:
            if isinstance(entry, dict) and isinstance(entry.get('from'), dict):
                source, target = entry.get('from') or {}, entry.get('to') or {}
                entry = LegacyKeys(entry, 'connect_pins', {
                    'fromNode': (source.get('nodeGuid', ''), "from['nodeGuid']"),
                    'fromPin': (source.get('pinGuid', ''), "from['pinGuid']"),
                    'toNode': (target.get('nodeGuid', ''), "to['nodeGuid']"),
                    'toPin': (target.get('pinGuid', ''), "to['pinGuid']"),
                })
            entries.append(entry)
        if key in data:
            data[key] = entries
    return {}


def _legacy_delete(data: Dict[str, Any]) -> Dict[str, Any]:
    data['deletedNodes'] = [LegacyKeys(node, 'delete_nodes', {'name': (node.get('title', ''), "title")})
                            if isinstance(node, dict) else node for node in data.get('deletedNodes') or []]
    return {}


def _legacy_find(data: Dict[str, Any]) -> Dict[str, Any]:
    nodes = []
    for node in data.get('nodes') or []:
        ids = node.get('ids') or {}
        position = node.get('position') or {}
        nodes.append(LegacyKeys(node, 'find_nodes_in_graph', {
            'nodeGuid': (legacy_guid(ids.get('guid')), "ids['guid']"),
            'nodeName': (node.get('title', ''), "title"),
            'nodeClass': (node.get('class', ''), "class"),
            'posX': (position.get('x', 0), "position['x']"),
            'posY': (position.get('y', 0), "position['y']"),
            'inputPins': ([_legacy_pin(p) for p in node.get('input_pins') or []], "input_pins"),
            'outputPins': ([_legacy_pin(p) for p in node.get('output_pins') or []], "output_pins"),
        }))
    data['nodes'] = nodes
    return {}


def _legacy_comment(data: Dict[str, Any]) -> Dict[str, Any]:
    comment = data.get('commentNode') or {}
    return {
        'commentGuid': (legacy_guid(comment.get('guid')), "commentNode['guid']"),
        'commentText': (comment.get('text', ''), "commentNode['text']"),
        'includedNodeCount': (data.get('includedCount', 0), "includedCount"),
        'position': ({'x': comment.get('x', 0), 'y': comment.get('y', 0)}, "commentNode['x'] and ['y']"),
        'size': ({'width': comment.get('width', 0), 'height': comment.get('height', 0)},
                 "commentNode['width'] and ['height']"),
    }
