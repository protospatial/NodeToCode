"""Member variable defaults are read and written on the class defaults, live through MCP and claude -p (card 0283).

Stdlib only; run with `python -I`:

    python -I live_variable_defaults_0283.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

UE 5.8 only: the variable tools act on the focused Blueprint editor, and closing a headless one is fatal on 5.6/5.7.
It starts a headless editor, makes an Actor Blueprint in memory under a fresh /Game/N2CTest/VarDefaults0283_<time>
folder and opens it. The probe creates Health (int, default 100, as in the card 0171 runs), Speed (float) and Channel
(ECollisionChannel). Then:

1. claude -p (unless --no-cli), with only mcp__nodetocode__* allowed, reads the variables (get-blueprint-member-variables),
   sets Speed to 75, tries Speed "fast", sets Channel to the display name "Camera", and reports what it saw.
2. The probe checks: Health's defaultValue is 100 and Speed's type category is float in the listing (the agent reports
   both too); setting Speed answered oldDefaultValue 0.000000 and newDefaultValue 75.000000; "fast" is an error naming
   the type; Channel reads ECC_Camera. The class default object (run-python, unreal.get_default_object) holds 75 and
   ECC_Camera.
3. Undo through TRANSACTION UNDO twice: Channel goes back to ECC_WorldStatic, then Speed back to 0, on the class
   defaults and in the listing.

With --no-cli the probe makes the agent's calls itself. Afterwards the folder is deleted (nothing was saved).
Report: logs/live_variable_defaults_0283_<label>.json.
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

NAME = 'BP_N2CVarDefaults0283'

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
'''

CDO_PY = r'''
import unreal
bp = unreal.load_asset("%(path)s")
cdo = unreal.get_default_object(bp.generated_class())
result = {"Speed": cdo.get_editor_property("Speed"), "Channel": str(cdo.get_editor_property("Channel")),
          "Health": cdo.get_editor_property("Health")}
'''

UNDO_PY = r'''
import unreal
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "TRANSACTION UNDO")
result = {"undone": True}
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

CLI_PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), work on the Blueprint that is open and focused (do not open '
    'other assets, delete or save anything). Make exactly these calls, in this order, and do not retry or fix '
    'anything: 1) get-blueprint-member-variables. 2) set-member-variable-default-value with variableName "Speed", '
    'defaultValue "75". 3) set-member-variable-default-value with variableName "Speed", defaultValue "fast". '
    '4) set-member-variable-default-value with variableName "Channel", defaultValue "Camera". '
    'Reply with only a JSON object {"healthDefault": <Health\'s defaultValue from call 1>, "speedCategory": <Speed\'s '
    'type.category from call 1>, "speedOld": <oldDefaultValue from call 2>, "speedNew": <newDefaultValue from call 2>, '
    '"fastIsError": <true if call 3 returned an error>, "fastMessage": <call 3\'s error or message>, "channelNew": '
    '<newDefaultValue from call 4>}.')


class Probe:
    def __init__(self, port):
        self.port = port
        self.rid = 100

    def tool(self, name, arguments):
        self.rid += 1
        status, response = call_tool(self.port, name, arguments, self.rid)
        is_error = bool((response.get('result') or {}).get('isError')) if isinstance(response, dict) else True
        return status, tool_text(response), is_error

    def json_tool(self, name, arguments):
        status, text, is_error = self.tool(name, arguments)
        try:
            return json.loads(text), is_error, text
        except ValueError:
            return {}, True, text

    def python(self, code):
        body = '\n'.join('    ' + line for line in code.strip('\n').splitlines())
        wrapped = 'def _n2c_0283():\n%s\n    return result\nresult = _n2c_0283()\ndel _n2c_0283\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]

    def members(self):
        body, err, text = self.json_tool('get-blueprint-member-variables', {})
        return {v.get('name'): v for v in (body.get('variables') or [])}, text


def run_cli(port, mcp_config_path, cli_out_path, model, timeout):
    with open(mcp_config_path, 'w', encoding='utf-8') as f:
        json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
    cmd = [shutil.which('claude') or 'claude', '-p', CLI_PROMPT,
           '--mcp-config', mcp_config_path, '--strict-mcp-config', '--allowedTools', 'mcp__nodetocode__*',
           '--output-format', 'stream-json', '--verbose', '--model', model]
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
        out = proc.stdout.decode('utf-8', 'replace')
        exit_code = proc.returncode
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b'').decode('utf-8', 'replace')
        exit_code = 'timeout'
    with open(cli_out_path, 'w', encoding='utf-8') as f:
        f.write(out)
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
                results[block.get('tool_use_id')] = {
                    'text': c if isinstance(c, str) else ''.join(x.get('text', '') for x in (c or []) if isinstance(x, dict)),
                    'isError': bool(block.get('is_error'))}
    sequence = []
    for call in calls:
        if not call.get('name', '').startswith('mcp__nodetocode__'):
            continue  # the CLI's own tools (ToolSearch)
        res = results.get(call.get('id'), {})
        sequence.append({'tool': call.get('name', '').replace('mcp__nodetocode__', ''), 'input': call.get('input'),
                         'isError': res.get('isError'), 'result': (res.get('text') or '')[:400]})
    answer = None
    text = ((final or {}).get('result') or '').strip()
    start, end = text.find('{'), text.rfind('}')
    if start >= 0 and end > start:
        try:
            answer = json.loads(text[start:end + 1])
        except ValueError:
            answer = None
    return {'exit': exit_code, 'seconds': round(time.time() - t0, 1), 'calls': sequence, 'final': text[:800],
            'answer': answer}


def parse(text):
    try:
        return json.loads(text)
    except ValueError:
        return {}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27283)
    ap.add_argument('--no-cli', action='store_true')
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    stem = 'live_variable_defaults_0283_%s' % args.label
    log_path = os.path.join(log_dir, stem + '_editor.log')
    report_path = os.path.join(log_dir, stem + '.json')

    port = free_port(args.port)
    folder = '/Game/N2CTest/VarDefaults0283_%s' % time.strftime('%H%M%S')
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
            checks['setup'] = data.get('created') is True
            status, text, err = p.tool('open-blueprint-asset', {'asset_path': path, 'focus_graph': 'EventGraph'})
            checks['open'] = not err
            made = []
            for var in ({'variableName': 'Health', 'typeIdentifier': 'int', 'defaultValue': '100'},
                        {'variableName': 'Speed', 'typeIdentifier': 'float'},
                        {'variableName': 'Channel', 'typeIdentifier': 'ECollisionChannel'}):
                status, text, err = p.tool('create-variable', var)
                made.append({'variable': var['variableName'], 'isError': err, 'text': text[:200]})
            report['created'] = made
            checks['variables created'] = not any(m['isError'] for m in made)

            if args.no_cli:
                listed, _ = p.members()
                _, _, speed_text = p.tool('set-member-variable-default-value', {'variableName': 'Speed', 'defaultValue': '75'})
                _, fast_text, fast_err = p.tool('set-member-variable-default-value', {'variableName': 'Speed', 'defaultValue': 'fast'})
                _, channel_text, _ = p.tool('set-member-variable-default-value', {'variableName': 'Channel', 'defaultValue': 'Camera'})
                speed, channel = parse(speed_text), parse(channel_text)
                answer = {'healthDefault': (listed.get('Health') or {}).get('defaultValue'),
                          'speedCategory': ((listed.get('Speed') or {}).get('type') or {}).get('category'),
                          'speedOld': speed.get('oldDefaultValue'), 'speedNew': speed.get('newDefaultValue'),
                          'fastIsError': fast_err, 'fastMessage': fast_text[:300], 'channelNew': channel.get('newDefaultValue')}
                report['probe_calls'] = answer
            else:
                cli = run_cli(port, os.path.join(log_dir, stem + '_mcp.json'), os.path.join(log_dir, stem + '_stream.jsonl'),
                              args.cli_model, args.timeout)
                report['cli'] = cli
                answer = cli['answer'] or {}
                tools_called = [c['tool'] for c in cli['calls']]
                checks['cli: the four calls'] = tools_called == ['get-blueprint-member-variables'] + ['set-member-variable-default-value'] * 3
                set_calls = [c for c in cli['calls'] if c['tool'] == 'set-member-variable-default-value']
                checks['cli: only the "fast" call failed'] = [bool(c['isError']) for c in set_calls] == [False, True, False]

            checks['Health reads back 100 (card 0171)'] = answer.get('healthDefault') == '100'
            checks['Speed is a float'] = answer.get('speedCategory') == 'float'
            checks['Speed old value 0.000000, new 75.000000'] = answer.get('speedOld') == '0.000000' and answer.get('speedNew') == '75.000000'
            checks['"fast" is refused naming the type'] = answer.get('fastIsError') is True and 'not a valid' in str(answer.get('fastMessage'))
            checks['Channel by display name is ECC_Camera'] = answer.get('channelNew') == 'ECC_Camera'

            listed, text = p.members()
            report['listed'] = {k: {'defaultValue': (v or {}).get('defaultValue'), 'type': (v or {}).get('type', {}).get('displayString')}
                                for k, v in listed.items()}
            checks['listing: Health 100, Speed 75, Channel ECC_Camera'] = (
                (listed.get('Health') or {}).get('defaultValue') == '100'
                and (listed.get('Speed') or {}).get('defaultValue') == '75.000000'
                and (listed.get('Channel') or {}).get('defaultValue') == 'ECC_Camera')
            cdo, text = p.python(CDO_PY % {'path': path})
            report['cdo'] = cdo or text
            checks['class defaults: Speed 75, Channel ECC_Camera'] = cdo.get('Speed') == 75 and 'CAMERA' in str(cdo.get('Channel')).upper()

            p.python(UNDO_PY)
            cdo1, _ = p.python(CDO_PY % {'path': path})
            listed1, _ = p.members()
            p.python(UNDO_PY)
            cdo2, _ = p.python(CDO_PY % {'path': path})
            listed2, _ = p.members()
            report['after_undo'] = {'1': {'cdo': cdo1, 'Channel': (listed1.get('Channel') or {}).get('defaultValue')},
                                    '2': {'cdo': cdo2, 'Speed': (listed2.get('Speed') or {}).get('defaultValue')}}
            checks['undo 1: Channel back to ECC_WorldStatic, Speed keeps 75'] = (
                (listed1.get('Channel') or {}).get('defaultValue') == 'ECC_WorldStatic' and cdo1.get('Speed') == 75)
            checks['undo 2: Speed back to 0 on the class defaults'] = (
                cdo2.get('Speed') == 0 and (listed2.get('Speed') or {}).get('defaultValue') == '0.000000')

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME})
            report['cleanup'] = data or text
            checks['scratch folder deleted'] = data.get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(bool(v) for v in checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
