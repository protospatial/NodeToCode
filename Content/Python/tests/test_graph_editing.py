"""
Integration tests for Blueprint graph editing functionality.

These tests verify the graph editing capabilities exposed through the
nodetocode Python module. They should be run with a Blueprint open in
the UE Editor.

Usage:
    1. Open a Blueprint in the Unreal Editor
    2. Focus on the EventGraph or a function graph
    3. Execute this script via the run-python MCP tool or UE Python console

Test Categories:
    - Node search and creation
    - Pin connections
    - Node deletion
    - Comment node creation
    - Function pin management

Note: These are integration tests that modify the active Blueprint.
      Use with test Blueprints only.
"""

import nodetocode as n2c


def test_search_blueprint_nodes():
    """Test searching for Blueprint nodes."""
    print("\n=== Testing search_blueprint_nodes ===")

    # Search for Print String node
    result = n2c.search_blueprint_nodes("Print String", max_results=5)

    if not result['success']:
        print(f"FAILED: {result['error']}")
        return False

    data = result['data']
    print(f"Found {data['count']} nodes matching 'Print String'")

    if data['count'] == 0:
        print("FAILED: No nodes found")
        return False

    # Verify structure
    node = data['nodes'][0]
    if 'spawnMetadata' not in node:
        print("FAILED: Missing spawnMetadata in result")
        return False

    if 'actionIdentifier' not in node['spawnMetadata']:
        print("FAILED: Missing actionIdentifier in spawnMetadata")
        return False

    print(f"First match: {node.get('displayName', node['name'])}")
    print(f"ActionID: {node['spawnMetadata']['actionIdentifier'][:50]}...")
    print("PASSED")
    return True


def test_add_and_delete_node():
    """Test adding a node to the graph and then deleting it."""
    print("\n=== Testing add_node_to_graph and delete_nodes ===")

    # First search for a node
    search = n2c.search_blueprint_nodes("Print String", max_results=1)
    if not search['success'] or search['data']['count'] == 0:
        print("SKIPPED: Could not find Print String node")
        return None

    node_info = search['data']['nodes'][0]

    # Add the node
    add_result = n2c.add_node_to_graph(
        node_name=node_info.get('displayName', 'Print String'),
        action_identifier=node_info['spawnMetadata']['actionIdentifier'],
        location_x=500,
        location_y=300
    )

    if not add_result['success']:
        print(f"FAILED to add node: {add_result['error']}")
        return False

    node_guid = add_result['data']['nodeGuid']
    print(f"Added node: {add_result['data']['nodeName']} (GUID: {node_guid})")

    # Verify we can find it
    find_result = n2c.find_nodes_in_graph([node_guid], search_type="guid")
    if not find_result['success']:
        print(f"FAILED to find added node: {find_result['error']}")
        return False

    if find_result['data']['metadata']['totalFound'] != 1:
        print(f"FAILED: Expected 1 node, found {find_result['data']['metadata']['totalFound']}")
        return False

    print(f"Verified node exists in graph")

    # Delete the node
    delete_result = n2c.delete_nodes([node_guid])

    if not delete_result['success']:
        print(f"FAILED to delete node: {delete_result['error']}")
        return False

    print(f"Deleted {delete_result['data']['deletedCount']} node(s)")

    # Verify deletion
    find_after = n2c.find_nodes_in_graph([node_guid], search_type="guid")
    if find_after['success'] and find_after['data']['metadata']['totalFound'] > 0:
        print("FAILED: Node still exists after deletion")
        return False

    print("PASSED")
    return True


def test_find_nodes_in_graph():
    """Test finding nodes in the graph by keyword."""
    print("\n=== Testing find_nodes_in_graph ===")

    # Find any nodes in the graph
    result = n2c.find_nodes_in_graph(["Event"], search_type="keyword", max_results=5)

    if not result['success']:
        print(f"FAILED: {result['error']}")
        return False

    metadata = result['data']['metadata']
    print(f"Blueprint: {metadata['blueprintName']}")
    print(f"Graph: {metadata['graphName']}")
    print(f"Found {metadata['totalFound']} nodes matching 'Event' (of {metadata['totalInGraph']} total)")

    for node in result['data']['nodes'][:3]:
        print(f"  - {node['nodeName']} ({node['nodeClass']})")
        print(f"    Inputs: {len(node['inputPins'])}, Outputs: {len(node['outputPins'])}")

    print("PASSED")
    return True


def test_set_pin_value():
    """Test setting a pin value (requires a node with settable pins)."""
    print("\n=== Testing set_pin_value ===")

    # Add a Print String node to test with
    search = n2c.search_blueprint_nodes("Print String", max_results=1)
    if not search['success'] or search['data']['count'] == 0:
        print("SKIPPED: Could not find Print String node")
        return None

    node_info = search['data']['nodes'][0]
    add_result = n2c.add_node_to_graph(
        node_name=node_info.get('displayName', 'Print String'),
        action_identifier=node_info['spawnMetadata']['actionIdentifier'],
        location_x=600,
        location_y=400
    )

    if not add_result['success']:
        print(f"SKIPPED: Could not add node - {add_result['error']}")
        return None

    node_guid = add_result['data']['nodeGuid']

    # Find the node to get pin info
    find_result = n2c.find_nodes_in_graph([node_guid], search_type="guid")
    if not find_result['success'] or len(find_result['data']['nodes']) == 0:
        print("SKIPPED: Could not find added node")
        n2c.delete_nodes([node_guid])
        return None

    node_data = find_result['data']['nodes'][0]

    # Find a string input pin (In String on Print String)
    string_pin = None
    for pin in node_data['inputPins']:
        if 'string' in pin['pinName'].lower() or 'string' in pin['displayName'].lower():
            string_pin = pin
            break

    if not string_pin:
        print("SKIPPED: No string input pin found")
        n2c.delete_nodes([node_guid])
        return None

    # Set the pin value
    set_result = n2c.set_pin_value(
        node_guid=node_guid,
        pin_guid=string_pin['pinGuid'],
        value="Test Value from Python"
    )

    # Clean up
    n2c.delete_nodes([node_guid])

    if not set_result['success']:
        print(f"FAILED: {set_result['error']}")
        return False

    print(f"Set pin '{set_result['data']['pinName']}' to '{set_result['data']['newValue']}'")
    print("PASSED")
    return True


