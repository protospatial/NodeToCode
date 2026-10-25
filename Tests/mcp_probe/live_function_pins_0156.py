"""Adding a parameter to a function that is already called and wired rebuilds the call site, live through claude -p
(card 0156).

Stdlib only; run with `python -I`:

    python -I live_function_pins_0156.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor (no provider key: nothing is translated), makes an Actor Blueprint in memory under a fresh
/Game/N2CTest/Pins0156_<time> folder, opens it, creates function MyFunc(Value: int) -> Result: int
(create-blueprint-function), and in the EventGraph adds a call to MyFunc (search-blueprint-nodes, then
add-bp-node-to-active-graph) wired from Event BeginPlay (connect-pins). Then:

1. claude -p (only mcp__nodetocode__* allowed) adds a float input Speed to MyFunc with add-function-input-pin, naming
   the function by blueprintPath and graphGuid.
2. The probe checks the CLI's tool call (no error, actualName Speed, its compile result: success, 0 errors) and the
   event graph's capture (get-focused-blueprint): the MyFunc call node has Value and Speed inputs and Result as output,
   no orphaned pin, and its execute still comes from BeginPlay. compile-blueprint reports no error.

Afterwards the editor is closed and the folder deleted (nothing was saved). Report:
logs/live_function_pins_0156_<label>.json.
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
from live_spawned_nodes_0155 import parse_stream, json_of  # noqa: E402

NAME = 'BP_N2CPins0156'
ZERO_GUID = '00000000000000000000000000000000'

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
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
    'Using only the nodetocode MCP tools, add an input parameter named "Speed" of type "float" to the Blueprint '
    'function MyFunc: call add-function-input-pin once with blueprintPath "%(path)s", graphGuid "%(guid)s", pinName '
    '"Speed" and typeIdentifier "float". Do not call any other tool, and do not save. Reply with only a JSON object '
    '{"actualName": <the actualName from the result>, "compileSuccess": <compile.success from the result>, '
    '"errorCount": <compile.errorCount from the result>}.')


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
        return json_of(text), is_error, text

    def python(self, code):
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0156():\n%s\n    return result\nresult = _n2c_0156()\ndel _n2c_0156\n' % body
        text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]


def call_site(blueprint_json):
    """The MyFunc call node of the captured event graph: its pins, orphans, and whether BeginPlay runs it"""
    def nid(x):
        return x.get('id') or (x.get('ids') or {}).get('short')
    for graph in (blueprint_json.get('n2cjson') or blueprint_json).get('graphs') or []:
        nodes = graph.get('nodes') or []
        calls = [n for n in nodes if n.get('class') == 'K2Node_CallFunction' and n.get('member_name') == 'MyFunc']
        if not calls:
            continue
        call = calls[0]
        events = [n for n in nodes if n.get('class') == 'K2Node_Event']
        inputs = {p.get('name'): p for p in call.get('input_pins') or []}
        execute = (inputs.get('execute') or {})
        return {
            'graph': graph.get('name'),
            'inputs': [p.get('name') for p in call.get('input_pins') or []],
            'outputs': [p.get('name') for p in call.get('output_pins') or []],
            'speed_type': (inputs.get('Speed') or {}).get('type'),
            'exec_from_beginplay': any(e.endswith('->%s.%s' % (nid(call), nid(execute)))
                                       and any(e.startswith(nid(ev) + '.') for ev in events)
                                       for e in (graph.get('flows') or {}).get('execution') or []),
        }
    return {}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27199)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_function_pins_0156_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_function_pins_0156_%s.json' % args.label)
    cli_out_path = os.path.join(log_dir, 'live_function_pins_0156_%s_stream.jsonl' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_function_pins_0156_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Pins0156_%s' % time.strftime('%H%M%S')
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
            checks['setup: Blueprint made'] = data.get('created') is True
            text, err = p.tool('open-blueprint-asset', {'asset_path': path, 'focus_graph': 'EventGraph'})
            checks['setup: opened'] = not err
            body, err, text = p.json_tool('create-blueprint-function', {
                'functionName': 'MyFunc', 'blueprintPath': path,
                'parameters': [{'name': 'Value', 'type': 'int'}, {'name': 'Result', 'type': 'int', 'direction': 'output'}]})
            guid = body.get('functionGuid', '')
            checks['setup: MyFunc(Value) -> Result'] = not err and bool(guid)
            text, err = p.tool('open-blueprint-asset', {'asset_path': path, 'focus_graph': 'EventGraph'})

            body, err, text = p.json_tool('search-blueprint-nodes', {'searchTerm': 'MyFunc', 'maxResults': 20})
            action = next((n.get('spawnMetadata') for n in body.get('nodes') or []
                           if 'MyFunc' in ((n.get('spawnMetadata') or {}).get('actionIdentifier') or '')), None)
            checks['setup: MyFunc found in the action menu'] = action is not None
            call_guid = ''
            if action:
                body, err, text = p.json_tool('add-bp-node-to-active-graph', {
                    'nodeName': action.get('nodeName', 'MyFunc'), 'actionIdentifier': action['actionIdentifier'],
                    'location': {'x': 300, 'y': 0}})
                call_guid = body.get('nodeGuid') or body.get('nodeId') or ''
                report['add_call'] = text[:400]
            checks['setup: call node added'] = bool(call_guid)
            body, err, text = p.json_tool('find-nodes-in-graph', {'searchTerms': ['BeginPlay']})
            begin = next((n for n in body.get('nodes') or [] if n.get('class') == 'K2Node_Event'), {})
            begin_guid = (begin.get('ids') or {}).get('guid', '')
            body, err, text = p.json_tool('connect-pins', {'connections': [{
                'from': {'nodeGuid': begin_guid, 'pinGuid': ZERO_GUID, 'pinName': 'then'},
                'to': {'nodeGuid': call_guid, 'pinGuid': ZERO_GUID, 'pinName': 'execute'}}]})
            checks['setup: BeginPlay -> MyFunc wired'] = ((body.get('summary') or {}).get('succeeded') == 1)
            body, err, text = p.json_tool('get-focused-blueprint', {})
            report['before'] = call_site(body)

            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            cmd = [shutil.which('claude') or 'claude', '-p', CLI_PROMPT % {'path': path, 'guid': guid},
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
            report['cli_calls'] = [{'tool': c['tool'], 'input': c['input'], 'result': c['result'][:1500]} for c in sequence]
            report['cli_final'] = (final or {}).get('result')
            adds = [c for c in sequence if c['tool'] == 'add-function-input-pin']
            checks['cli: add-function-input-pin called'] = len(adds) >= 1
            result = json_of(adds[-1]['result']) if adds else {}
            compile_json = result.get('compile') or {}
            report['tool_result'] = {k: result.get(k) for k in ('success', 'actualName', 'functionName', 'target')}
            report['compile'] = compile_json
            checks['cli: the call succeeded with actualName Speed'] = result.get('success') is True and result.get('actualName') == 'Speed'
            checks['cli: compile success, 0 errors'] = compile_json.get('success') is True and compile_json.get('errorCount') == 0

            body, err, text = p.json_tool('get-focused-blueprint', {})
            after = call_site(body)
            report['after'] = after
            checks['call site: inputs execute, self, Value, Speed'] = [n for n in after.get('inputs') or []
                                                                        if n not in ('execute', 'self')] == ['Value', 'Speed']
            checks['call site: Speed is a real'] = after.get('speed_type') == 'Real'
            checks['call site: outputs then, Result'] = after.get('outputs') == ['then', 'Result']
            checks['call site: still run from BeginPlay'] = after.get('exec_from_beginplay') is True
            body, err, text = p.json_tool('compile-blueprint', {'blueprintPath': path})
            report['compile_blueprint'] = text[:600]
            checks['compile-blueprint: no error'] = not err and '"errorCount":0' in text.replace(' ', '')

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME})
            report['cleanup'] = data or text
            checks['scratch folder deleted'] = data.get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(bool(v) for v in checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'ok': report['ok'], 'checks': checks, 'before': report.get('before'), 'after': report.get('after'),
                      'compile': report.get('compile'), 'cli_final': report.get('cli_final')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
