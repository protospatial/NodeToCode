# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for the blueprint_path / graph target arguments of the nodetocode wrappers (card 0274), on plain CPython with
the `unreal` stub from conftest.py.

The bridge's focus-based calls take optional BlueprintPath and Graph (UN2CPythonBridge, FN2CGraphTarget). A wrapper
passes them as keyword arguments only when given, so a call without them is the same bridge call as before (the
focused graph), and blank values count as not given.
"""

import importlib
import json

import pytest

from conftest import UNREAL

OK = json.dumps({'success': True, 'data': {}, 'error': None})


class _RecordingBridge:
    """Stand-in for unreal.N2CPythonBridge: every attribute is a function that records its call"""

    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        def call(*args, **kwargs):
            self.calls.append((name, args, kwargs))
            return OK
        return call


@pytest.fixture
def stub(monkeypatch):
    bridge = _RecordingBridge()
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', bridge, raising=False)
    return bridge


# (module, wrapper, positional arguments, bridge function)
CALLS = [
    ('nodetocode.graph', 'search_blueprint_nodes', ('Print String',), 'search_blueprint_nodes'),
    ('nodetocode.graph', 'add_node_to_graph', ('Print String', 'id'), 'add_node_to_graph'),
    ('nodetocode.graph', 'connect_pins', ([{'from': {}, 'to': {}}],), 'connect_pins'),
    ('nodetocode.graph', 'set_pin_value', ('N', 'P', '1'), 'set_pin_value'),
    ('nodetocode.graph', 'delete_nodes', (['N'],), 'delete_nodes'),
    ('nodetocode.graph', 'find_nodes_in_graph', (['Print'],), 'find_nodes_in_graph'),
    ('nodetocode.graph', 'create_comment_node', (['N'],), 'create_comment_node'),
    ('nodetocode.functions', 'add_function_input_pin', ('Speed', 'float'), 'add_function_input_pin'),
    ('nodetocode.functions', 'add_function_return_pin', ('Speed', 'float'), 'add_function_return_pin'),
    ('nodetocode.functions', 'remove_function_entry_pin', ('Speed',), 'remove_function_entry_pin'),
    ('nodetocode.functions', 'remove_function_return_pin', ('Speed',), 'remove_function_return_pin'),
    ('nodetocode.bridge', 'tag_graph', ('Movement',), 'tag_focused_graph'),
]


@pytest.mark.parametrize('module,wrapper,args,bridge_name', CALLS, ids=[c[1] for c in CALLS])
def test_the_target_reaches_the_bridge(stub, module, wrapper, args, bridge_name):
    fn = getattr(importlib.import_module(module), wrapper)
    result = fn(*args, blueprint_path=' /Game/BP_Door ', graph='OpenDoor')
    assert result['success'] is True
    name, _, kwargs = stub.calls[-1]
    assert name == bridge_name
    assert kwargs == {'blueprint_path': '/Game/BP_Door', 'graph': 'OpenDoor'}


@pytest.mark.parametrize('module,wrapper,args,bridge_name', CALLS, ids=[c[1] for c in CALLS])
def test_without_a_target_the_call_is_unchanged(stub, module, wrapper, args, bridge_name):
    fn = getattr(importlib.import_module(module), wrapper)
    fn(*args)
    fn(*args, blueprint_path='  ', graph='')
    assert [c[2] for c in stub.calls if c[0] == bridge_name] == [{}, {}]
