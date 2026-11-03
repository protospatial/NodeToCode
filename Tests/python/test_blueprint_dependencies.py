# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.blueprint.get_blueprint_dependencies (card 0319) on plain CPython with the `unreal` stub from
conftest.py. Since card 0342 the wrapper runs get-blueprint-dependencies through InvokeTool: it passes a list of paths
and its three flags as the tool's arguments, takes one path as a string, returns the tool's result, and refuses empty
paths before calling the tool. The wrapper stays "always whole": an index over the tool's inline limit gets its lists
back from the export the tool wrote. The C++ side (the index and its JSON) is NodeToCode.MCP.BlueprintDependencies.
"""

import importlib
import json

import pytest

FOLDER = '/Game/N2CTest/Dependencies'
TOOL = 'get-blueprint-dependencies'


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def test_the_package_exports_it(blueprint_module):
    package = importlib.import_module('nodetocode')
    assert 'get_blueprint_dependencies' in package.__all__
    assert package.get_blueprint_dependencies is blueprint_module.get_blueprint_dependencies


def test_passes_paths_and_flags_and_returns_the_index(blueprint_module, tools):
    data = {'roots': [FOLDER, '/Game/AI/BP_Bot'], 'nodeCount': 2, 'edgeCount': 1,
            'nodes': [{'path': '/Game/AI/BP_Bot.BP_Bot', 'name': 'BP_Bot', 'kind': 'blueprint', 'root': True}],
            'edges': [{'from': '/Game/AI/BP_Bot.BP_Bot', 'to': '/Game/S.S', 'kind': 'variable-type', 'node': 'Data'}],
            'order': ['/Game/S.S', '/Game/AI/BP_Bot.BP_Bot'], 'cycleGroups': [], 'warnings': [], 'inline': True}
    tools.answers[TOOL] = data
    result = blueprint_module.get_blueprint_dependencies([FOLDER, '/Game/AI/BP_Bot'], include_call_graph=False,
                                                         include_referenced=False, to_file=True)
    assert result == {'success': True, 'data': data, 'error': None}
    assert tools.calls == [(TOOL, {'paths': [FOLDER, '/Game/AI/BP_Bot'], 'includeCallGraph': False,
                                   'includeReferenced': False, 'toFile': True})]


def test_one_path_as_a_string_and_the_defaults(blueprint_module, tools):
    tools.answers[TOOL] = {'nodeCount': 8, 'inline': True}
    blueprint_module.get_blueprint_dependencies(FOLDER)
    assert tools.calls == [(TOOL, {'paths': [FOLDER], 'includeCallGraph': True, 'includeReferenced': True,
                                   'toFile': False})]


def test_tool_errors_come_back_with_their_code(blueprint_module, tools):
    tools.answers[TOOL] = ('NOT_FOUND: no Blueprint, Blueprint interface, macro library, user struct or user enum '
                           'under /Game/Nope.')
    result = blueprint_module.get_blueprint_dependencies('/Game/Nope')
    assert result['success'] is False
    assert result['error'].startswith('NOT_FOUND')


def test_an_index_over_the_inline_limit_comes_back_whole(blueprint_module, tools, tmp_path):
    export = {'roots': [FOLDER], 'nodeCount': 1, 'edgeCount': 0,
              'nodes': [{'path': '/Game/A.A', 'name': 'A', 'kind': 'blueprint', 'root': True}], 'edges': [],
              'order': ['/Game/A.A'], 'cycleGroups': [], 'warnings': []}
    saved = tmp_path / 'Dependencies__1.json'
    saved.write_text(json.dumps(export, indent=2), encoding='utf-8')
    tools.answers[TOOL] = {'roots': [FOLDER], 'nodeCount': 1, 'edgeCount': 0, 'warnings': [], 'inline': False,
                           'savedTo': str(saved), 'bytes': saved.stat().st_size, 'cycleGroupCount': 0,
                           'reason': 'The index would be about 30000 tokens, over the 25000 inline limit'}
    data = blueprint_module.get_blueprint_dependencies(FOLDER)['data']
    assert data['inline'] is True
    assert (data['nodes'], data['edges'], data['order'], data['cycleGroups']) == (
        export['nodes'], [], ['/Game/A.A'], [])
    assert 'reason' not in data and 'cycleGroupCount' not in data
    assert data['savedTo'] == str(saved)


def test_an_export_that_cannot_be_read_is_an_error(blueprint_module, tools, tmp_path):
    tools.answers[TOOL] = {'nodeCount': 1, 'inline': False, 'savedTo': str(tmp_path / 'gone.json')}
    result = blueprint_module.get_blueprint_dependencies(FOLDER)
    assert result['success'] is False and result['error'].startswith('EXPORT_FAILED')


@pytest.mark.parametrize('paths', ['', [], ['', '  '], None, [3]])
def test_empty_paths_are_refused_before_the_tool(blueprint_module, tools, paths):
    result = blueprint_module.get_blueprint_dependencies(paths)
    assert result['success'] is False
    assert result['error'].startswith('INVALID_ARGUMENT')
    assert tools.calls == []
