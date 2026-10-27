# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Calls UN2CPythonBridge UFUNCTIONs through the editor's `unreal` module, as the nodetocode package does, and checks that
every response is what json.loads reads: one JSON object {success, data, error}. Run by the C++ spec
NodeToCode.Python.BridgeCalls (Source/Private/Tests/MCP/N2CPythonBridgeCalls.spec.cpp, card 0201), which executes this
file in a fresh namespace with N2C_CTX set (a JSON string) and reads the report it writes to N2C_CTX["out"].

Every call runs once with normal arguments, then once more per string argument with an apostrophe put into that
argument alone (the apostrophe class of bug, python-bridge-1: UE's ReplaceCharWithEscapedChar wrote ' as \\', which no
JSON parser reads). A string argument marks where the apostrophe goes with {q}; {q} is empty in the normal call.

Groups (N2C_CTX["group"]):
- "unfocused": every UFUNCTION, with no Blueprint editor focused. Functions that need the focused graph answer with an
  error, which must be JSON too; the rest read the scratch Blueprint by path. The report lists the functions called and
  the bridge's own UFUNCTIONs, so the spec fails when a new UFUNCTION is not called here.
- "focused": the scratch Blueprint is open with its function graph focused (UE 5.8). The normal calls must succeed:
  read, compile, save (only if dirty: the package is never dirty), find, search, add a node, set a pin, connect, comment,
  add and remove function pins, tag, list and remove the tag, delete, open.
