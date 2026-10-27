# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.bridge.open_blueprint's focus_graph handling (card 0286), on plain CPython with the `unreal`
stub from conftest.py.

UN2CPythonBridge::OpenBlueprint fails with "GRAPH_NOT_FOUND: ..." on a focus_graph that names no graph (before card
0286 it opened the Blueprint with focusedGraph "" and the next add_node_to_graph landed in the graph focused
before). The wrapper's default, EventGraph, must not turn that into a failure for a Blueprint without an EventGraph
(a Function Library, an Interface): it opens that Blueprint unfocused. A name the caller gave stays an error.
"""

import importlib
import json

import pytest

from conftest import UNREAL

NOT_FOUND = "GRAPH_NOT_FOUND: BFL_Math has no graph named 'EventGraph'. Available graphs: Add"


def writer_json(success, data=None, error=None):
    return json.dumps({'success': success, 'data': data, 'error': error}, separators=(',', ':'))


class _RecordingBridge:
    """Stand-in for unreal.N2CPythonBridge.open_blueprint: answers per focus_graph and records each call"""

    def __init__(self, answers):
        self.calls = []
        self._answers = answers
        bridge = self

        def open_blueprint(path, focus):
            bridge.calls.append((path, focus))
            return bridge._answers[focus]

        self.open_blueprint = open_blueprint


@pytest.fixture
def bridge_module():
    return importlib.import_module('nodetocode.bridge')


def opened(focused):
    return writer_json(True, {'blueprintName': 'BP', 'blueprintPath': '/Game/BP.BP', 'focusedGraph': focused,
                              'eventGraphCount': 1, 'functionCount': 0})


def test_default_focuses_the_event_graph(monkeypatch, bridge_module):
    stub = _RecordingBridge({'EventGraph': opened('EventGraph')})
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', stub, raising=False)
    result = bridge_module.open_blueprint('/Game/BP')
    assert result['success'] is True
    assert result['data']['focusedGraph'] == 'EventGraph'
    assert stub.calls == [('/Game/BP', 'EventGraph')]


def test_default_opens_a_blueprint_without_an_event_graph_unfocused(monkeypatch, bridge_module):
    stub = _RecordingBridge({'EventGraph': writer_json(False, None, NOT_FOUND), '': opened('')})
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', stub, raising=False)
    result = bridge_module.open_blueprint('/Game/BFL_Math')
    assert result['success'] is True
    assert result['data']['focusedGraph'] == ''
    assert stub.calls == [('/Game/BFL_Math', 'EventGraph'), ('/Game/BFL_Math', '')]


def test_an_unknown_name_the_caller_gave_fails_with_the_graph_list(monkeypatch, bridge_module):
    error = "GRAPH_NOT_FOUND: BP has no graph named 'Nope'. Available graphs: EventGraph, UserConstructionScript"
    stub = _RecordingBridge({'Nope': writer_json(False, None, error)})
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', stub, raising=False)
    result = bridge_module.open_blueprint('/Game/BP', 'Nope')
    assert result['success'] is False
    assert result['error'] == error
    assert stub.calls == [('/Game/BP', 'Nope')]


def test_the_name_is_passed_through_for_the_bridge_to_match(monkeypatch, bridge_module):
    stub = _RecordingBridge({'ConstructionScript': opened('UserConstructionScript')})
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', stub, raising=False)
    result = bridge_module.open_blueprint(' /Game/BP ', ' ConstructionScript ')
    assert result['data']['focusedGraph'] == 'UserConstructionScript'
    assert stub.calls == [('/Game/BP', 'ConstructionScript')]
