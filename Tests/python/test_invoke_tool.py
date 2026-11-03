# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.invoke_tool and the wrappers that run MCP tools through UN2CPythonBridge::InvokeTool (card
0342), on plain CPython with the `unreal` stub from conftest.py (ToolStub stands in for InvokeTool).

The bridge's own copies of 17 operations drifted from the MCP tools (review later-1, imp-python-bridge-2); the
nodetocode functions now run the tools, through one table (utils.TOOL_NAMES), and keep the keys the 1.x copies
returned for one release (LegacyKeys: reading one logs a deprecation warning once). The parity of the tool results
with MCP's is checked in the editor (NodeToCode.Python.ToolParity).
"""

import importlib
import json
import os

import pytest

from conftest import REPO_ROOT, UNREAL

utils = importlib.import_module('nodetocode.utils')


def registered_tools():
    with open(os.path.join(REPO_ROOT, 'Context', 'mcp-server', 'tools.generated.json'), encoding='utf-8') as f:
        return {tool['name'] for tool in json.load(f)['tools']}


def test_every_routed_function_names_a_registered_tool():
    assert set(utils.TOOL_NAMES.values()) <= registered_tools()
    assert not set(utils.TOOL_NAMES) & set(utils.BRIDGE_ONLY)
    nodetocode = importlib.import_module('nodetocode')
    for function in list(utils.TOOL_NAMES) + list(utils.BRIDGE_ONLY):
        assert function in nodetocode.__all__, function


def test_invoke_tool_passes_the_arguments_and_returns_the_envelope(tools):
    tools.answers['list-blueprint-functions'] = {'functions': [{'name': 'Open'}]}
    nodetocode = importlib.import_module('nodetocode')
    result = nodetocode.invoke_tool(' list-blueprint-functions ', {'blueprintPath': "/Game/BP_It's"})
    assert result == {'success': True, 'data': {'functions': [{'name': 'Open'}]}, 'error': None}
    assert tools.calls == [('list-blueprint-functions', {'blueprintPath': "/Game/BP_It's"})]


def test_invoke_tool_returns_the_tools_error_and_code(monkeypatch):
    class Bridge:
        @staticmethod
        def invoke_tool(name, arguments):
            return json.dumps({'success': False, 'data': None, 'error': 'INVALID_ARGUMENT: Missing required '
                               'parameter: connections', 'code': 'INVALID_ARGUMENT', 'field': 'connections'})
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', Bridge, raising=False)
    result = utils.invoke_tool('connect-pins')
    assert result['success'] is False
    assert result['error'] == 'INVALID_ARGUMENT: Missing required parameter: connections'
    assert result['code'] == 'INVALID_ARGUMENT' and result['field'] == 'connections'


def test_invoke_tool_refuses_what_is_not_json_before_calling(tools):
    assert utils.invoke_tool('')['success'] is False
    assert 'not JSON' in utils.invoke_tool('connect-pins', {'x': object()})['error']
    assert tools.calls == []


def test_a_text_value_passes_through_unchanged(monkeypatch):
    # InvokeTool sends a tool's value that is not JSON as a string
    class Bridge:
        @staticmethod
        def invoke_tool(name, arguments):
            return json.dumps({'success': True, 'data': "plain text, it's not JSON", 'error': None})
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', Bridge, raising=False)
    assert utils.invoke_tool('read-file', {'path': 'a.txt'})['data'] == "plain text, it's not JSON"
    # and a wrapper given a text value returns it as it is, without 1.x keys
    graph = importlib.import_module('nodetocode.graph')
    assert graph.delete_nodes(['N'])['data'] == "plain text, it's not JSON"


# One call per routed wrapper: (module, wrapper, args)
WRAPPERS = [
    ('graph', 'search_blueprint_nodes', ('Print String',)),
    ('graph', 'add_node_to_graph', ('Print String', 'id')),
    ('graph', 'connect_pins', ([{'from': {'nodeGuid': 'A', 'pinGuid': 'B'}, 'to': {'nodeGuid': 'C', 'pinGuid': 'D'}}],)),
    ('graph', 'set_pin_value', ('N', 'P', "it's")),
    ('graph', 'delete_nodes', (['N'],)),
    ('graph', 'find_nodes_in_graph', (['Print'],)),
    ('graph', 'create_comment_node', (['N'],)),
    ('functions', 'add_function_input_pin', ('Speed', 'float')),
    ('functions', 'add_function_return_pin', ('Speed', 'float')),
    ('functions', 'remove_function_entry_pin', ('Speed',)),
    ('functions', 'remove_function_return_pin', ('Speed',)),
    ('blueprint', 'compile_blueprint', ()),
    ('blueprint', 'save_blueprint', ()),
    ('bridge', 'tag_graph', ('Movement',)),
    ('bridge', 'open_blueprint', ('/Game/BP',)),
    ('bridge', 'open_blueprint_function', ('Open',)),
    ('blueprint', 'get_blueprint_dependencies', ('/Game/AI',)),
]


def test_the_table_and_the_wrappers_agree():
    assert sorted(w[1] for w in WRAPPERS) == sorted(utils.TOOL_NAMES)


@pytest.mark.parametrize('module,wrapper,args', WRAPPERS, ids=[w[1] for w in WRAPPERS])
def test_each_wrapper_runs_its_tool_and_no_bridge_copy(tools, module, wrapper, args):
    # ToolStub has invoke_tool only: a wrapper that still called its own bridge function would fail here
    result = getattr(importlib.import_module('nodetocode.' + module), wrapper)(*args)
    assert result['success'] is True, result
    assert [name for name, _ in tools.calls] == [utils.TOOL_NAMES[wrapper]]


def test_legacy_keys_are_added_and_warn_once(tools):
    graph = importlib.import_module('nodetocode.graph')
    tools.answers['find-nodes-in-graph'] = {
        'metadata': {'totalFound': 1},
        'nodes': [{'ids': {'short': 'N1', 'guid': '271A8452-4F5F-8709-94F6-1CBF935EA768'}, 'type': 'FunctionEntry',
                   'class': 'K2Node_FunctionEntry', 'title': 'OpenDoor',
                   'output_pins': [{'ids': {'short': 'P1', 'guid': '15288D7D-44AD-951E-0773-0689AE7A5E48',
                                            'name': 'then'}, 'name': 'then'}],
                   'input_pins': [{'ids': {'short': 'P2', 'guid': '25288D7D-44AD-951E-0773-0689AE7A5E48',
                                           'name': 'InString'}, 'name': 'In String', 'type': 'String'}]}]}
    UNREAL.logs.clear()
    utils._warned_legacy.clear()
    node = graph.find_nodes_in_graph(['OpenDoor'])['data']['nodes'][0]
    # The tool's own keys, untouched
    assert node['ids']['guid'] == '271A8452-4F5F-8709-94F6-1CBF935EA768'
    # The 1.x keys, as the bridge wrote them
    assert node['nodeGuid'] == '271A84524F5F870994F61CBF935EA768'
    assert node['nodeName'] == 'OpenDoor' and node['nodeClass'] == 'K2Node_FunctionEntry'
    assert node['outputPins'][0]['pinName'] == 'then'
    assert node['outputPins'][0]['pinGuid'] == '15288D7D44AD951E07730689AE7A5E48'
    pin = node['inputPins'][0]
    assert (pin['pinName'], pin['displayName'], pin['type']) == ('InString', 'In String', 'String')  # tool's key wins
    assert node['outputPins'][0]['type'] == 'exec'
    node.get('nodeGuid')
    warnings = [m for m in UNREAL.messages('warning') if "'nodeGuid' is deprecated" in m]
    assert len(warnings) == 1 and "ids['guid']" in warnings[0]
    assert not [m for m in UNREAL.messages('warning') if "'ids'" in m]
    # json.dumps and iteration see every key
    assert 'nodeGuid' in json.loads(json.dumps(node))


def test_connect_and_search_keep_their_1x_keys(tools):
    graph = importlib.import_module('nodetocode.graph')
    tools.answers['connect-pins'] = {
        'succeeded': [{'from': {'nodeGuid': 'A', 'pinGuid': 'B'}, 'to': {'nodeGuid': 'C', 'pinGuid': 'D'},
                       'index': 0, 'response': 'MAKE'}], 'failed': [],
        'summary': {'totalRequested': 1, 'succeeded': 1, 'failed': 0}}
    done = graph.connect_pins([{'from': {'nodeGuid': 'A', 'pinGuid': 'B'}, 'to': {'nodeGuid': 'C', 'pinGuid': 'D'}}],
                              break_existing_links=True)['data']['succeeded'][0]
    assert (done['fromNode'], done['fromPin'], done['toNode'], done['toPin']) == ('A', 'B', 'C', 'D')
    assert tools.calls[-1][1]['options'] == {'breakExistingLinks': True}

    tools.answers['search-blueprint-nodes'] = {'nodes': [{'spawnMetadata': {
        'actionIdentifier': 'X', 'nodeName': 'Print String', 'category': '|Development'}}]}
    data = graph.search_blueprint_nodes('Print String', category='Development')['data']
    assert data['count'] == 1
    assert (data['nodes'][0]['menuName'], data['nodes'][0]['category']) == ('Print String', '|Development')
    assert tools.calls[-1][1]['category'] == 'Development'


def test_tag_with_an_apostrophe_and_remove_a_missing_tag(tools, monkeypatch):
    # The card's tag case on the stub; NodeToCode.Python.ToolParity runs it in the editor
    bridge = importlib.import_module('nodetocode.bridge')
    tools.answers['tag-blueprint-graph'] = lambda arguments: {
        'success': True, 'taggedGraph': {'tag': arguments['tag'], 'category': arguments['category'],
                                         'description': arguments['description'],
                                         'graphGuid': '08B91F5F-4294-3CC3-F9CC-309BA07D9813', 'graphName': 'Open',
                                         'blueprintPath': '/Game/BP.BP'},
        'target': {'blueprintName': 'BP'}}
    tagged = bridge.tag_graph('Input', description="Player's input")
    assert json.loads(json.dumps(tagged))['data']['taggedGraph']['description'] == "Player's input"
    assert tagged['data']['graph_guid'] == '08B91F5F42943CC3F9CC309BA07D9813'

    error = "Tag 'Nope' not found on graph /Game/BP.BP:08B91F5F42943CC3F9CC309BA07D9813"
    tools.remove_tag = lambda guid, tag, path: json.dumps({'success': False, 'data': None, 'error': error})
    removed = bridge.remove_tag('08B91F5F42943CC3F9CC309BA07D9813', 'Nope', '/Game/BP.BP')
    assert removed == {'success': False, 'data': None, 'error': error}
