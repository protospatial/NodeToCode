"""The Blueprint resources by asset path, live (card 0208): blueprints/all's resourceUri reads BP_Player as N2C JSON.

Stdlib only; run with `python -I`:

    python -I live_resources_0208.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor (-RenderOffScreen), makes an Actor Blueprint BP_Player with a function under a fresh
/Game/N2CTest/Res0208_<time> folder (through run-python) and opens its editor, so nodetocode://blueprints/all lists it.
Then, over raw JSON-RPC: resources/templates/list lists nodetocode://blueprint/{+path}; blueprints/all gives BP_Player
a resourceUri that keeps its underscore (nodetocode://blueprint/Game/N2CTest/Res0208_<time>/BP_Player);
resources/read of that URI returns its N2C JSON (version 2.0.0, the function graph with its entry node, marked
untrusted first) and no stub; the old name form nodetocode://blueprint/BP_Player reads the same document; an empty
value, a query, an empty segment, a malformed escape and an unknown Blueprint are -32002 with the uri in data.
Unless --skip-cli, claude -p with only ListMcpResourcesTool and ReadMcpResourceTool allowed reads blueprints/all and
then BP_Player's resourceUri, and answers with the graph names it got. The editor is not closed before shutdown
(closing a headless Blueprint editor is fatal on UE 5.6 and 5.7); on 5.8 the scratch folder is deleted, below it
the unsaved Blueprint goes with the editor.
Report: logs/live_resources_0208_<label>.json.
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
from live_env_translation import post  # noqa: E402
from live_graph_target_0152 import Probe  # noqa: E402

NAME = 'BP_Player'
FUNCTION = 'N2CRes0208Fn'
TEMPLATE = 'nodetocode://blueprint/{+path}'

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
if bp:
    result["function"] = unreal.BlueprintEditorLibrary.add_function_graph(bp, "%(fn)s") is not None
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    result["opened"] = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([bp])
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

PROMPT = (
    'Using only the MCP resource tools on the "nodetocode" server: 1) read the resource nodetocode://blueprints/all; '
    '2) read the resourceUri it lists for the Blueprint named %(name)s. Do nothing else. Reply with only a JSON object '
    '{"uri": <the resourceUri you read in step 2>, "graphs": [<the graph names in the N2C JSON of step 2>]}.')


class Rpc:
    def __init__(self, port):
        self.port = port
        self.rid = 500

    def __call__(self, method, params=None):
        self.rid += 1
        body = {'jsonrpc': '2.0', 'id': self.rid, 'method': method}
        if params is not None:
            body['params'] = params
        status, text = post(self.port, body)
        try:
            return status, json.loads(text)
        except ValueError:
            return status, {'raw': (text or '')[:500]}


def read_text(message):
    try:
        return message['result']['contents'][0]['text']
    except (KeyError, IndexError, TypeError):
        return None


def run_claude(prompt, mcp_config_path, model, timeout, out_path):
    cmd = [shutil.which('claude') or 'claude', '-p', prompt, '--mcp-config', mcp_config_path, '--strict-mcp-config',
           '--allowedTools', 'ListMcpResourcesTool,ReadMcpResourceTool', '--output-format', 'stream-json', '--verbose',
           '--model', model]
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
        out = proc.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b'').decode('utf-8', 'replace')
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
            if isinstance(block, dict) and block.get('type') == 'tool_use':
                calls.append({'id': block.get('id'), 'tool': block.get('name'), 'input': block.get('input') or {}})
            elif isinstance(block, dict) and block.get('type') == 'tool_result':
                results[block.get('tool_use_id')] = bool(block.get('is_error'))
    for call in calls:
        call['error'] = results.get(call['id'], True)
    answer = None
    text = (final or {}).get('result') or ''
    start, end = text.find('{'), text.rfind('}')
    if start >= 0 and end > start:
        try:
            answer = json.loads(text[start:end + 1])
        except ValueError:
            answer = text[:300]
    return {'calls': calls, 'answer': answer}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27208)
    ap.add_argument('--cli-model', default='claude-haiku-5-5')
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--skip-cli', action='store_true')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_resources_0208_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_resources_0208_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_resources_0208_%s_mcp.json' % args.label)

    port = free_port(args.port + int(os.environ.get('N2C_PORT_OFFSET', '0') or 0))
    folder = '/Game/N2CTest/Res0208_%s' % time.strftime('%H%M%S')
    expected_uri = 'nodetocode://blueprint/%s/%s' % (folder.lstrip('/'), NAME)
    report = {'label': args.label, 'port': port, 'folder': folder}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            rpc = Rpc(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME, 'fn': FUNCTION})
            report['setup'] = data or text
            checks['setup: BP_Player made with a function, its editor open'] = data.get('created') is True \
                and data.get('function') is True

            _, templates = rpc('resources/templates/list')
            listed_templates = [t.get('uriTemplate') for t in (templates.get('result') or {}).get('resourceTemplates') or []]
            report['templates'] = listed_templates
            checks['templates/list: nodetocode://blueprint/{+path}'] = TEMPLATE in listed_templates

            _, all_message = rpc('resources/read', {'uri': 'nodetocode://blueprints/all'})
            try:
                entries = json.loads(read_text(all_message) or '{}').get('blueprints') or []
            except ValueError:
                entries = []
            entry = next((e for e in entries if e.get('name') == NAME), {})
            report['blueprints_all_entry'] = entry
            uri = entry.get('resourceUri')
            checks['blueprints/all: BP_Player by asset path, underscore kept'] = uri == expected_uri

            _, read = rpc('resources/read', {'uri': uri or expected_uri})
            text = read_text(read) or ''
            try:
                document = json.loads(text)
            except ValueError:
                document = {}
            graphs = {g.get('name'): g for g in document.get('graphs') or []}
            nodes = (graphs.get(FUNCTION) or {}).get('nodes') or []
            report['read'] = {'error': read.get('error'), 'version': document.get('version'), 'graphs': list(graphs),
                              'chars': len(text), 'keys': list(document)[:6]}
            checks['read: N2C JSON with the function and its entry node'] = 'error' not in read \
                and document.get('version') == '2.0.0' and any(n.get('class') == 'K2Node_FunctionEntry' for n in nodes)
            checks['read: no stub message'] = 'message' not in document and 'not yet implemented' not in text
            checks['read: marked untrusted first'] = text.lstrip().startswith('{"untrusted_content"')

            _, by_name = rpc('resources/read', {'uri': 'nodetocode://blueprint/' + NAME})
            report['by_name'] = {'error': by_name.get('error'), 'chars': len(read_text(by_name) or '')}
            checks['old name form reads the same document'] = 'error' not in by_name and read_text(by_name) == text

            refused = {}
            for bad in ('nodetocode://blueprint/', 'nodetocode://blueprint/a/b?x',
                        'nodetocode://blueprint/Game//BP_Player', 'nodetocode://blueprint/BP%zz',
                        'nodetocode://blueprint/Game/N2CTest/Res0208_none/BP_Missing',
                        'nodetocode://blueprint/BP_N2CRes0208Missing'):
                _, message = rpc('resources/read', {'uri': bad})
                error = message.get('error') or {}
                refused[bad] = error
                checks['%s: -32002 with the uri' % bad] = error.get('code') == -32002 \
                    and (error.get('data') or {}).get('uri') == bad
            report['refused'] = refused

            if not args.skip_cli:
                with open(mcp_config_path, 'w', encoding='utf-8') as f:
                    json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
                run = run_claude(PROMPT % {'name': NAME}, mcp_config_path, args.cli_model, args.timeout,
                                 os.path.join(log_dir, 'live_resources_0208_%s_cli.jsonl' % args.label))
                report['cli'] = run
                reads = [c for c in run['calls'] if c['tool'] == 'ReadMcpResourceTool']
                checks['agent: read blueprints/all, then the listed resourceUri'] = len(reads) >= 2 \
                    and reads[0]['input'].get('uri') == 'nodetocode://blueprints/all' \
                    and any(c['input'].get('uri') == expected_uri and not c['error'] for c in reads[1:])
                answer = run['answer'] if isinstance(run['answer'], dict) else {}
                checks['agent: answered with the URI and the function graph'] = answer.get('uri') == expected_uri \
                    and FUNCTION in (answer.get('graphs') or [])

            if args.label.startswith('5.8'):
                # Deleting the asset closes its editor, which is fatal headless below UE 5.8; nothing was saved there
                data, text = p.python(CLEANUP_PY % {'folder': folder})
                report['cleanup'] = data or text
                checks['scratch folder deleted'] = data.get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
