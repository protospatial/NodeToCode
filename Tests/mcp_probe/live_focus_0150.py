"""The focused Blueprint follows what was opened last, also for a Blueprint that is already open (card 0150).

Stdlib only; run with `python -I`:

    python -I live_focus_0150.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor, makes three Actor Blueprints in memory under a fresh /Game/N2CTest/Focus0150_<time>
folder (each with a function whose name says which Blueprint it is, so a read shows whose graph it got), and then:

1. MCP: opens A, B and C in turn with open-blueprint-asset, then re-opens A and B (already open, the case card 0145
   saw put a graph into another Blueprint's editor), then opens C through the Python bridge's open_blueprint. After
   each open, get-focused-blueprint must read that Blueprint, get-open-blueprint-editors must mark it (and only it)
   isFocused, and every other open editor must still show its own graph.
2. claude -p (unless --no-cli), with only mcp__nodetocode__* allowed, asked to open A then B and read each with
   get-focused-blueprint: each read must be the Blueprint opened just before it.

Afterwards the editors are closed and the folder is deleted (nothing was saved). Report: logs/live_focus_0150_<label>.json.
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

NAMES = ('BP_N2CFocusA', 'BP_N2CFocusB', 'BP_N2CFocusC')

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
result = {}
tools = unreal.AssetToolsHelpers.get_asset_tools()
for name in %(names)r:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    bp = tools.create_asset(name, folder, unreal.Blueprint, factory)
    ok = bp is not None
    if ok:
        # A function named after the Blueprint, so the graphs a read returns say whose they are
        try:
            unreal.BlueprintEditorLibrary.add_function_graph(bp, "Only_" + name)
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        except Exception as error:
            result["function_" + name] = str(error)
    result[name] = ok
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
result = {"closed": [], "deleted": False}
for name in %(names)r:
    bp = unreal.load_asset(folder + "/" + name)
    if bp:
        subsystem.close_all_editors_for_asset(bp)
        result["closed"].append(name)
result["deleted"] = unreal.EditorAssetLibrary.delete_directory(folder)
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

BRIDGE_OPEN_PY = r'''
import json, unreal
result = json.loads(unreal.N2CPythonBridge.open_blueprint("%(path)s", "EventGraph"))
'''

CLI_PROMPT = ('Using only the nodetocode MCP tools: (1) call open-blueprint-asset with asset_path "%s", then call '
              'get-focused-blueprint and note the Blueprint name in its metadata; (2) call open-blueprint-asset with '
              'asset_path "%s", then call get-focused-blueprint again and note the name. Do nothing else. Reply with '
              'only a JSON object {"first": "<name from the first read>", "second": "<name from the second read>"}.')


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
        """run-python's `result` dict ({} when it failed) and the raw text. The code runs inside a function, so no
        module-level variable in the editor's shared __main__ keeps an asset loaded (and undeletable)."""
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0150():\n%s\n    return result\nresult = _n2c_0150()\ndel _n2c_0150\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]


def focused_name(text):
    try:
        return (json.loads(text).get('metadata') or {}).get('name')
    except (ValueError, AttributeError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27195)
    ap.add_argument('--no-cli', action='store_true', help='skip the claude -p step')
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_focus_0150_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_focus_0150_%s.json' % args.label)
    cli_out_path = os.path.join(log_dir, 'live_focus_0150_%s_stream.jsonl' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_focus_0150_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Focus0150_%s' % time.strftime('%H%M%S')
    paths = {n: '%s/%s.%s' % (folder, n, n) for n in NAMES}
    report = {'label': args.label, 'port': port, 'folder': folder, 'steps': []}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'names': NAMES})
            report['setup'] = data or text
            checks['setup'] = all(data.get(n) is True for n in NAMES)

            def after_open(step, expected):
                status, focused, f_err = p.tool('get-focused-blueprint', {})
                status, editors_text, e_err = p.tool('get-open-blueprint-editors', {})
                try:
                    editors = json.loads(editors_text).get('editors') or []
                except ValueError:
                    editors = []
                marked = [e.get('blueprintName') for e in editors if e.get('isFocused')]
                # Every open editor shows a graph of its own Blueprint (no editor got another Blueprint's graph)
                own = all(not e.get('focusedGraph') or e.get('focusedGraph') in ('EventGraph', 'Only_' + e.get('blueprintName', ''),
                                                                                   'UserConstructionScript')
                          for e in editors)
                entry = {'step': step, 'expected': expected, 'focused': focused_name(focused) or focused[:160], 'marked': marked,
                         'editors': [(e.get('blueprintName'), e.get('focusedGraph')) for e in editors], 'own_graphs': own}
                report['steps'].append(entry)
                checks['%s: get-focused-blueprint reads %s' % (step, expected)] = entry['focused'] == expected
                checks['%s: only %s marked isFocused' % (step, expected)] = marked == [expected]
                checks['%s: every editor shows its own graph' % step] = own

            for name in NAMES:
                # A Blueprint just opened headless has no graph focused until one is asked for
                status, text, err = p.tool('open-blueprint-asset', {'asset_path': paths[name], 'focus_graph': 'EventGraph'})
                checks['open %s' % name] = not err
                after_open('open %s' % name, name)
            for name in ('BP_N2CFocusA', 'BP_N2CFocusB'):
                status, text, err = p.tool('open-blueprint-asset', {'asset_path': paths[name], 'focus_graph': 'Only_' + name})
                checks['re-open %s' % name] = not err
                after_open('re-open %s (already open)' % name, name)
            data, text = p.python(BRIDGE_OPEN_PY % {'path': paths['BP_N2CFocusC']})
            report['bridge_open'] = data or text
            checks['bridge open_blueprint C'] = data.get('success') is True
            after_open('bridge open_blueprint C', 'BP_N2CFocusC')

            if not args.no_cli:
                with open(mcp_config_path, 'w', encoding='utf-8') as f:
                    json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
                cmd = [shutil.which('claude') or 'claude', '-p', CLI_PROMPT % (paths['BP_N2CFocusA'], paths['BP_N2CFocusB']),
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

                # The tool calls in order, each with its result text
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
                sequence = []
                last_opened = None
                reads_ok = []
                for call in calls:
                    name = call.get('name', '').replace('mcp__nodetocode__', '')
                    text = results.get(call.get('id'), '')
                    if name == 'open-blueprint-asset':
                        last_opened = (call.get('input') or {}).get('asset_path', '').rsplit('.', 1)[-1]
                        sequence.append({'tool': name, 'asset': last_opened})
                    elif name == 'get-focused-blueprint':
                        read = focused_name(text)
                        sequence.append({'tool': name, 'read': read, 'after_opening': last_opened})
                        reads_ok.append(read == last_opened)
                    else:
                        sequence.append({'tool': name})
                report['cli_calls'] = sequence
                report['cli_final'] = (final or {}).get('result')
                checks['cli: two get-focused-blueprint reads'] = len(reads_ok) >= 2
                checks['cli: each read is the Blueprint opened just before'] = bool(reads_ok) and all(reads_ok)
                try:
                    answer = json.loads((final or {}).get('result', '').strip().strip('`').replace('json\n', '', 1))
                except ValueError:
                    answer = None
                report['cli_answer'] = answer
                checks['cli: the agent reports A then B'] = isinstance(answer, dict) and answer.get('first') == 'BP_N2CFocusA' \
                    and answer.get('second') == 'BP_N2CFocusB'

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'names': NAMES})
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
