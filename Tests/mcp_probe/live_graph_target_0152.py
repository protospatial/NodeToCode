"""Explicit graph targets and stable function IDs, live through claude -p (card 0152).

Stdlib only; run with `python -I`:

    python -I live_graph_target_0152.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint in memory under a fresh /Game/N2CTest/Target0152_<time>
folder, without opening it. Then:

1. claude -p, with only mcp__nodetocode__* allowed, creates a function in that Blueprint by blueprintPath
   (create-blueprint-function) and answers with the functionGuid it got.
2. The probe renames the function (BlueprintEditorLibrary.rename_graph, through run-python) and closes every editor of
   the Blueprint, so nothing is focused and the old name is gone.
3. The same claude session (--resume), told only that the function was renamed, keeps editing it by the functionGuid
   it got first: add-function-input-pin, search-blueprint-nodes, add-bp-node-to-active-graph, find-nodes-in-graph and
   connect-pins, each with blueprintPath and graphGuid.

Checks: every call of step 3 passes the step-1 ID as graphGuid and succeeds; their results' target names the renamed
function (resolvedBy explicit); list-blueprint-functions afterwards lists the renamed function with the same guid and
the new input; the renamed graph holds a Print String. Afterwards the folder is deleted (nothing was saved).
Report: logs/live_graph_target_0152_<label>.json.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from live_env_translation import call_tool, tool_text  # noqa: E402

NAME = 'BP_N2CTarget0152'
FUNCTION = 'N2CTargetFn'
RENAMED = 'N2CTargetFnRenamed'

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
'''

RENAME_PY = r'''
import unreal
bp = unreal.load_asset("%(path)s")
graph = unreal.BlueprintEditorLibrary.find_graph(bp, "%(old)s")
result = {"found": graph is not None}
if graph:
    unreal.BlueprintEditorLibrary.rename_graph(graph, "%(new)s")
    result["renamed"] = graph.get_name()
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result["old_still_there"] = unreal.BlueprintEditorLibrary.find_graph(bp, "%(old)s") is not None
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
bp = unreal.load_asset(folder + "/%(name)s")
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

PROMPT_1 = (
    'Using only the nodetocode MCP tools (not run-python), create a function named %(fn)s in the Blueprint at '
    'blueprintPath "%(path)s" with create-blueprint-function (pass blueprintPath; no parameters). Do nothing else. '
    'Reply with only a JSON object {"functionGuid": <the functionGuid from the result>}.')

PROMPT_2 = (
    'The user has since renamed that function (you are not told its new name) and closed the Blueprint editor, so '
    'nothing is focused. Keep editing the same function by the functionGuid you got, using only the nodetocode MCP '
    'tools (not run-python), and pass blueprintPath "%(path)s" and graphGuid=<that functionGuid> to every call: '
    '1) add-function-input-pin: an input named Count of type int; '
    '2) search-blueprint-nodes for "print string", then add-bp-node-to-active-graph with the Print String result\'s '
    'spawnMetadata.actionIdentifier and spawnMetadata.nodeName; '
    '3) find-nodes-in-graph with searchTerms ["FunctionEntry"] to get the entry node GUID; '
    '4) ONE connect-pins call wiring the entry node\'s "then" pin to the Print String\'s "execute" pin (use pin names; '
    'pinGuid may be any value). '
    'Reply with only a JSON object {"graphName": <target.graphName from the connect-pins result>, '
    '"functionGuid": <the functionGuid you used>}.')


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
        wrapped = 'def _n2c_0152():\n%s\n    return result\nresult = _n2c_0152()\ndel _n2c_0152\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]


def run_cli(prompt, mcp_config_path, model, timeout, out_path, resume=None):
    cmd = [shutil.which('claude') or 'claude', '-p', prompt, '--mcp-config', mcp_config_path, '--strict-mcp-config',
           '--allowedTools', 'mcp__nodetocode__*', '--output-format', 'stream-json', '--verbose', '--model', model]
    if resume:
        cmd += ['--resume', resume]
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
        out, code = proc.stdout.decode('utf-8', 'replace'), proc.returncode
    except subprocess.TimeoutExpired as e:
        out, code = (e.stdout or b'').decode('utf-8', 'replace'), 'timeout'
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(out)
    calls, results, final, session = [], {}, None, None
    for line in out.splitlines():
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        session = msg.get('session_id') or session
        if msg.get('type') == 'result':
            final = msg
        for block in (msg.get('message') or {}).get('content') or []:
            if not isinstance(block, dict):
                continue
            if block.get('type') == 'tool_use':
                calls.append(block)
            elif block.get('type') == 'tool_result':
                c = block.get('content')
                results[block.get('tool_use_id')] = (c if isinstance(c, str) else ''.join(
                    x.get('text', '') for x in (c or []) if isinstance(x, dict)), bool(block.get('is_error')))
    seq = []
    for call in calls:
        text, is_error = results.get(call.get('id'), ('', True))
        try:
            body = json.loads(text)
        except ValueError:
            body = None
        seq.append({'tool': call.get('name', '').replace('mcp__nodetocode__', ''), 'input': call.get('input') or {},
                    'text': text, 'error': is_error, 'body': body if isinstance(body, dict) else None})
    answer = None
    try:
        raw = (final or {}).get('result', '').strip()
        raw = raw[raw.find('{'):raw.rfind('}') + 1]
        answer = json.loads(raw)
    except ValueError:
        pass
    return {'exit': code, 'seconds': round(time.time() - t0, 1), 'session': session, 'calls': seq, 'answer': answer,
            'final': (final or {}).get('result')}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27197)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_graph_target_0152_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_graph_target_0152_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_graph_target_0152_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Target0152_%s' % time.strftime('%H%M%S')
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
            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)

            # 1. The agent creates the function by blueprintPath and keeps its ID
            first = run_cli(PROMPT_1 % {'fn': FUNCTION, 'path': path}, mcp_config_path, args.cli_model, args.timeout,
                            os.path.join(log_dir, 'live_graph_target_0152_%s_step1.jsonl' % args.label))
            created = [c for c in first['calls'] if c['tool'] == 'create-blueprint-function']
            first_id = (created[-1]['body'] or {}).get('functionGuid') if created and created[-1]['body'] else None
            report['step1'] = {'calls': [(c['tool'], c['input'], c['error']) for c in first['calls']],
                               'answer': first['answer'], 'functionGuid': first_id, 'session': first['session'],
                               'exit': first['exit'], 'seconds': first['seconds'],
                               'target': ((created[-1]['body'] or {}).get('target') if created else None)}
            checks['step1: the agent created the function by blueprintPath'] = bool(created) and not created[-1]['error'] \
                and created[-1]['input'].get('blueprintPath') == path
            checks['step1: its answer is the functionGuid of the result'] = bool(first_id) and \
                isinstance(first['answer'], dict) and first['answer'].get('functionGuid') == first_id

            # 2. Rename it and close its editor
            data, text = p.python(RENAME_PY % {'path': path, 'old': FUNCTION, 'new': RENAMED})
            report['rename'] = data or text
            checks['renamed, old name gone'] = data.get('renamed') == RENAMED and data.get('old_still_there') is False

            # 3. The same session keeps editing it by its first ID
            second = run_cli(PROMPT_2 % {'path': path}, mcp_config_path, args.cli_model, args.timeout,
                             os.path.join(log_dir, 'live_graph_target_0152_%s_step2.jsonl' % args.label),
                             resume=first['session'])
            report['step2'] = {'calls': [(c['tool'], {k: v for k, v in c['input'].items()
                                                      if k in ('blueprintPath', 'graphGuid', 'graphName', 'searchTerm',
                                                               'pinName', 'searchTerms')},
                                          c['error'], ((c['body'] or {}).get('target') or {}).get('graphName'))
                                         for c in second['calls']],
                               'answer': second['answer'], 'exit': second['exit'], 'seconds': second['seconds'],
                               'final': second['final']}
            edits = [c for c in second['calls'] if c['tool'] in ('add-function-input-pin', 'search-blueprint-nodes',
                                                                   'add-bp-node-to-active-graph', 'find-nodes-in-graph',
                                                                   'connect-pins')]
            checks['step2: all five tools called'] = {c['tool'] for c in edits} >= {
                'add-function-input-pin', 'search-blueprint-nodes', 'add-bp-node-to-active-graph', 'find-nodes-in-graph',
                'connect-pins'}
            checks['step2: every call passed the first ID as graphGuid'] = bool(edits) and all(
                (c['input'].get('graphGuid') or '').replace('-', '').upper() == (first_id or '-').upper()
                and c['input'].get('blueprintPath') == path for c in edits)
            checks['step2: no call failed'] = bool(second['calls']) and not any(c['error'] for c in second['calls'])
            targets = [((c['body'] or {}).get('target') or {}) for c in edits]
            checks['step2: every result targets the renamed function, explicitly'] = bool(targets) and all(
                t.get('graphName') == RENAMED and t.get('resolvedBy') == 'explicit' for t in targets)
            connects = [c for c in edits if c['tool'] == 'connect-pins']
            checks['step2: connect-pins made the link'] = bool(connects) and \
                ((connects[-1]['body'] or {}).get('summary') or {}).get('succeeded') == 1
            checks['step2: the agent reports the renamed function'] = isinstance(second['answer'], dict) and \
                second['answer'].get('graphName') == RENAMED

            # The probe's own reads
            status, text, err = p.tool('list-blueprint-functions', {'blueprintPath': path})
            try:
                functions = json.loads(text).get('functions') or []
            except (ValueError, AttributeError):
                functions = []
            mine = next((f for f in functions if f.get('name') == RENAMED), None)
            report['listed'] = mine
            checks['list: the renamed function keeps the first ID'] = bool(mine) and \
                (mine.get('guid') or '').upper() == (first_id or '-').upper()
            checks['list: it has the Count input'] = bool(mine) and any(
                (i.get('name') == 'Count') for i in (mine.get('inputs') or []))
            status, text, err = p.tool('find-nodes-in-graph', {'blueprintPath': path, 'graphName': RENAMED,
                                                               'searchTerms': ['PrintString']})
            try:
                found = json.loads(text)
                found = found.get('content', found) if isinstance(found, dict) else {}
            except ValueError:
                found = {}
            report['find_print'] = text[:400]
            checks['the renamed graph holds a Print String'] = '"totalFound":1' in text.replace(' ', '') or \
                '"totalFound": 1' in text

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
