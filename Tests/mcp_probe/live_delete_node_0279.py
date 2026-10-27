"""delete-blueprint-node with preserveConnections bridges the exec chain, live through MCP and claude -p (card 0279).

Stdlib only; run with `python -I`:

    python -I live_delete_node_0279.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint in memory under a fresh /Game/N2CTest/DeleteNode0279_<time>
folder, without opening it. Every call names the graph by blueprintPath and graphName EventGraph:

1. The probe adds three Print String nodes A, B, C (search-blueprint-nodes, add-bp-node-to-active-graph) and links
   A.then -> B.execute -> B.then -> C.execute (connect-pins).
2. claude -p (unless --no-cli), with only mcp__nodetocode__* allowed, is asked to make ONE delete-blueprint-node call
   that deletes B with preserveConnections, and to report what the tool answered. With --no-cli the probe makes it.
3. The probe looks B up again (find-nodes-in-graph by GUID) and compiles the Blueprint (compile-blueprint).

Checks: the delete call succeeds and its preservedConnections lists exactly A.then -> C.execute (exec); the agent
reports that link; B is gone; the Blueprint compiles without errors. Afterwards the folder is deleted (nothing was
saved). Report: logs/live_delete_node_0279_<label>.json.
"""
import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from live_pin_default_0278 import CLEANUP_PY, SETUP_PY, body_of, run_cli  # noqa: E402
from live_env_translation import call_tool, tool_text  # noqa: E402

NAME = 'BP_N2CDeleteNode0279'

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), make exactly ONE delete-blueprint-node call, and no other '
    'call: on the Blueprint at blueprintPath "%(path)s", graphName "EventGraph", delete the node with nodeGuid '
    '"%(node)s" (a Print String in the middle of an exec chain) with preserveConnections true. Do not retry and do not '
    'fix anything. Reply with only a JSON object {"isError": <true if the tool returned an error>, '
    '"preservedConnections": <the preservedConnections array the tool returned for the deleted node, or []>}.')


class Probe:
    def __init__(self, port):
        self.port = port
        self.rid = 100

    def tool(self, name, arguments):
        self.rid += 1
        status, response = call_tool(self.port, name, arguments, self.rid)
        is_error = bool((response.get('result') or {}).get('isError')) if isinstance(response, dict) else True
        return status, tool_text(response), is_error

    def python(self, code):
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0279():\n%s\n    return result\nresult = _n2c_0279()\ndel _n2c_0279\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]


def pin(node, name, side):
    return next((x for x in node.get(side) or [] if x.get('pinName') == name), {})


def pin_ref(node, name, side):
    p = pin(node, name, side)
    return {'nodeGuid': node.get('nodeGuid'), 'pinGuid': p.get('pinGuid'), 'pinName': name}


def bridged_links(body):
    deleted = body.get('deletedNodes') or []
    return (deleted[0].get('preservedConnections') or []) if deleted else []


def is_a_to_c(links, a, c):
    return len(links) == 1 and links[0].get('fromNode') == a and links[0].get('fromPin') == 'then' \
        and links[0].get('toNode') == c and links[0].get('toPin') == 'execute' and links[0].get('type') == 'exec'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27199)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--no-cli', action='store_true', help='the probe makes the delete call instead of claude -p')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_delete_node_0279_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_delete_node_0279_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_delete_node_0279_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/DeleteNode0279_%s' % time.strftime('%H%M%S')
    path = '%s/%s.%s' % (folder, NAME, NAME)
    report = {'label': args.label, 'port': port, 'folder': folder}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME})
            report['setup'] = data or text
            checks['setup'] = data.get('created') is True
            target = {'blueprintPath': path, 'graphName': 'EventGraph'}

            status, text, err = p.tool('search-blueprint-nodes', dict(target, searchTerm='print string'))
            action = next((n.get('spawnMetadata') or {} for n in body_of(text).get('nodes') or []
                           if re.search(r'KismetSystemLibrary:PrintString(?![A-Za-z0-9_])',
                                        (n.get('spawnMetadata') or {}).get('actionIdentifier') or '')), {})
            nodes = []
            for i in range(3):
                status, text, err = p.tool('add-bp-node-to-active-graph', dict(
                    target, nodeName=action.get('nodeName'), actionIdentifier=action.get('actionIdentifier'),
                    location={'x': 300 * i, 'y': 0}))
                nodes.append(body_of(text) if not err else {})
            a, b, c = nodes
            report['nodes'] = {'A': a.get('nodeGuid'), 'B': b.get('nodeGuid'), 'C': c.get('nodeGuid')}
            checks['three Print String nodes added'] = all(report['nodes'].values())

            status, text, err = p.tool('connect-pins', dict(target, connections=[
                {'from': pin_ref(a, 'then', 'outputPins'), 'to': pin_ref(b, 'execute', 'inputPins')},
                {'from': pin_ref(b, 'then', 'outputPins'), 'to': pin_ref(c, 'execute', 'inputPins')}]))
            report['connect'] = text[:400]
            checks['A -> B -> C linked'] = not err and not (body_of(text).get('failed') or [])

            if args.no_cli:
                status, text, err = p.tool('delete-blueprint-node', dict(
                    target, nodeGuids=[b.get('nodeGuid')], preserveConnections=True))
                report['delete'] = text[:800]
                checks['delete succeeds'] = not err
                checks['preservedConnections is A.then -> C.execute'] = is_a_to_c(
                    bridged_links(body_of(text)), a.get('nodeGuid'), c.get('nodeGuid'))
            else:
                with open(mcp_config_path, 'w', encoding='utf-8') as f:
                    json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
                cli = run_cli(PROMPT % {'path': path, 'node': b.get('nodeGuid')}, mcp_config_path, args.cli_model,
                              args.timeout, os.path.join(log_dir, 'live_delete_node_0279_%s_cli.jsonl' % args.label))
                deletes = [x for x in cli['calls'] if x['tool'] == 'delete-blueprint-node']
                report['cli'] = {'calls': [(x['tool'], x['input'], x['error'], x['text'][:800]) for x in cli['calls']],
                                 'answer': cli['answer'], 'exit': cli['exit'], 'seconds': cli['seconds']}
                # Claude Code's own ToolSearch (loading the deferred MCP tool) is not a nodetocode call
                ours = [x for x in cli['calls'] if x['tool'] != 'ToolSearch']
                checks['cli: one delete-blueprint-node call with preserveConnections'] = len(ours) == 1 \
                    and len(deletes) == 1 and deletes[0]['input'].get('preserveConnections') is True
                checks['cli: the tool succeeded and bridged A.then -> C.execute'] = bool(deletes) \
                    and not deletes[0]['error'] and is_a_to_c(bridged_links(body_of(deletes[0]['text'])),
                                                              a.get('nodeGuid'), c.get('nodeGuid'))
                answer = cli['answer'] if isinstance(cli['answer'], dict) else {}
                checks['cli: the agent reports the bridged link'] = answer.get('isError') is False and is_a_to_c(
                    answer.get('preservedConnections') or [], a.get('nodeGuid'), c.get('nodeGuid'))

            status, text, err = p.tool('find-nodes-in-graph', dict(target, searchType='guid',
                                                                   searchTerms=[b.get('nodeGuid')]))
            report['find B after'] = text[:300]
            checks['B is gone'] = not (body_of(text).get('nodes') or [])
            status, text, err = p.tool('compile-blueprint', {'blueprintPath': path})
            report['compile'] = text[:400]
            checks['compiles without errors'] = not err and '"errorCount":0' in text.replace(' ', '')

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME})
            report['cleanup'] = data or text
            checks['scratch folder deleted'] = data.get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
