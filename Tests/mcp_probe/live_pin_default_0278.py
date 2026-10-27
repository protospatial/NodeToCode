"""Pin default writes take the K2 schema's verdict, live through MCP and claude -p (card 0278).

Stdlib only; run with `python -I`:

    python -I live_pin_default_0278.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint in memory under a fresh /Game/N2CTest/PinDefault0278_<time>
folder, without opening it. Then, every call by blueprintPath and graphName EventGraph:

1. The probe adds Get All Actors Of Class, Clamp (integer) and Line Trace By Channel (search-blueprint-nodes, then
   add-bp-node-to-active-graph) and sets ActorClass to StaticMeshActor.
2. claude -p (unless --no-cli), with only mcp__nodetocode__* allowed, is asked to make ONE set-input-pin-value call
   that puts the Blueprint asset itself on ActorClass, and to report what the tool answered.
3. The probe sends 1.5 to the integer pin, the index 2 and the display name "For One Frame" to DrawDebugType, and an
   empty value to ActorClass.

Checks: the agent's call is an error naming the schema's reason ("is not a class") and the generated class to send,
and the agent reports the refusal; ActorClass still reads StaticMeshActor afterwards (find-nodes-in-graph). 1.5 is
refused and Max keeps 7; the index and display name come back as ForDuration and ForOneFrame in newValue; the empty value
resets ActorClass to None. Afterwards the folder is deleted (nothing was saved).
Report: logs/live_pin_default_0278_<label>.json.
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
from live_env_translation import call_tool, tool_text  # noqa: E402

NAME = 'BP_N2CPinDefault0278'
MESH_ACTOR = '/Script/Engine.StaticMeshActor'

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
bp = unreal.load_asset(folder + "/%(name)s")
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), make exactly ONE set-input-pin-value call, and no other '
    'call: on the Blueprint at blueprintPath "%(path)s", graphName "EventGraph", node nodeGuid "%(node)s", pin '
    'pinGuid "%(pin)s" (the ActorClass pin of Get All Actors Of Class), set the value to "%(path)s". Do not retry '
    'and do not fix anything. Reply with only a JSON object {"isError": <true if the tool returned an error>, '
    '"message": <the tool\'s message or error text>, "pinValue": <the pin\'s value according to the tool, or null>}.')


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
        wrapped = 'def _n2c_0278():\n%s\n    return result\nresult = _n2c_0278()\ndel _n2c_0278\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]


def body_of(text):
    """The tool result's JSON object (the untrusted-content marker, when present, is a field of it)"""
    try:
        body = json.loads(text)
    except ValueError:
        return {}
    return body if isinstance(body, dict) else {}


