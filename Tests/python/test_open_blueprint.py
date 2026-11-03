# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.bridge.open_blueprint's focus_graph handling (card 0286), on plain CPython with the `unreal`
stub from conftest.py.

open-blueprint-asset (which open_blueprint runs since card 0342) fails with "GRAPH_NOT_FOUND: ..." on a focus_graph
that names no graph. The wrapper's default, EventGraph, must not turn that into a failure for a Blueprint without
an EventGraph (a Function Library, an Interface): it opens that Blueprint unfocused. A name the caller gave stays an
error. The tool's result keeps the 1.x keys (blueprintName, blueprintPath, focusedGraph, ...) for one release.
"""

import importlib

import pytest

NOT_FOUND = "GRAPH_NOT_FOUND: BFL_Math has no graph named 'EventGraph'. Available graphs: Add"


def opener(answers):
    """open-blueprint-asset's answer per focus_graph"""
    def answer(arguments):
        return answers[arguments.get('focus_graph', '')]
    return answer


def opened(focused):
    return {'success': True, 'asset_path': '/Game/BP', 'blueprint_name': 'BP', 'focused_graph': focused,
            'graph_counts': {'event_graphs': 1, 'function_graphs': 0, 'macro_graphs': 0, 'delegate_graphs': 0}}


@pytest.fixture
def bridge_module():
    return importlib.import_module('nodetocode.bridge')


def focus_graphs(tools):
    return [(arguments['asset_path'], arguments.get('focus_graph', '')) for _name, arguments in tools.calls]


def test_default_focuses_the_event_graph(tools, bridge_module):
    tools.answers['open-blueprint-asset'] = opener({'EventGraph': opened('EventGraph')})
    result = bridge_module.open_blueprint('/Game/BP')
    assert result['success'] is True
    assert result['data']['focused_graph'] == 'EventGraph'
    assert result['data']['focusedGraph'] == 'EventGraph'
    assert result['data']['blueprintPath'] == '/Game/BP.BP'
    assert focus_graphs(tools) == [('/Game/BP', 'EventGraph')]


def test_default_opens_a_blueprint_without_an_event_graph_unfocused(tools, bridge_module):
    tools.answers['open-blueprint-asset'] = opener({'EventGraph': NOT_FOUND, '': opened('')})
    result = bridge_module.open_blueprint('/Game/BFL_Math')
    assert result['success'] is True
    assert result['data']['focusedGraph'] == ''
    assert focus_graphs(tools) == [('/Game/BFL_Math', 'EventGraph'), ('/Game/BFL_Math', '')]


def test_an_unknown_name_the_caller_gave_fails_with_the_graph_list(tools, bridge_module):
    error = "GRAPH_NOT_FOUND: BP has no graph named 'Nope'. Available graphs: EventGraph, UserConstructionScript"
    tools.answers['open-blueprint-asset'] = opener({'Nope': error})
    result = bridge_module.open_blueprint('/Game/BP', 'Nope')
    assert result['success'] is False
    assert result['error'] == error
    assert focus_graphs(tools) == [('/Game/BP', 'Nope')]


def test_the_name_is_passed_through_for_the_tool_to_match(tools, bridge_module):
    tools.answers['open-blueprint-asset'] = opener({'ConstructionScript': opened('UserConstructionScript')})
    result = bridge_module.open_blueprint(' /Game/BP ', ' ConstructionScript ')
    assert result['data']['focusedGraph'] == 'UserConstructionScript'
    assert focus_graphs(tools) == [('/Game/BP', 'ConstructionScript')]