- "export_round_trip": read_n2c_json on get_blueprint_n2c_json's document keeps every node (known defect, card 0505).
"""

import json

import unreal

QUOTE = "it's"
BRIDGE = unreal.N2CPythonBridge


def _reject_constant(token):
    raise ValueError(f'non-standard JSON constant {token}')


def strict_loads(text):
    """json.loads, refusing NaN and Infinity (Python accepts them; JSON does not)"""
    return json.loads(text, parse_constant=_reject_constant)


class Report:
    def __init__(self):
        self.calls = []
        self.called = set()

    def call(self, function, args, kind='response', expect=None, label=None):
        """Calls BRIDGE.<function> with args once normally and once per {q} argument with the apostrophe.

        kind 'response' checks the {success, data, error} object, 'json' any JSON object, 'text' a non-empty string.
        expect True or False is the success the normal call must report. Returns the normal call's parsed response.
        """
        self.called.add(function)
        marked = [i for i, a in enumerate(args) if isinstance(a, str) and '{q}' in a]
        first = None
        for variant in [None] + marked:
            actual = [(a.replace('{q}', QUOTE if i == variant else '') if isinstance(a, str) else a)
                      for i, a in enumerate(args)]
            name = f'{label or function}' + ('' if variant is None else f' (apostrophe in argument {variant + 1})')
            parsed = self._one(name, function, actual, kind, expect if variant is None else None)
            if variant is None:
                first = parsed
        return first

    def _one(self, name, function, args, kind, expect):
        record = {'call': name, 'function': function, 'ok': True, 'problem': ''}
        self.calls.append(record)

        def fail(problem):
            record['ok'] = False
            record['problem'] = problem

        try:
            raw = getattr(BRIDGE, function)(*args)
        except Exception as error:  # a wrong signature here is a broken test, reported as a failed call
            fail(f'raised {type(error).__name__}: {error}')
            return None
        if not isinstance(raw, str):
            fail(f'returned {type(raw).__name__}, not str')
            return None
        if kind == 'text':
            if not raw.strip():
                fail('returned empty text')
            return raw
        try:
            parsed = strict_loads(raw)
        except ValueError as error:
            fail(f'json.loads failed: {error}; response: {raw[:300]}')
            return None
        if not isinstance(parsed, dict):
            fail(f'response is {type(parsed).__name__}, not an object: {raw[:300]}')
            return None
        if kind == 'json':
            return parsed
        if not isinstance(parsed.get('success'), bool) or 'data' not in parsed or 'error' not in parsed:
            fail(f'response is not {{success, data, error}}: {raw[:300]}')
            return parsed
        record['success'] = parsed['success']
        error = parsed['error']
        if parsed['success'] and error is not None:
            fail(f'a success carries an error: {error}')
        if not parsed['success'] and not (isinstance(error, str) and error):
            fail(f'a failure has no error text: {raw[:300]}')
        if expect is not None and parsed['success'] != expect:
            fail(f'success is {parsed["success"]}, expected {expect} (error: {error})')
        return parsed

    def check(self, name, condition, problem):
        self.calls.append({'call': name, 'function': '', 'ok': bool(condition), 'problem': '' if condition else problem})


def bridge_functions():
    """The UFUNCTIONs UN2CPythonBridge adds, by their Python names"""
    inherited = set(dir(unreal.BlueprintFunctionLibrary))
    return sorted(n for n in dir(BRIDGE) if not n.startswith('_') and n not in inherited and callable(getattr(BRIDGE, n)))


def data(response):
    return response.get('data') if isinstance(response, dict) and isinstance(response.get('data'), dict) else {}


def unfocused(report, ctx):
    path = ctx['blueprintPath']
    graph_guid = ctx['graphGuid']
    function = ctx['functionName']
    missing = '/Game/N2CTest_NoSuchFolder/BP_N2CMissing{q}'
    guid = 'A1B2C3D4-0000-0000-0000-0000000002{q}01'

    # By path: no editor needed
    read = report.call('get_blueprint_n2c_json', [path + '{q}', function + '{q}', '', -1, False, True], expect=True)
    report.call('get_blueprint_n2c_json', ['{q}' + path, '', graph_guid + '{q}', 1, False, True],
                label='get_blueprint_n2c_json by guid', expect=True)
    report.call('list_blueprint_graphs', [path + '{q}'], expect=True)
    report.call('list_blueprint_graphs', [missing], label='list_blueprint_graphs (missing)', expect=False)
    report.check('get_blueprint_n2c_json returns an n2c_json object', isinstance(data(read).get('n2c_json'), dict),
                 f'data: {str(data(read))[:300]}')
    with open(ctx['v1Document'], encoding='utf-8') as handle:
        v1 = json.load(handle)
    for graph in v1.get('graphs', []):
        graph['name'] = graph.get('name', '') + '{q}'
    v1_read = report.call('read_n2c_json', [json.dumps(v1)], expect=True)
    report.check('read_n2c_json reads the four nodes of the v1 document', data(v1_read).get('nodes') == 4,
                 f'data: {str(data(v1_read))[:300]}')
    report.call('read_n2c_json', ['{"note": "not N2C JSON{q}"}'], label='read_n2c_json (not N2C JSON)', expect=False)

    # Tags, providers, languages, the untrusted-content mark
    report.call('list_tags', ['N2CBridgeCalls{q}', 'Tag{q}', path + '{q}'], expect=True)
    report.call('list_tags', ['', '', ''], label='list_tags (all)', expect=True)
    report.call('remove_tag', [guid, 'Tag{q}', path + '{q}'], expect=False)
    report.call('get_llm_providers', [], expect=True)
    report.call('get_active_provider', [], expect=True)
    report.call('get_translation_targets', [], expect=True)
    marked = report.call('mark_untrusted_content', ['{"note": "Blueprint text{q}"}'], kind='json')
    report.check('mark_untrusted_content keeps the note', isinstance(marked, dict) and 'note' in marked, f'{marked}')
    report.call('mark_untrusted_content', ['Blueprint text{q}'], kind='text', label='mark_untrusted_content (text)')
    report.call('get_untrusted_content_notice', [], kind='text')
    report.call('open_blueprint', [missing, 'EventGraph{q}'], expect=False)

    # Need the focused graph: with none focused each answers with an error, which must be JSON too
    connection = '[{"from":{"nodeGuid":"' + guid + '","pinGuid":"","pinName":"then{q}"},' \
                 '"to":{"nodeGuid":"' + guid + '","pinGuid":"","pinName":"execute{q}"}}]'
    report.call('get_focused_blueprint_json', [], expect=False)
    report.call('compile_focused_blueprint', [], expect=False)
    report.call('save_focused_blueprint', [True], expect=False)
    report.call('tag_focused_graph', ['Tag{q}', 'N2CBridgeCalls{q}', 'Description{q}'], expect=False)
    report.call('open_blueprint_function', [function + '{q}'], expect=False)
    report.call('search_blueprint_nodes', ['Print String{q}', True, 5, '{q}', True], expect=False)
    report.call('add_node_to_graph', ['Print String{q}', 'Action{q}', 0.0, 0.0], expect=False)
    report.call('connect_pins', [connection, False], expect=False)
    report.call('set_pin_value', [guid, guid, 'Value{q}', 'InString{q}'], expect=False)
    report.call('delete_nodes', ['["' + guid + '"]', False, False], expect=False)
    report.call('find_nodes_in_graph', ['["Print{q}"]', 'keyword{q}', False, 50], expect=False)
    report.call('create_comment_node', ['["' + guid + '"]', 'Comment{q}', 0.1, 0.2, 0.3, 18, 'group{q}', 50.0],
                expect=False)
    report.call('add_function_input_pin', ['InPin{q}', 'float{q}', '1.5{q}', False, 'Tooltip{q}'], expect=False)
    report.call('add_function_return_pin', ['OutPin{q}', 'float{q}', 'Tooltip{q}'], expect=False)
    report.call('remove_function_entry_pin', ['InPin{q}'], expect=False)
    report.call('remove_function_return_pin', ['OutPin{q}'], expect=False)


def focused(report, ctx):
    path = ctx['blueprintPath']
    graph_guid = ctx['graphGuid']
    function = ctx['functionName']
    print_guid = ctx['printGuid']
    in_string = ctx['printInStringPinId']
    get_guid = ctx['variableGetGuid']

    focus = report.call('get_focused_blueprint_json', [], expect=True)
    report.check('get_focused_blueprint_json names the focused function', data(focus).get('graph_name') == function,
                 f'data: {str(data(focus))[:300]}')
    report.call('compile_focused_blueprint', [], expect=True)
    report.call('save_focused_blueprint', [True], expect=True)
    report.call('open_blueprint_function', [function + '{q}'], expect=True)
    report.call('find_nodes_in_graph', ['["Print{q}"]', 'keyword{q}', False, 50], expect=True)
    report.call('find_nodes_in_graph', ['["' + get_guid + '{q}"]', 'guid', False, 50], label='find_nodes_in_graph by guid',
                expect=True)

    found = report.call('search_blueprint_nodes', ['Print String{q}', True, 5, '{q}', True], expect=True)
    nodes = data(found).get('nodes') or []
    report.check('search_blueprint_nodes finds Print String', bool(nodes), f'data: {str(data(found))[:300]}')
    if nodes:
        spawn = nodes[0].get('spawnMetadata', {})
        report.call('add_node_to_graph', [spawn.get('nodeName', '') + '{q}', spawn.get('actionIdentifier', '') + '{q}',
                                          800.0, 0.0], expect=True)

    report.call('set_pin_value', [print_guid + '{q}', in_string + '{q}', 'Hello {q}', '{q}'], expect=True)
    connection = '[{"from":{"nodeGuid":"' + get_guid + '","pinGuid":"","pinName":"no such pin{q}"},' \
                 '"to":{"nodeGuid":"' + print_guid + '","pinGuid":"","pinName":"InString{q}"}}]'
    report.call('connect_pins', [connection, False], expect=True)
    report.call('create_comment_node', ['["' + print_guid + '"]', 'Comment {q}', 0.1, 0.2, 0.3, 18, 'group{q}', 50.0],
                expect=True)

    report.call('add_function_input_pin', ['InPin{q}', 'float{q}', '1.5{q}', False, 'Tooltip {q}'], expect=True)
    report.call('add_function_return_pin', ['OutPin{q}', 'float{q}', 'Tooltip {q}'], expect=True)
    report.call('remove_function_entry_pin', ['InPin{q}'], expect=True)
    report.call('remove_function_return_pin', ['OutPin{q}'], expect=True)

    report.call('tag_focused_graph', ['Tag{q}', 'N2CBridgeCalls{q}', 'Description {q}'], expect=True)
    tags = report.call('list_tags', ['N2CBridgeCalls{q}', 'Tag{q}', path + '{q}'], expect=True)
    listed = [t.get('tag') for t in data(tags).get('tags') or []]
    report.check('list_tags lists the new tag', 'Tag' in listed, f'response: {str(tags)[:300]}')
    report.call('remove_tag', [graph_guid + '{q}', 'Tag{q}', path + '{q}'], expect=True)

    report.call('delete_nodes', ['["' + get_guid + '{q}"]', False, False], expect=True)
    report.call('open_blueprint', [path + '{q}', function + '{q}'], expect=True)


def export_round_trip(report, ctx):
    """read_n2c_json on the document get_blueprint_n2c_json returns keeps every node (known defect: the by-path
    export writes node ids as {short, guid} objects, the reader takes only a string id and skips every node)"""
    read = report.call('get_blueprint_n2c_json', [ctx['blueprintPath'], ctx['functionName'], '', -1, False, True],
                       expect=True)
    document = data(read).get('n2c_json')
    if not isinstance(document, dict):
        report.check('get_blueprint_n2c_json returns an n2c_json object', False, f'data: {str(data(read))[:300]}')
        return
    exported = sum(len(graph.get('nodes', [])) for graph in document.get('graphs', []))
    again = report.call('read_n2c_json', [json.dumps(document)], expect=True)
    report.check('read_n2c_json keeps every node of the export', exported > 0 and data(again).get('nodes') == exported,
                 f'exported {exported} nodes, read back {data(again).get("nodes")} (valid: {data(again).get("valid")})')


def main(ctx_text):
    ctx = json.loads(ctx_text)
    report = Report()
    try:
        {'unfocused': unfocused, 'focused': focused, 'export_round_trip': export_round_trip}[ctx['group']](report, ctx)
    except Exception as error:
        import traceback
        report.check('the group runs to its end', False, f'{type(error).__name__}: {error}\n{traceback.format_exc()}')
    out = {'calls': report.calls, 'called': sorted(report.called), 'functions': bridge_functions()}
    with open(ctx['out'], 'w', encoding='utf-8') as handle:
        json.dump(out, handle, ensure_ascii=False)


main(N2C_CTX)  # noqa: F821 -- set by the spec in the namespace it runs this file in