def run_cli(prompt, mcp_config_path, model, timeout, out_path):
    cmd = [shutil.which('claude') or 'claude', '-p', prompt, '--mcp-config', mcp_config_path, '--strict-mcp-config',
           '--allowedTools', 'mcp__nodetocode__*', '--output-format', 'stream-json', '--verbose', '--model', model]
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
        out, code = proc.stdout.decode('utf-8', 'replace'), proc.returncode
    except subprocess.TimeoutExpired as e:
        out, code = (e.stdout or b'').decode('utf-8', 'replace'), 'timeout'
    with open(out_path, 'w', encoding='utf-8') as f:
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
                results[block.get('tool_use_id')] = (c if isinstance(c, str) else ''.join(
                    x.get('text', '') for x in (c or []) if isinstance(x, dict)), bool(block.get('is_error')))
    seq = []
    for call in calls:
        text, is_error = results.get(call.get('id'), ('', True))
        seq.append({'tool': call.get('name', '').replace('mcp__nodetocode__', ''), 'input': call.get('input') or {},
                    'text': text, 'error': is_error})
    answer = None
    try:
        raw = (final or {}).get('result', '').strip()
        raw = raw[raw.find('{'):raw.rfind('}') + 1]
        answer = json.loads(raw)
    except ValueError:
        pass
    return {'exit': code, 'seconds': round(time.time() - t0, 1), 'calls': seq, 'answer': answer,
            'final': (final or {}).get('result')}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27199)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--no-cli', action='store_true', help='skip the claude -p step')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_pin_default_0278_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_pin_default_0278_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_pin_default_0278_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/PinDefault0278_%s' % time.strftime('%H%M%S')
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

            def add(term, match):
                status, text, err = p.tool('search-blueprint-nodes', dict(target, searchTerm=term))
                report.setdefault('searched', {})[term] = [(n.get('spawnMetadata') or {}).get('actionIdentifier')
                                                           for n in (body_of(text).get('nodes') or [])][:12]
                for node in body_of(text).get('nodes') or []:
                    spawn = node.get('spawnMetadata') or {}
                    # The whole function name: GetAllActorsOfClass must not match GetAllActorsOfClassWithTag
                    if re.search(re.escape(match) + r'(?![A-Za-z0-9_])', spawn.get('actionIdentifier') or ''):
                        status, text, err = p.tool('add-bp-node-to-active-graph', dict(
                            target, nodeName=spawn.get('nodeName'), actionIdentifier=spawn.get('actionIdentifier')))
                        return body_of(text) if not err else {}
                return {}

            def pin(node, name):
                return next((x for x in node.get('inputPins') or [] if x.get('pinName') == name), {})

            def set_value(node, pin_name, value):
                status, text, err = p.tool('set-input-pin-value', dict(
                    target, nodeGuid=node.get('nodeGuid'), pinGuid=pin(node, pin_name).get('pinGuid'), value=value))
                return err, text, body_of(text)

            def read_default(node, pin_name):
                status, text, err = p.tool('find-nodes-in-graph', dict(target, searchType='guid',
                                                                       searchTerms=[node.get('nodeGuid')]))
                # N2C JSON: each input pin's ids.name is its PinName, default_value what the capture read
                for found in body_of(text).get('nodes') or []:
                    for x in found.get('input_pins') or []:
                        if (x.get('ids') or {}).get('name') == pin_name or x.get('name') == pin_name:
                            return x.get('default_value', '')
                return None

            get_all = add('get all actors of class', 'GameplayStatics:GetAllActorsOfClass')
            add_int = add('clamp (integer)', 'KismetMathLibrary:Clamp')
            trace = add('line trace by channel', 'KismetSystemLibrary:LineTraceSingle')
            report['nodes'] = {'getAll': get_all.get('nodeGuid'), 'addInt': add_int.get('nodeGuid'),
                               'trace': trace.get('nodeGuid')}
            checks['nodes added'] = all(report['nodes'].values())

            err, text, body = set_value(get_all, 'ActorClass', MESH_ACTOR)
            report['set_mesh_actor'] = text[:400]
            checks['ActorClass set to StaticMeshActor'] = not err and body.get('newValue') == MESH_ACTOR

            # 2. The agent sends the Blueprint asset to the class pin
            if args.no_cli:
                err, text, body = set_value(get_all, 'ActorClass', path)
                report['blueprint_asset'] = text[:600]
                checks['the Blueprint asset is refused with the schema\'s reason'] = err and 'is not a class' in text \
                    and '%s_C' % NAME in text
            else:
                with open(mcp_config_path, 'w', encoding='utf-8') as f:
                    json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
                cli = run_cli(PROMPT % {'path': path, 'node': get_all.get('nodeGuid'),
                                        'pin': pin(get_all, 'ActorClass').get('pinGuid')},
                              mcp_config_path, args.cli_model, args.timeout,
                              os.path.join(log_dir, 'live_pin_default_0278_%s_cli.jsonl' % args.label))
                sets = [c for c in cli['calls'] if c['tool'] == 'set-input-pin-value']
                report['cli'] = {'calls': [(c['tool'], c['input'].get('value'), c['error'], c['text'][:500])
                                           for c in cli['calls']],
                                 'answer': cli['answer'], 'exit': cli['exit'], 'seconds': cli['seconds']}
                checks['cli: one set-input-pin-value call with the asset path'] = len(sets) == 1 and \
                    sets[0]['input'].get('value') == path
                checks['cli: the tool refused it with the schema\'s reason and the _C class'] = bool(sets) and \
                    sets[0]['error'] and 'is not a class' in sets[0]['text'] and '%s_C' % NAME in sets[0]['text']
                checks['cli: the agent reports the refusal'] = isinstance(cli['answer'], dict) and \
                    cli['answer'].get('isError') is True
            actor_class = read_default(get_all, 'ActorClass')
            report['ActorClass after'] = actor_class
            checks['ActorClass still StaticMeshActor'] = 'StaticMeshActor' in (actor_class or '')

            # 3. The probe's own writes
            err, text, body = set_value(add_int, 'Max', '7')
            checks['Max set to 7'] = not err and body.get('newValue') == '7'
            err, text, body = set_value(add_int, 'Max', '1.5')
            report['int 1.5'] = text[:400]
            checks['1.5 on an integer pin is refused'] = err and 'integer' in text
            checks['Max keeps 7'] = read_default(add_int, 'Max') == '7'
            err, text, body = set_value(trace, 'DrawDebugType', '2')
            report['enum index'] = text[:400]
            checks['enum index 2 reads back ForDuration'] = not err and body.get('newValue') == 'ForDuration'
            err, text, body = set_value(trace, 'DrawDebugType', 'For One Frame')
            report['enum display name'] = text[:400]
            checks['display name reads back ForOneFrame'] = not err and body.get('newValue') == 'ForOneFrame'
            err, text, body = set_value(get_all, 'ActorClass', '')
            report['empty'] = text[:400]
            checks['an empty value resets ActorClass to None'] = not err and body.get('newValue') in ('', 'None') \
                and body.get('oldValue') == MESH_ACTOR

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
