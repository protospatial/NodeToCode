# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.utils._parse_bridge_result, the one parser of UN2CPythonBridge responses (card 0120), on plain
CPython with the `unreal` stub from conftest.py.

The bridge builds every response with a TJsonWriter, so names and messages with quotes, backslashes, newlines,
apostrophes and non-ASCII text arrive as valid JSON. Before card 0120 it used Printf with
FString::ReplaceCharWithEscapedChar, which writes an apostrophe as \\' (not a JSON escape), so json.loads failed and
the caller got 'Failed to parse bridge response' instead of the result. Review ids: qw-14, python-bridge-1,
imp-python-bridge-1.
"""

import importlib
import json

import pytest

from conftest import UNREAL

# A name that breaks hand-built JSON: an apostrophe, a double quote, a backslash, a newline, a tab, non-ASCII and an
# emoji outside the BMP
AWKWARD = "it's \"q\" C:\\dir\\ line1\nline2\ttab caf\u00e9 \u6f22\u5b57 \U0001F600"

WRAPPER_MODULES = ('blueprint', 'bridge', 'functions', 'graph')


def writer_json(success, data=None, error=None):
    """What the C++ helper sends: a condensed object, non-ASCII written as is (TJsonWriter does not \\u-escape it)"""
    return json.dumps({'success': success, 'data': data, 'error': error}, ensure_ascii=False, separators=(',', ':'))


@pytest.fixture
def utils():
    return importlib.import_module('nodetocode.utils')


class _BridgeStub:
    """Stand-in for unreal.N2CPythonBridge: each method returns the text the test sets for it"""

    def __init__(self, **responses):
        for name, text in responses.items():
            setattr(self, name, staticmethod(lambda *args, _text=text: _text))


def test_utils_holds_the_only_parser_and_the_wrappers_import_it(utils):
    for name in WRAPPER_MODULES:
        module = importlib.import_module('nodetocode.' + name)
        assert module._parse_bridge_result is utils._parse_bridge_result, name
        with open(module.__file__, encoding='utf-8') as f:
            assert 'def _parse_bridge_result' not in f.read(), name


def test_parses_awkward_text_in_data_and_error(utils):
    text = writer_json(False, {'tag': AWKWARD, 'nested': {'name': AWKWARD}, 'count': 3}, "Tag '%s' not found" % AWKWARD)
    result = utils._parse_bridge_result(text)
    assert result['success'] is False
    assert result['error'] == "Tag '%s' not found" % AWKWARD
    assert result['data']['tag'] == AWKWARD
    assert result['data']['nested']['name'] == AWKWARD
    assert result['data']['count'] == 3


def test_parses_a_success_with_null_error(utils):
    result = utils._parse_bridge_result(writer_json(True, {'removed': True}))
    assert result == {'success': True, 'data': {'removed': True}, 'error': None}


def test_the_old_printf_apostrophe_escape_is_not_json(utils):
    # What MakeErrorJson sent before card 0120 for "Tag 'x' not found": \' is not a JSON escape
    old = '{"success": false, "data": null, "error": "Tag \\\'x\\\' not found on graph G"}'
    result = utils._parse_bridge_result(old)
    assert result['success'] is False
    assert result['error'].startswith('Failed to parse bridge response')


@pytest.mark.parametrize('text, message', [
    ('', 'Empty response from bridge function'),
    (None, 'Empty response from bridge function'),
    ('[1, 2]', 'Bridge response is not a JSON object: list'),
    ('"text"', 'Bridge response is not a JSON object: str'),
])
def test_rejects_empty_and_non_object_responses(utils, text, message):
    result = utils._parse_bridge_result(text)
    assert result == {'success': False, 'data': None, 'error': message}


def test_remove_tag_returns_the_bridge_error_with_the_apostrophe(monkeypatch):
    bridge = importlib.import_module('nodetocode.bridge')
    error = "Tag 'it's' not found on graph A1B2C3D4-0000-0000-0000-000000000001"
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', _BridgeStub(remove_tag=writer_json(False, None, error)),
                        raising=False)
    result = bridge.remove_tag('A1B2C3D4-0000-0000-0000-000000000001', "it's")
    assert result == {'success': False, 'data': None, 'error': error}


class _RecordingBridge:
    """Stand-in for unreal.N2CPythonBridge that records the arguments of each call"""

    def __init__(self, text):
        self.calls = []
        for name in ('remove_tag', 'list_tags'):
            setattr(self, name, self._recorder(name, text))

    def _recorder(self, name, text):
        def call(*args):
            self.calls.append((name, args))
            return text
        return call


def test_remove_and_list_tags_pass_the_blueprint_path_to_the_bridge(monkeypatch):
    # Card 0498: a duplicated Blueprint keeps its graph GUIDs, so the bridge names a graph by Blueprint and GUID
    bridge = importlib.import_module('nodetocode.bridge')
    stub = _RecordingBridge(writer_json(True, {}))
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', stub, raising=False)
    path = '/Game/BP_Copy.BP_Copy'
    assert bridge.remove_tag(' A1B2C3D4-0000-0000-0000-000000000001 ', 'gameplay', ' %s ' % path)['success']
    assert bridge.list_tags(tag='gameplay', blueprint_path=path)['success']
    assert bridge.remove_tag('A1B2C3D4-0000-0000-0000-000000000001', 'gameplay')['success']
    assert stub.calls == [
        ('remove_tag', ('A1B2C3D4-0000-0000-0000-000000000001', 'gameplay', path)),
        ('list_tags', ('', 'gameplay', path)),
        ('remove_tag', ('A1B2C3D4-0000-0000-0000-000000000001', 'gameplay', '')),
    ]


def test_create_comment_node_reports_success_for_an_apostrophe_comment(monkeypatch):
    graph = importlib.import_module('nodetocode.graph')
    data = {'commentGuid': 'G', 'commentText': AWKWARD, 'includedNodeCount': 1,
            'position': {'x': 0, 'y': 0}, 'size': {'width': 250, 'height': 150}}
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', _BridgeStub(create_comment_node=writer_json(True, data)),
                        raising=False)
    result = graph.create_comment_node(['A1B2C3D4-0000-0000-0000-000000000002'], comment_text=AWKWARD)
    assert result['success'] is True, result
    assert result['data']['commentText'] == AWKWARD
