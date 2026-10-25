# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.blueprint.read_n2c_json (card 0445) on plain CPython with the `unreal` stub from conftest.py:
the wrapper hands the text to UN2CPythonBridge.ReadN2CJson unchanged and returns the bridge's answer, warnings and
all, so a later 2.x document's unknown kinds and fields reach the caller. The C++ side (the reader itself) is
NodeToCode.Serializer.ForwardCompat and NodeToCode.Python.Bridge, which read the same fixture.
"""

import importlib
import json
from pathlib import Path

import pytest

from conftest import UNREAL

FIXTURE = (Path(__file__).resolve().parents[2] / 'Source' / 'Private' / 'Tests' / 'Fixtures' / 'Capture'
           / 'ForwardCompat' / 'UnknownKinds_2.1.json')


class _Bridge:
    """Records each read_n2c_json call and answers with the text the test gives"""

    def __init__(self, response):
        self.calls = []
        self._response = response

    def read_n2c_json(self, *args):
        self.calls.append(args)
        return self._response


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def test_the_fixture_is_a_later_2x_document_with_unknown_kinds():
    document = json.loads(FIXTURE.read_text(encoding='utf-8'))
    assert document['version'] == '2.1.0'
    assert document['metadata']['asset_kind'] == 'Material'
    assert document['graphs'][0]['graph_type'] == 'MaterialGraph'
    assert 'extensions' in document


def test_read_n2c_json_passes_the_text_and_returns_the_warnings(blueprint_module, monkeypatch):
    text = FIXTURE.read_text(encoding='utf-8').strip()
    warnings = [{'code': 'UNKNOWN_NODE_TYPE', 'path': 'graphs[0].nodes[0].type', 'value': 'MaterialExpression',
                 'message': 'unknown node type "MaterialExpression" read as Unknown and kept'}]
    data = {'source_version': '2.1.0', 'version': '2.1.0', 'valid': True, 'graphs': 2, 'nodes': 4,
            'data_edges': 2, 'exec_edges': 1, 'n2c_json': json.loads(text), 'warnings': warnings}
    bridge = _Bridge(json.dumps({'success': True, 'data': data, 'error': None}))
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', bridge, raising=False)

    result = blueprint_module.read_n2c_json(text)

    assert bridge.calls == [(text,)]
    assert result['success'] is True
    assert result['data']['warnings'] == warnings
    assert result['data']['n2c_json'] == json.loads(text)
