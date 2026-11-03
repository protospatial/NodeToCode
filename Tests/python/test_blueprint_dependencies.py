# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.blueprint.get_blueprint_dependencies (card 0319) on plain CPython with the `unreal` stub from
conftest.py: the wrapper passes a list of paths and its three flags to UN2CPythonBridge.GetBlueprintDependencies in
order, takes one path as a string, parses the bridge's response, and refuses empty paths before calling the bridge.
The C++ side (the index and its JSON) is NodeToCode.MCP.BlueprintDependencies.
"""

import importlib
import json

import pytest

from conftest import UNREAL

FOLDER = '/Game/N2CTest/Dependencies'


class _Bridge:
    def __init__(self, response):
        self.calls = []
        self._response = response

    def get_blueprint_dependencies(self, *args):
        self.calls.append(args)
        return self._response


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def _bridge(monkeypatch, success, data=None, error=None):
    bridge = _Bridge(json.dumps({'success': success, 'data': data, 'error': error}))
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', bridge, raising=False)
    return bridge


def test_the_package_exports_it(blueprint_module):
    package = importlib.import_module('nodetocode')
    assert 'get_blueprint_dependencies' in package.__all__
    assert package.get_blueprint_dependencies is blueprint_module.get_blueprint_dependencies


def test_passes_paths_and_flags_in_order_and_returns_the_index(blueprint_module, monkeypatch):
    data = {'roots': [FOLDER, '/Game/AI/BP_Bot'], 'nodeCount': 2, 'edgeCount': 1,
            'nodes': [{'path': '/Game/AI/BP_Bot.BP_Bot', 'name': 'BP_Bot', 'kind': 'blueprint', 'root': True}],
            'edges': [{'from': '/Game/AI/BP_Bot.BP_Bot', 'to': '/Game/S.S', 'kind': 'variable-type', 'node': 'Data'}],
            'order': ['/Game/S.S', '/Game/AI/BP_Bot.BP_Bot'], 'cycleGroups': [], 'warnings': [], 'inline': True}
    bridge = _bridge(monkeypatch, True, data)
    result = blueprint_module.get_blueprint_dependencies([FOLDER, '/Game/AI/BP_Bot'], include_call_graph=False,
                                                         include_referenced=False, to_file=True)
    assert result == {'success': True, 'data': data, 'error': None}
    assert bridge.calls == [([FOLDER, '/Game/AI/BP_Bot'], False, False, True)]


def test_one_path_as_a_string_and_the_defaults(blueprint_module, monkeypatch):
    bridge = _bridge(monkeypatch, True, {'nodeCount': 8})
    blueprint_module.get_blueprint_dependencies(FOLDER)
    assert bridge.calls == [([FOLDER], True, True, False)]


def test_bridge_errors_come_back_with_their_code(blueprint_module, monkeypatch):
    _bridge(monkeypatch, False, None, 'NOT_FOUND: no Blueprint, Blueprint interface, macro library, user struct or user enum under /Game/Nope.')
    result = blueprint_module.get_blueprint_dependencies('/Game/Nope')
    assert result['success'] is False
    assert result['error'].startswith('NOT_FOUND')


@pytest.mark.parametrize('paths', ['', [], ['', '  '], None, [3]])
def test_empty_paths_are_refused_before_the_bridge(blueprint_module, monkeypatch, paths):
    bridge = _bridge(monkeypatch, True, {})
    result = blueprint_module.get_blueprint_dependencies(paths)
    assert result['success'] is False
    assert result['error'].startswith('INVALID_ARGUMENT')
    assert bridge.calls == []
