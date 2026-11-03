# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for the blueprint_path / graph target arguments of the nodetocode wrappers (card 0274), on plain CPython with
the `unreal` stub from conftest.py.

Since card 0342 the wrappers run the MCP tools through UN2CPythonBridge::InvokeTool: blueprint_path becomes the
tool's blueprintPath, and graph its graphGuid when it is a GUID, else its graphName. A wrapper passes them only when
given, so a call without them acts on the focused graph, and blank values count as not given.
"""

import importlib

import pytest

TARGET_KEYS = {'blueprintPath', 'graphName', 'graphGuid'}

# (module, wrapper, positional arguments, tool)
CALLS = [
    ('nodetocode.graph', 'search_blueprint_nodes', ('Print String',), 'search-blueprint-nodes'),
    ('nodetocode.graph', 'add_node_to_graph', ('Print String', 'id'), 'add-bp-node-to-active-graph'),
    ('nodetocode.graph', 'connect_pins', ([{'from': {}, 'to': {}}],), 'connect-pins'),
    ('nodetocode.graph', 'set_pin_value', ('N', 'P', '1'), 'set-input-pin-value'),
    ('nodetocode.graph', 'delete_nodes', (['N'],), 'delete-blueprint-node'),
    ('nodetocode.graph', 'find_nodes_in_graph', (['Print'],), 'find-nodes-in-graph'),
    ('nodetocode.graph', 'create_comment_node', (['N'],), 'create-comment-node'),
    ('nodetocode.functions', 'add_function_input_pin', ('Speed', 'float'), 'add-function-input-pin'),
    ('nodetocode.functions', 'add_function_return_pin', ('Speed', 'float'), 'add-function-return-pin'),
    ('nodetocode.functions', 'remove_function_entry_pin', ('Speed',), 'remove-function-entry-pin'),
    ('nodetocode.functions', 'remove_function_return_pin', ('Speed',), 'remove-function-return-pin'),
    ('nodetocode.bridge', 'tag_graph', ('Movement',), 'tag-blueprint-graph'),
]


@pytest.mark.parametrize('module,wrapper,args,tool', CALLS, ids=[c[1] for c in CALLS])
def test_the_target_reaches_the_tool(tools, module, wrapper, args, tool):
    fn = getattr(importlib.import_module(module), wrapper)
    result = fn(*args, blueprint_path=' /Game/BP_Door ', graph='OpenDoor')
    assert result['success'] is True
    name, arguments = tools.calls[-1]
    assert name == tool
    assert {k: v for k, v in arguments.items() if k in TARGET_KEYS} == {'blueprintPath': '/Game/BP_Door',
                                                                        'graphName': 'OpenDoor'}


@pytest.mark.parametrize('module,wrapper,args,tool', CALLS, ids=[c[1] for c in CALLS])
def test_a_graph_guid_is_passed_as_graph_guid(tools, module, wrapper, args, tool):
    fn = getattr(importlib.import_module(module), wrapper)
    for guid in ('A1B2C3D4-0000-0000-0000-000000000001', 'A1B2C3D4000000000000000000000001'):
        fn(*args, graph=' %s ' % guid)
        assert {k: v for k, v in tools.calls[-1][1].items() if k in TARGET_KEYS} == {'graphGuid': guid}


@pytest.mark.parametrize('module,wrapper,args,tool', CALLS, ids=[c[1] for c in CALLS])
def test_without_a_target_the_focused_graph_is_used(tools, module, wrapper, args, tool):
    fn = getattr(importlib.import_module(module), wrapper)
    fn(*args)
    fn(*args, blueprint_path='  ', graph='')
    calls = [arguments for name, arguments in tools.calls if name == tool]
    assert len(calls) == 2
    assert all(not (TARGET_KEYS & set(arguments)) for arguments in calls)