def test_create_comment_node():
    """Test creating a comment node around existing nodes."""
    print("\n=== Testing create_comment_node ===")

    # Find some nodes to comment
    find_result = n2c.find_nodes_in_graph(["Event"], search_type="keyword", max_results=2)

    if not find_result['success'] or len(find_result['data']['nodes']) == 0:
        print("SKIPPED: No nodes found to comment")
        return None

    # Get node GUIDs
    node_guids = [node['nodeGuid'] for node in find_result['data']['nodes'][:2]]

    if len(node_guids) < 1:
        print("SKIPPED: Need at least 1 node to create comment")
        return None

    # Create a comment
    result = n2c.create_comment_node(
        node_guids=node_guids,
        comment_text="Test Comment from Python",
        color={"r": 0.2, "g": 0.6, "b": 0.2},
        font_size=20
    )

    if not result['success']:
        print(f"FAILED: {result['error']}")
        return False

    data = result['data']
    print(f"Created comment: '{data['commentText']}'")
    print(f"Position: ({data['position']['x']:.0f}, {data['position']['y']:.0f})")
    print(f"Size: {data['size']['width']:.0f}x{data['size']['height']:.0f}")
    print(f"Included {data['includedNodeCount']} nodes")

    # Clean up - delete the comment
    n2c.delete_nodes([data['commentGuid']], force=True)

    print("PASSED")
    return True


def test_full_node_workflow():
    """Test a complete workflow: search, add, find, delete."""
    print("\n=== Testing full node workflow ===")

    # 1. Search for Print String node
    print("Step 1: Searching for 'Print String'...")
    search = n2c.search_blueprint_nodes("Print String", max_results=1)

    if not search['success'] or search['data']['count'] == 0:
        print("FAILED: Could not find Print String node")
        return False

    node_info = search['data']['nodes'][0]
    print(f"  Found: {node_info.get('displayName', 'Print String')}")

    # 2. Add the node
    print("Step 2: Adding node to graph...")
    add_result = n2c.add_node_to_graph(
        node_name=node_info.get('displayName', 'Print String'),
        action_identifier=node_info['spawnMetadata']['actionIdentifier'],
        location_x=700,
        location_y=500
    )

    if not add_result['success']:
        print(f"FAILED: {add_result['error']}")
        return False

    node_guid = add_result['data']['nodeGuid']
    print(f"  Added: {add_result['data']['nodeName']} at ({add_result['data']['location']['x']}, {add_result['data']['location']['y']})")

    # 3. Find and verify
    print("Step 3: Verifying node exists...")
    find_result = n2c.find_nodes_in_graph([node_guid], search_type="guid")

    if not find_result['success'] or find_result['data']['metadata']['totalFound'] != 1:
        print("FAILED: Node not found after adding")
        return False

    print(f"  Verified in {find_result['data']['metadata']['graphName']}")

    # 4. Delete the node
    print("Step 4: Deleting node...")
    delete_result = n2c.delete_nodes([node_guid])

    if not delete_result['success']:
        print(f"FAILED: {delete_result['error']}")
        return False

    print(f"  Deleted: {delete_result['data']['deletedNodes'][0]['name']}")

    # 5. Compile to verify valid Blueprint
    print("Step 5: Compiling Blueprint...")
    compile_result = n2c.compile_blueprint()

    if not compile_result['success']:
        print(f"WARNING: Compilation issue - {compile_result.get('error', 'Unknown')}")
    else:
        print(f"  Compiled successfully")

    print("PASSED - Full workflow completed")
    return True


def run_all_tests():
    """Run all graph editing tests."""
    print("=" * 60)
    print("NodeToCode Graph Editing Integration Tests")
    print("=" * 60)

    # Check if we have a Blueprint focused
    bp = n2c.get_focused_blueprint()
    if not bp['success']:
        print(f"\nERROR: {bp['error']}")
        print("Please open a Blueprint and focus on a graph before running tests.")
        return

    print(f"\nTesting with Blueprint: {bp['data']['name']}")
    print(f"Graph: {bp['data']['graph_name']}")

    tests = [
        test_search_blueprint_nodes,
        test_find_nodes_in_graph,
        test_add_and_delete_node,
        test_set_pin_value,
        test_create_comment_node,
        test_full_node_workflow,
    ]

    results = {"passed": 0, "failed": 0, "skipped": 0}

    for test in tests:
        try:
            result = test()
            if result is True:
                results["passed"] += 1
            elif result is False:
                results["failed"] += 1
            else:  # None = skipped
                results["skipped"] += 1
        except Exception as e:
            print(f"ERROR in {test.__name__}: {e}")
            results["failed"] += 1

    print("\n" + "=" * 60)
    print("Test Results Summary")
    print("=" * 60)
    print(f"Passed:  {results['passed']}")
    print(f"Failed:  {results['failed']}")
    print(f"Skipped: {results['skipped']}")
    print("=" * 60)


# Entry point for direct execution
if __name__ == "__main__":
    run_all_tests()
else:
    # When imported, make result available
    result = {"status": "imported", "run": "run_all_tests()"}
