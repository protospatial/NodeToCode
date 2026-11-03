# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.blueprint.get_blueprint_n2cjson and list_blueprint_graphs (cards 0157, 0301) on plain CPython with
the `unreal` stub from conftest.py: the wrappers call UN2CEditorSubsystem.GetBlueprintN2CJson and ListBlueprintGraphs
with their arguments in order, turn the struct into the {success, data, error} dict the bridge versions returned, turn
the subsystem's raised error into the error text from its code on, and refuse an empty path before calling it. The C++
side is NodeToCode.MCP.BlueprintByPath and NodeToCode.Editor.Subsystem.
"""

import importlib
import json
from types import SimpleNamespace

import pytest

from conftest import UNREAL

PATH = '/Game/N2CTest/BP_Door'


class _Subsystem:
    """Records each call and answers with the struct (a SimpleNamespace) or raises the RuntimeError the test gives"""

    def __init__(self, answer):
        self.calls = []
        self._answer = answer

    def _reply(self, name, args):
        self.calls.append((name, args))
        if isinstance(self._answer, Exception):
            raise self._answer
        return self._answer

    def get_blueprint_n2c_json(self, *args):
        return self._reply('get_blueprint_n2c_json', args)

    def list_blueprint_graphs(self, *args):
        return self._reply('list_blueprint_graphs', args)


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def _subsystem(monkeypatch, answer):
    subsystem = _Subsystem(answer)
    monkeypatch.setattr(UNREAL, 'N2CEditorSubsystem', object(), raising=False)
    monkeypatch.setattr(UNREAL, 'get_editor_subsystem',
                        lambda cls: subsystem if cls is UNREAL.N2CEditorSubsystem else None, raising=False)
    return subsystem


def _read(**fields):
    base = dict(untrusted_content='notice', blueprint_path=PATH + '.BP_Door', blueprint_name='BP_Door', graph_count=1,
                node_count=3, estimated_tokens=10, n2c_json='{"version": "2.0.0"}', file_path='', file_bytes=0,
                graph_name='', graph_guid='', graph_kind='', resolved_by='', deprecated_id='', warning='')
    base.update(fields)
    return SimpleNamespace(**base)


def test_the_package_exports_both(blueprint_module):
    package = importlib.import_module('nodetocode')
    for name in ('get_blueprint_n2cjson', 'list_blueprint_graphs'):
        assert name in package.__all__
        assert getattr(package, name) is getattr(blueprint_module, name)


def test_get_blueprint_n2cjson_passes_its_arguments_in_order(blueprint_module, monkeypatch):
    subsystem = _subsystem(monkeypatch, _read(file_path='C:/x.json', file_bytes=42, n2c_json=''))
    result = blueprint_module.get_blueprint_n2cjson(PATH, graph_name='Fn', graph_guid='A1B2', depth=2, to_file=True,
                                                    compact=False)
    assert result['success'] is True
    assert result['data'] == {'blueprintPath': PATH + '.BP_Door', 'blueprintName': 'BP_Door', 'graphCount': 1,
                              'nodeCount': 3, 'estimatedTokens': 10, 'path': 'C:/x.json', 'bytes': 42}
    # The GUID wins over the name, as in the MCP tool
    assert subsystem.calls == [('get_blueprint_n2c_json', (PATH, 'A1B2', 2, True, False))]


def test_get_blueprint_n2cjson_returns_the_document_and_the_graph_read(blueprint_module, monkeypatch):
    subsystem = _subsystem(monkeypatch, _read(graph_name='EventGraph', graph_guid='A1B2', graph_kind='event',
                                              resolved_by='explicit'))
    result = blueprint_module.get_blueprint_n2cjson(PATH, graph_name='EventGraph')
    assert result['data']['n2c_json'] == {'version': '2.0.0'}
    assert result['data']['target'] == {'blueprintPath': PATH + '.BP_Door', 'blueprintName': 'BP_Door',
                                        'graphName': 'EventGraph', 'graphGuid': 'A1B2', 'graphKind': 'event',
                                        'resolvedBy': 'explicit'}
    assert subsystem.calls == [('get_blueprint_n2c_json', (PATH, 'EventGraph', -1, False, True))]


def test_get_blueprint_n2cjson_defaults_read_every_graph_at_the_project_depth(blueprint_module, monkeypatch):
    subsystem = _subsystem(monkeypatch, _read())
    result = blueprint_module.get_blueprint_n2cjson(PATH)
    assert 'target' not in result['data']
    assert subsystem.calls == [('get_blueprint_n2c_json', (PATH, '', -1, False, True))]


def test_raised_errors_come_back_with_their_code(blueprint_module, monkeypatch):
    _subsystem(monkeypatch, RuntimeError(f"GetBlueprintN2CJson: {PATH}: GRAPH_NOT_FOUND: no graph 'Nope' in {PATH}"))
    result = blueprint_module.get_blueprint_n2cjson(PATH, graph_name='Nope')
    assert result['success'] is False
    assert result['error'] == f"GRAPH_NOT_FOUND: no graph 'Nope' in {PATH}"


def test_list_blueprint_graphs_turns_the_struct_into_the_dict(blueprint_module, monkeypatch):
    graphs = [SimpleNamespace(name='EventGraph', graph_guid='A1B2', kind='event', node_count=3, read_only=False,
                              translatable=True, parent_graph=''),
              SimpleNamespace(name='Inner', graph_guid='C3D4', kind='collapsed', node_count=2, read_only=False,
                              translatable=True, parent_graph='EventGraph')]
    subsystem = _subsystem(monkeypatch, SimpleNamespace(blueprint_path=PATH + '.BP_Door', blueprint_name='BP_Door',
                                                        graphs=graphs))
    result = blueprint_module.list_blueprint_graphs(PATH)
    assert result == {'success': True, 'error': None, 'data': {
        'blueprintPath': PATH + '.BP_Door', 'blueprintName': 'BP_Door', 'graphCount': 2,
        'graphs': [{'name': 'EventGraph', 'graphGuid': 'A1B2', 'kind': 'event', 'nodeCount': 3, 'readOnly': False,
                    'translatable': True},
                   {'name': 'Inner', 'graphGuid': 'C3D4', 'kind': 'collapsed', 'nodeCount': 2, 'readOnly': False,
                    'translatable': True, 'parentGraph': 'EventGraph'}]}}
    assert subsystem.calls == [('list_blueprint_graphs', (PATH,))]


def test_list_blueprint_graphs_reports_a_missing_blueprint(blueprint_module, monkeypatch):
    _subsystem(monkeypatch, RuntimeError(f'ListBlueprintGraphs: {PATH}: ASSET_NOT_FOUND: no Blueprint at {PATH}'))
    result = blueprint_module.list_blueprint_graphs(PATH)
    assert result['success'] is False
    assert result['error'].startswith('ASSET_NOT_FOUND')


@pytest.mark.parametrize('call', ['get_blueprint_n2cjson', 'list_blueprint_graphs'])
def test_an_empty_path_is_refused_before_the_subsystem(blueprint_module, monkeypatch, call):
    subsystem = _subsystem(monkeypatch, _read())
    result = getattr(blueprint_module, call)('')
    assert result['success'] is False
    assert result['error'].startswith('INVALID_ARGUMENT')
    assert subsystem.calls == []
