# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.material.get_material_n2cjson (card 0453) on plain CPython with the `unreal` stub from
conftest.py: the wrapper runs get-material-n2cjson through InvokeTool with materialPath, compact and toFile (depth
only when given), returns the tool's result, passes its error code through, and refuses an empty path before calling
the tool. The C++ side (capture, refusals, the export) is NodeToCode.MCP.MaterialN2CJson.
"""

import importlib

import pytest

TOOL = 'get-material-n2cjson'
PATH = '/Engine/EngineMaterials/WorldGridMaterial'


@pytest.fixture
def material_module():
    return importlib.import_module('nodetocode.material')


def test_the_package_exports_it(material_module):
    package = importlib.import_module('nodetocode')
    assert 'get_material_n2cjson' in package.__all__
    assert package.get_material_n2cjson is material_module.get_material_n2cjson


def test_passes_the_path_and_the_defaults(material_module, tools):
    data = {'version': '2.0.0', 'metadata': {'asset_kind': 'Material'}, 'graphs': [{'nodes': []}],
            'target': {'assetPath': PATH + '.WorldGridMaterial', 'kind': 'material', 'resolvedBy': 'path', 'workingCopy': False}}
    tools.answers[TOOL] = data
    assert material_module.get_material_n2cjson(PATH) == {'success': True, 'data': data, 'error': None}
    assert tools.calls == [(TOOL, {'materialPath': PATH, 'compact': True, 'toFile': False})]


def test_passes_depth_and_flags_when_given(material_module, tools):
    tools.answers[TOOL] = {'path': 'C:/x.json', 'reason': 'toFile'}
    material_module.get_material_n2cjson(PATH, depth=2, compact=False, to_file=True)
    assert tools.calls == [(TOOL, {'materialPath': PATH, 'compact': False, 'toFile': True, 'depth': 2})]


def test_tool_errors_come_back_with_their_code(material_module, tools):
    tools.answers[TOOL] = "UNSUPPORTED_ASSET_TYPE: '/Game/MI_Rock' is a MaterialInstanceConstant"
    result = material_module.get_material_n2cjson('/Game/MI_Rock')
    assert result['success'] is False
    assert result['error'].startswith('UNSUPPORTED_ASSET_TYPE')


@pytest.mark.parametrize('path', ['', '   ', None])
def test_an_empty_path_is_refused_before_the_tool(material_module, tools, path):
    result = material_module.get_material_n2cjson(path)
    assert result['success'] is False
    assert result['error'].startswith('INVALID_ARGUMENT')
    assert tools.calls == []
