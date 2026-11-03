# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The nodetocode half of NodeToCode.Python.ToolParity (Source/Private/Tests/MCP/N2CPythonToolParity.spec.cpp, card
0342): every nodetocode function that runs an MCP tool through UN2CPythonBridge::InvokeTool, called in order on the
scratch Blueprint N2C_CTX["blueprintPath"] (function graph N2C_CTX["functionName"], only its entry node). The spec runs
the same steps with the MCP tools on a twin Blueprint and compares each outcome with this report's.

The steps (the spec's list must match): open_blueprint, open_blueprint_function, search_blueprint_nodes,
add_node_to_graph, find_nodes_in_graph, connect_pins, set_pin_value, create_comment_node, add_function_input_pin,
add_function_return_pin, remove_function_entry_pin, remove_function_return_pin, compile_blueprint, save_blueprint,
tag_graph, delete_nodes. Then the card's tag case: a tag described "Player's input", and remove_tag of a tag the
graph does not have; json.loads must read both, and the second must carry the real error text.

Report (written to N2C_CTX["out"]): {"steps": [{"step", "success", "data", "error"}], "tagCase": {...}, "problem"}.
"""

import json
import traceback

import nodetocode as n2c


def plain(result):
    """A wrapper's result as plain JSON values (LegacyKeys and all), with its 1.x keys"""
    return json.loads(json.dumps(result))


def run(ctx):
    path, function = ctx['blueprintPath'], ctx['functionName']
    target = {'blueprint_path': path, 'graph': function}
    steps = []

    def step(name, result):
        result = plain(result)
        steps.append({'step': name, 'success': result.get('success'), 'data': result.get('data'),
                      'error': result.get('error')})
        return result.get('data') if isinstance(result.get('data'), dict) else {}

    step('open_blueprint', n2c.open_blueprint(path, function))
    step('open_blueprint_function', n2c.open_blueprint_function(function))
    found = step('search_blueprint_nodes', n2c.search_blueprint_nodes('Print String', max_results=5, **target))
    meta = next((n.get('spawnMetadata') or {} for n in found.get('nodes') or []
                 if (n.get('spawnMetadata') or {}).get('nodeName') == 'Print String'), {})
    added = step('add_node_to_graph', n2c.add_node_to_graph(meta.get('nodeName', 'Print String'),
                                                            meta.get('actionIdentifier', ''), 400, 0, **target))
    node = added.get('nodeGuid', '')
    pins = {p.get('pinName'): p.get('pinGuid') for p in (added.get('inputPins') or []) + (added.get('outputPins') or [])}
    entry = step('find_nodes_in_graph', n2c.find_nodes_in_graph([function], **target))
    entry_node = (entry.get('nodes') or [{}])[0]
    entry_then = next((p.get('pinGuid') for p in entry_node.get('outputPins') or [] if p.get('pinName') == 'then'), '')
    step('connect_pins', n2c.connect_pins([{'from': {'nodeGuid': entry_node.get('nodeGuid', ''), 'pinGuid': entry_then},
                                            'to': {'nodeGuid': node, 'pinGuid': pins.get('execute', '')}}], **target))
    step('set_pin_value', n2c.set_pin_value(node, pins.get('InString', ''), "it's \"quoted\"", **target))
    step('create_comment_node', n2c.create_comment_node([node], "Player's comment", **target))
    step('add_function_input_pin', n2c.add_function_input_pin('Speed', 'float', '1.5', **target))
    step('add_function_return_pin', n2c.add_function_return_pin('Result', 'int', **target))
    step('remove_function_entry_pin', n2c.remove_function_entry_pin('Speed', **target))
    step('remove_function_return_pin', n2c.remove_function_return_pin('Result', **target))
    step('compile_blueprint', n2c.compile_blueprint(path))
    step('save_blueprint', n2c.save_blueprint(path))
    step('tag_graph', n2c.tag_graph('N2CParity', 'N2CParity', "Player's input", **target))
    step('delete_nodes', n2c.delete_nodes([node], **target))

    # The card's tag case: both results go through json.loads, the second carries the real error text
    tagged = json.loads(json.dumps(n2c.tag_graph('N2CParityInput', 'N2CParity', "Player's input", **target)))
    graph_guid = (tagged.get('data') or {}).get('graph_guid', '')
    missing = json.loads(json.dumps(n2c.remove_tag(graph_guid, 'N2CParityMissing', path)))
    tag_case = {'tagged': tagged, 'missing': missing, 'graphGuid': graph_guid}
    return steps, tag_case


def main(ctx_text):
    ctx = json.loads(ctx_text)
    report = {'steps': [], 'tagCase': {}, 'problem': ''}
    try:
        report['steps'], report['tagCase'] = run(ctx)
    except Exception as error:  # the spec reports it
        report['problem'] = f'{type(error).__name__}: {error}\n{traceback.format_exc()}'
    with open(ctx['out'], 'w', encoding='utf-8') as f:
        json.dump(report, f)


main(N2C_CTX)  # noqa: F821  (set by the spec)
