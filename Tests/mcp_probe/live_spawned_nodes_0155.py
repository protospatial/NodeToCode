"""Variable and comment nodes made through MCP get GUIDs, wire, translate and undo, live through claude -p (card 0155).

Stdlib only; run with `python -I`:

    python -I live_spawned_nodes_0155.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor (ANTHROPIC_API_KEY from the repo's .env in the editor's environment only, never printed),
makes an Actor Blueprint in memory under a fresh /Game/N2CTest/Spawn0155_<time> folder, opens it and adds a float
member Health (create-variable). Then:

1. claude -p (only mcp__nodetocode__* allowed) adds a Get Health node and a Set Health node, finds Event BeginPlay,
   connects BeginPlay then -> Set execute and Get Health -> Set Health, and puts a comment around the two variable
   nodes.
2. The probe checks the CLI's tool calls (no error, nodeId values are GUIDs, the comment's GUID is not zero), finds
   both nodes by their nodeId (find-nodes-in-graph, searchType guid), and checks the capture (get-focused-blueprint):
   the Set node's execute comes from BeginPlay and its Health input from the Get node, with one set of pins each.
3. translate-focused-blueprint with anthropic / claude-haiku-5-5 returns a translation with code.
4. The package is dirty. Three TRANSACTION UNDO (run-python) remove the comment, the links and the Set node, one each;
   the Get node stays.

Afterwards the editor is closed and the folder deleted (nothing was saved); no key value may appear in the editor log
or the report. Report: logs/live_spawned_nodes_0155_<label>.json.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import call_tool, run_tool_call, tool_text  # noqa: E402

NAME = 'BP_N2CSpawn0155'
GUID_RE = re.compile(r'^[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}$')
ZERO = re.compile(r'^[0-]+$')

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
'''

UNDO_PY = r'''
import unreal
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "TRANSACTION UNDO")
result = {"undone": True}
'''

DIRTY_PY = r'''
import unreal
names = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
result = {"dirty": [n for n in names if "%(name)s" in n]}
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
bp = unreal.load_asset(folder + "/%(name)s")
if bp:
    subsystem.close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

CLI_PROMPT = (
    'Using only the nodetocode MCP tools, edit the Blueprint that is open and focused on its EventGraph (do not open '
    'other assets, delete or save anything). Make exactly these calls, in this order: '
    '1) create-get-member-variable-node with variableName "Health" and location {"x": 200, "y": 250}. '
    '2) create-set-member-variable-node with variableName "Health" and location {"x": 450, "y": 0}. '
    '3) find-nodes-in-graph with searchTerms ["BeginPlay"]. '
    '4) one connect-pins call with two connections: from the BeginPlay event node (its ids.guid from step 3) pin '
    '"then" to the Set node (nodeId from step 2) pin "execute"; and from the Get node (nodeId from step 1) pin '
    '"Health" to the Set node pin "Health". Use the pinGuid values from the results, or any GUID with the pinName. '
    '5) create-comment-node with nodeGuids [the Get nodeId, the Set nodeId] and commentText "Health wiring". '
    'Reply with only a JSON object {"get": <Get nodeId>, "set": <Set nodeId>, "comment": <commentNode.guid>}.')


class Probe:
    def __init__(self, port):
        self.port = port
        self.rid = 100

    def tool(self, name, arguments):
        self.rid += 1
        status, response = call_tool(self.port, name, arguments, self.rid)
        is_error = bool((response.get('result') or {}).get('isError')) if isinstance(response, dict) else True
        return tool_text(response), is_error

    def json_tool(self, name, arguments):
        text, is_error = self.tool(name, arguments)
        try:
            return json.loads(text), is_error, text
        except ValueError:
            return {}, True, text

    def python(self, code):
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0155():\n%s\n    return result\nresult = _n2c_0155()\ndel _n2c_0155\n' % body
        text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]

    def found(self, guid):
        body, err, text = self.json_tool('find-nodes-in-graph', {'searchTerms': [guid], 'searchType': 'guid'})
        return len(body.get('nodes') or [])


def parse_stream(out):
    calls, results, final = [], {}, None
    for line in out.splitlines():
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if msg.get('type') == 'result':
            final = msg
        for block in (msg.get('message') or {}).get('content') or []:
            if not isinstance(block, dict):
                continue
            if block.get('type') == 'tool_use':
                calls.append(block)
            elif block.get('type') == 'tool_result':
                c = block.get('content')
                results[block.get('tool_use_id')] = c if isinstance(c, str) else ''.join(
                    x.get('text', '') for x in (c or []) if isinstance(x, dict))
    return [{'tool': c.get('name', '').replace('mcp__nodetocode__', ''), 'input': c.get('input'),
             'result': results.get(c.get('id'), '')} for c in calls], final


def json_of(text):
    try:
        return json.loads(text)
    except ValueError:
        return {}


def capture_check(blueprint_json):
    """Set Health's execute from BeginPlay and its Health input from Get Health, in the event graph's capture"""
    out = {}

    def nid(x):
        return x.get('id') or (x.get('ids') or {}).get('short')
    graphs = (blueprint_json.get('n2cjson') or blueprint_json).get('graphs') or []
    for graph in graphs:
        nodes = graph.get('nodes') or []
        sets = [n for n in nodes if n.get('class') == 'K2Node_VariableSet' and n.get('member_name') == 'Health']
        gets = [n for n in nodes if n.get('class') == 'K2Node_VariableGet' and n.get('member_name') == 'Health']
        events = [n for n in nodes if n.get('class') == 'K2Node_Event']
        if not sets:
            continue
        flows = graph.get('flows') or {}
        s, g = sets[0], (gets[0] if gets else {})
        pin = {p.get('name'): nid(p) for p in (s.get('input_pins') or [])}
        names = [p.get('name') for p in (s.get('input_pins') or []) + (s.get('output_pins') or [])]
        out['set_pins'] = names
        out['set_one_pin_set'] = len(names) == len(set(names))
        out['exec_from_beginplay'] = any(e.endswith('->%s.%s' % (nid(s), pin.get('execute'))) and
                                         any(e.startswith(nid(ev) + '.') for ev in events)
                                         for e in flows.get('execution') or [])
        data = flows.get('data') or {}
        out['data_from_get'] = bool(g) and any(src.startswith(nid(g) + '.') and dst == '%s.%s' % (nid(s), pin.get('Health'))
                                               for src, dst in data.items())
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27198)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()

    keys = load_provider_keys(only=['ANTHROPIC_API_KEY'])
    if 'ANTHROPIC_API_KEY' not in keys:
        print(json.dumps({'ok': False, 'reason': 'ANTHROPIC_API_KEY not in .env'}))
        return 2

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_spawned_nodes_0155_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_spawned_nodes_0155_%s.json' % args.label)
    cli_out_path = os.path.join(log_dir, 'live_spawned_nodes_0155_%s_stream.jsonl' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_spawned_nodes_0155_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Spawn0155_%s' % time.strftime('%H%M%S')
    path = '%s/%s.%s' % (folder, NAME, NAME)
    report = {'label': args.label, 'port': port, 'folder': folder, 'model': args.model}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME})
            checks['setup'] = data.get('created') is True
            text, err = p.tool('open-blueprint-asset', {'asset_path': path, 'focus_graph': 'EventGraph'})
            checks['open'] = not err
            text, err = p.tool('create-variable', {'variableName': 'Health', 'typeIdentifier': 'float'})
            checks['member Health'] = not err

            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            cmd = [shutil.which('claude') or 'claude', '-p', CLI_PROMPT,
                   '--mcp-config', mcp_config_path, '--strict-mcp-config', '--allowedTools', 'mcp__nodetocode__*',
                   '--output-format', 'stream-json', '--verbose', '--model', args.cli_model]
            t0 = time.time()
            try:
                proc = subprocess.run(cmd, capture_output=True, timeout=args.timeout, stdin=subprocess.DEVNULL)
                out = proc.stdout.decode('utf-8', 'replace')
                report['cli_exit'] = proc.returncode
            except subprocess.TimeoutExpired as e:
                out = (e.stdout or b'').decode('utf-8', 'replace')
                report['cli_exit'] = 'timeout'
            report['cli_seconds'] = round(time.time() - t0, 1)
            with open(cli_out_path, 'w', encoding='utf-8') as f:
                f.write(out)

            sequence, final = parse_stream(out)
            report['cli_calls'] = [{'tool': c['tool'], 'input': c['input'], 'result': c['result'][:400]} for c in sequence]
            report['cli_final'] = (final or {}).get('result')
            tools_called = [c['tool'] for c in sequence]
            for name in ('create-get-member-variable-node', 'create-set-member-variable-node', 'connect-pins',
                         'create-comment-node'):
                checks['cli: ' + name] = name in tools_called
            checks['cli: no tool call failed'] = bool(sequence) and not any(
                c['result'].startswith('Error') or '"isError": true' in c['result'] for c in sequence)

            def result_of(tool):
                return next((json_of(c['result']) for c in sequence if c['tool'] == tool), {})
            get_id = result_of('create-get-member-variable-node').get('nodeId', '')
            set_id = result_of('create-set-member-variable-node').get('nodeId', '')
            comment = (result_of('create-comment-node').get('commentNode') or {}).get('guid', '')
            connect = result_of('connect-pins')
            report['ids'] = {'get': get_id, 'set': set_id, 'comment': comment}
            report['connect'] = {k: connect.get(k) for k in ('succeeded', 'failed', 'summary') if k in connect} or connect
            checks['nodeId values are non-zero GUIDs'] = all(GUID_RE.match(x or '') and not ZERO.match(x) for x in (get_id, set_id))
            checks['comment GUID is not zero'] = bool(comment) and not ZERO.match(comment)
            checks['find-nodes-in-graph finds the Get node by nodeId'] = p.found(get_id) == 1
            checks['find-nodes-in-graph finds the Set node by nodeId'] = p.found(set_id) == 1

            body, err, text = p.json_tool('get-focused-blueprint', {})
            cap = capture_check(body)
            report['capture'] = cap
            checks['capture: Set execute from BeginPlay'] = cap.get('exec_from_beginplay') is True
            checks['capture: Set Health from Get Health'] = cap.get('data_from_get') is True
            checks['capture: one set of pins on the Set node'] = cap.get('set_one_pin_set') is True

            t1 = time.time()
            events = run_tool_call(port, 'translate-focused-blueprint',
                                   {'provider': 'anthropic', 'model': args.model, 'language': 'cpp'}, 50,
                                   meta={'progressToken': 'live-0155'}, timeout=300)
            final_events = [d for e, d in (events or []) if e == 'response']
            message = json_of(final_events[-1]) if final_events else {}
            translation = json_of(tool_text(message)) if message else {}
            graphs = translation.get('graphs') or [] if isinstance(translation, dict) else []
            code_chars = sum(len((g.get('code') or {}).get('graphDeclaration', '') or '')
                             + len((g.get('code') or {}).get('graphImplementation', '') or '') for g in graphs if isinstance(g, dict))
            report['translation'] = {'seconds': round(time.time() - t1, 1), 'graphs': len(graphs), 'code_chars': code_chars,
                                     'is_error': bool((message.get('result') or {}).get('isError')) if message else None,
                                     'head': (tool_text(message) or '')[:300] if message else 'no response'}
            checks['translation with code (claude-haiku-5-5)'] = code_chars > 0 and not report['translation']['is_error']

            data, text = p.python(DIRTY_PY % {'name': NAME})
            checks['package dirty'] = bool(data.get('dirty'))

            undo = []
            for _ in range(3):
                p.python(UNDO_PY)
                undo.append({'get': p.found(get_id), 'set': p.found(set_id)})
            body, err, text = p.json_tool('get-focused-blueprint', {})
            report['after_undo'] = {'found': undo, 'capture': capture_check(body)}
            checks['undo 1-2 keep both nodes (comment, then links)'] = undo[0] == {'get': 1, 'set': 1} and undo[1] == {'get': 1, 'set': 1}
            checks['undo 3 removes the Set node, the Get node stays'] = undo[2] == {'get': 1, 'set': 0}

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME})
            report['cleanup'] = data or text
            checks['scratch folder deleted'] = data.get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [log_path, report_path, cli_out_path])
    checks['no key value in the logs or report'] = not leaks
    report['ok'] = bool(checks) and all(bool(v) for v in checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'ok': report['ok'], 'checks': checks, 'ids': report.get('ids'), 'capture': report.get('capture'),
                      'translation': report.get('translation'), 'after_undo': report.get('after_undo')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
