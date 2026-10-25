# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.blueprint.get_blueprint_n2cjson and list_blueprint_graphs (card 0157) on plain CPython with the
`unreal` stub from conftest.py: the wrappers pass their arguments to UN2CPythonBridge.GetBlueprintN2CJson and
ListBlueprintGraphs in order, parse the bridge's response, and refuse an empty path before calling the bridge. The
C++ side (the reads themselves) is NodeToCode.MCP.BlueprintByPath.
"""

import importlib
import json

import pytest

from conftest import UNREAL

PATH = '/Game/N2CTest/BP_Door'


class _Bridge:
    """Records each call to the two bridge functions and answers with the text the test gives"""

    def __init__(self, response):
        self.calls = []
        self._response = response

    def get_blueprint_n2c_json(self, *args):
        self.calls.append(('get_blueprint_n2c_json', args))
        return self._response

    def list_blueprint_graphs(self, *args):
        self.calls.append(('list_blueprint_graphs', args))
        return self._response


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def _bridge(monkeypatch, success, data=None, error=None):
    bridge = _Bridge(json.dumps({'success': success, 'data': data, 'error': error}))
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', bridge, raising=False)
    return bridge


def test_the_package_exports_both(blueprint_module):
    package = importlib.import_module('nodetocode')
    for name in ('get_blueprint_n2cjson', 'list_blueprint_graphs'):
        assert name in package.__all__
        assert getattr(package, name) is getattr(blueprint_module, name)


def test_get_blueprint_n2cjson_passes_its_arguments_in_order(blueprint_module, monkeypatch):
    data = {'blueprintPath': PATH + '.BP_Door', 'graphCount': 1, 'nodeCount': 3, 'n2c_json': {'version': '2.0.0'}}
    bridge = _bridge(monkeypatch, True, data)
    result = blueprint_module.get_blueprint_n2cjson(PATH, graph_guid='A1B2', depth=2, to_file=True, compact=False)
    assert result == {'success': True, 'data': data, 'error': None}
    assert bridge.calls == [('get_blueprint_n2c_json', (PATH, '', 'A1B2', 2, True, False))]


def test_get_blueprint_n2cjson_defaults_read_every_graph_at_the_project_depth(blueprint_module, monkeypatch):
    bridge = _bridge(monkeypatch, True, {'graphCount': 4})
    blueprint_module.get_blueprint_n2cjson(PATH)
    assert bridge.calls == [('get_blueprint_n2c_json', (PATH, '', '', -1, False, True))]


def test_bridge_errors_come_back_with_their_code(blueprint_module, monkeypatch):
    _bridge(monkeypatch, False, None, "GRAPH_NOT_FOUND: no graph 'Nope' in " + PATH)
    result = blueprint_module.get_blueprint_n2cjson(PATH, graph_name='Nope')
    assert result['success'] is False
    assert result['error'].startswith('GRAPH_NOT_FOUND')


def test_list_blueprint_graphs_parses_the_bridge_result(blueprint_module, monkeypatch):
    data = {'blueprintPath': PATH + '.BP_Door', 'graphCount': 1,
            'graphs': [{'name': 'EventGraph', 'graphGuid': 'A1B2', 'kind': 'event', 'nodeCount': 3, 'readOnly': False}]}
    bridge = _bridge(monkeypatch, True, data)
    assert blueprint_module.list_blueprint_graphs(PATH) == {'success': True, 'data': data, 'error': None}
    assert bridge.calls == [('list_blueprint_graphs', (PATH,))]


@pytest.mark.parametrize('call', ['get_blueprint_n2cjson', 'list_blueprint_graphs'])
def test_an_empty_path_is_refused_before_the_bridge(blueprint_module, monkeypatch, call):
    bridge = _bridge(monkeypatch, True, {})
    result = getattr(blueprint_module, call)('')
    assert result['success'] is False
    assert result['error'].startswith('INVALID_ARGUMENT')
    assert bridge.calls == []
