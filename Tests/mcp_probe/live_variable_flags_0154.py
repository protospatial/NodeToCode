"""create-variable and create-local-variable set their flags on the variable description, undoably, live through claude -p
(card 0154).

Stdlib only; run with `python -I`:

    python -I live_variable_flags_0154.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor, makes an Actor Blueprint in memory under a fresh /Game/N2CTest/VarFlags0154_<time>
folder, opens it, and adds a function N2CFlagsFn with an int input Target (create-blueprint-function). Then:

1. claude -p (only mcp__nodetocode__* allowed) creates three member variables (Health: float, instance editable,
   read-only, replicated, category Stats, a tooltip; Ammo: int, repnotify; Plain: bool, no flags), opens N2CFlagsFn and
   creates a local variable named Target, then reads the members back with get-blueprint-member-variables.
2. The probe reads them back itself (get-blueprint-member-variables, get-blueprint-function-local-variables) and checks
   the flags on the description: Health replicated, read-only, instance editable (flags.private false, which is
   CPF_DisableEditOnInstance), category and tooltip; Ammo replicated with repNotify and repNotifyFunc OnRep_Ammo; Plain
   not instance editable (flags.private true) and not replicated; the local renamed away from the parameter Target.
3. The package is dirty. Undo through the editor's TRANSACTION UNDO console command (run-python): the first undo
   removes the local, the second removes Plain, Health and Ammo keep their flags.

Afterwards the editor is closed and the folder deleted (nothing was saved). Report: logs/live_variable_flags_0154_<label>.json.
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

NAME = 'BP_N2CVarFlags0154'
FUNCTION = 'N2CFlagsFn'

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
    'Using only the nodetocode MCP tools, edit the Blueprint that is open and focused (do not open other assets, delete '
    'or save anything). Make exactly these calls, in this order: '
    '1) create-variable with variableName "Health", typeIdentifier "float", isInstanceEditable true, '
    'isBlueprintReadOnly true, replicationCondition "replicated", category "Stats", tooltip "Hit points left". '
    '2) create-variable with variableName "Ammo", typeIdentifier "int", replicationCondition "repnotify". '
    '3) create-variable with variableName "Plain", typeIdentifier "bool" and no other arguments. '
    '4) open-blueprint-function with graphName "%s". '
    '5) create-local-variable with variableName "Target", typeIdentifier "int". '
    '6) get-blueprint-member-variables. '
    'Reply with only a JSON object {"local": <actualName from the create-local-variable result>, "members": '
    '{<name>: {"replicated": bool, "repNotifyFunc": string or null, "blueprintReadOnly": bool, "private": bool, '
    '"category": string}} for Health, Ammo and Plain from the get-blueprint-member-variables result}.' % FUNCTION)


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
        wrapped = 'def _n2c_0154():\n%s\n    return result\nresult = _n2c_0154()\ndel _n2c_0154\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]

    def members(self):
        body, err, text = self.json_tool('get-blueprint-member-variables', {})
        return {v.get('name'): v for v in (body.get('variables') or [])}, text

    def locals(self):
        body, err, text = self.json_tool('get-blueprint-function-local-variables', {})
        return [v.get('name') for v in (body.get('localVariables') or body.get('variables') or [])], text


def summary(var):
    if not var:
        return None
    flags = var.get('flags') or {}
    rep = flags.get('replication') or var.get('replication') or {}
    # metadata keys are FNames, so their case is whatever the name table first saw (tooltip or ToolTip)
    metadata = {k.lower(): v for k, v in (var.get('metadata') or {}).items()}
    return {'replicated': rep.get('replicated'), 'repNotify': rep.get('repNotify'), 'repNotifyFunc': rep.get('repNotifyFunc'),
            'blueprintReadOnly': flags.get('blueprintReadOnly'), 'private': flags.get('private'),
            'instanceEditable': flags.get('instanceEditable'), 'category': var.get('category'),
            'tooltip': metadata.get('tooltip')}


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
    log_path = os.path.join(log_dir, 'live_variable_flags_0154_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_variable_flags_0154_%s.json' % args.label)
    cli_out_path = os.path.join(log_dir, 'live_variable_flags_0154_%s_stream.jsonl' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_variable_flags_0154_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/VarFlags0154_%s' % time.strftime('%H%M%S')
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
            status, text, err = p.tool('create-blueprint-function', {
                'functionName': FUNCTION, 'parameters': [{'name': 'Target', 'direction': 'input', 'type': 'int'}]})
            checks['function with a Target input'] = not err
            report['function'] = text[:400]

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
            for call in calls:
                text = results.get(call.get('id'), '')
                sequence.append({'tool': call.get('name', '').replace('mcp__nodetocode__', ''),
                                 'input': call.get('input'), 'result': text[:300]})
            report['cli_calls'] = sequence
            report['cli_final'] = (final or {}).get('result')
            tools_called = [c['tool'] for c in sequence]
            checks['cli: three create-variable calls'] = tools_called.count('create-variable') == 3
            checks['cli: one create-local-variable call'] = tools_called.count('create-local-variable') == 1
            checks['cli: read the members back'] = 'get-blueprint-member-variables' in tools_called
            checks['cli: no tool call failed'] = not any(c['result'].startswith('Error') or '"isError": true' in c['result']
                                                         for c in sequence)

            members, text = p.members()
            report['members'] = {k: summary(members.get(k)) for k in ('Health', 'Ammo', 'Plain')}
            h, a, pl = (report['members'][k] or {} for k in ('Health', 'Ammo', 'Plain'))
            checks['Health: replicated, read-only, instance editable'] = h.get('replicated') is True and \
                h.get('blueprintReadOnly') is True and h.get('private') is False and h.get('repNotify') is False
            checks['Health: category and tooltip'] = h.get('category') == 'Stats' and h.get('tooltip') == 'Hit points left'
            checks['Ammo: repnotify with OnRep_Ammo'] = a.get('replicated') is True and a.get('repNotify') is True and \
                a.get('repNotifyFunc') == 'OnRep_Ammo'
            checks['Plain: not instance editable, not replicated'] = pl.get('private') is True and pl.get('replicated') is False

            status, text, err = p.tool('open-blueprint-function', {'graphName': FUNCTION})
            local_names, text = p.locals()
            report['locals'] = local_names
            checks['local renamed away from the parameter Target'] = bool(local_names) and 'Target' not in local_names

            data, text = p.python(DIRTY_PY % {'name': NAME})
            report['dirty'] = data or text
            checks['package dirty'] = bool(data.get('dirty'))

            undo1, _ = p.python(UNDO_PY)
            local_after_undo1, _ = p.locals()
            members_after_undo1, _ = p.members()
            report['after_undo_1'] = {'locals': local_after_undo1, 'members': sorted(members_after_undo1)}
            checks['undo 1 removes the local'] = local_names and not any(n in local_after_undo1 for n in local_names)
            checks['undo 1 keeps the members'] = all(k in members_after_undo1 for k in ('Health', 'Ammo', 'Plain'))
            undo2, _ = p.python(UNDO_PY)
            members_after_undo2, _ = p.members()
            report['after_undo_2'] = {k: summary(members_after_undo2.get(k)) for k in ('Health', 'Ammo', 'Plain')}
            checks['undo 2 removes Plain'] = 'Plain' not in members_after_undo2
            h2 = summary(members_after_undo2.get('Health')) or {}
            checks['undo 2 leaves Health with its flags'] = h2.get('replicated') is True and h2.get('blueprintReadOnly') is True \
                and h2.get('private') is False

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
