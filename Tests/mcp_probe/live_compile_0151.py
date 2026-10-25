"""One compile per tool call, with the compiler's diagnostics in the result, live through claude -p (card 0151).

Stdlib only; run with `python -I`:

    python -I live_compile_0151.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor, makes an Actor Blueprint in memory under a fresh /Game/N2CTest/Compile0151_<time>
folder and opens it on its EventGraph, then:

1. MCP: a context-free search-blueprint-nodes call (must compile nothing), then the probe adds a Static Mesh
   component (add-component-class-to-actor) and a Branch node from its search result's stable actionIdentifier
   (card 0277: the spawner signature, no carriage returns), one compile per call.
2. claude -p (unless --no-cli), with only mcp__nodetocode__* allowed, asked to add a Custom Event, a Print String and
   a Set Static Mesh node itself (search-blueprint-nodes, then add-bp-node-to-active-graph with the returned
   actionIdentifier; before card 0277 an agent could not, so the probe added them), wire them in ONE connect-pins
   call, leaving Set Static Mesh's Target unconnected (an Actor Blueprint is not a StaticMeshComponent, so that does
   not compile), then report what the connect-pins result said about the compile.

Checks: the editor log's "Blueprint '<name>' compiled:" lines (FN2CMcpBlueprintUtils::Compile) between the
"Calling tool:" lines give the compiles per tool call: one for each node add and for the connect-pins call, none for
a search or a find. The agent's adds all succeed with the IDs it was given (no failed add-bp-node-to-active-graph
call). The connect-pins result carries compile.errorCount >= 1 with a message naming the Set Static Mesh node's GUID,
and the agent's answer repeats the error. Afterwards the editor is closed and the folder deleted (nothing was
saved). Report: logs/live_compile_0151_<label>.json.
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

NAME = 'BP_N2CCompile0151'

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
    'Using only the nodetocode MCP tools, edit the Blueprint that is open and focused (do not open, delete or save '
    'anything, and add no components or variables). Add three nodes to its graph with search-blueprint-nodes and then '
    'add-bp-node-to-active-graph, passing a result\'s spawnMetadata.actionIdentifier and spawnMetadata.nodeName: a '
    'Custom Event (search "add custom event"), a Print String, and a Set Static Mesh for the StaticMeshComponent '
    '(search "set static mesh"). Then make ONE connect-pins call with two connections, using pin names: the custom '
    'event\'s "then" pin to Print String\'s "execute" pin, and Print String\'s "then" pin to Set Static Mesh\'s '
    '"execute" pin (the add results give the node GUIDs and pin GUIDs). Leave every other pin unconnected and do not fix '
    'any compile error. Reply with only a JSON object {"added": {"event": <nodeGuid>, "print": <nodeGuid>, "mesh": '
    '<nodeGuid>}, "errorCount": <compile.errorCount from the connect-pins result>, "errors": [<each compile error '
    'message text>], "errorNodeGuids": [<nodeGuid of each compile error>]}.')


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
        wrapped = 'def _n2c_0151():\n%s\n    return result\nresult = _n2c_0151()\ndel _n2c_0151\n' % body
        status, text, is_error = self.tool('run-python', {'code': wrapped})
        try:
            data = json.loads(text).get('data') or {}
        except (ValueError, AttributeError):
            data = {}
        return data, text[:600]


def read_log(path):
    with open(path, 'rb') as f:
        raw = f.read()
    return raw.decode('utf-8', 'replace').replace('\x00', '')


def compiles_per_call(log_text, start, name):
    """[(tool, compiles of this Blueprint during that call)] for every tools/call after offset start"""
    calls = []
    for line in log_text[start:].splitlines():
        m = re.search(r'Calling tool: ([\w-]+)', line)
        if m:
            calls.append([m.group(1), 0])
        elif ("Blueprint '%s' compiled:" % name) in line and calls:
            calls[-1][1] += 1
    return [tuple(c) for c in calls]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27196)
    ap.add_argument('--no-cli', action='store_true', help='skip the claude -p step')
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_compile_0151_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_compile_0151_%s.json' % args.label)
    cli_out_path = os.path.join(log_dir, 'live_compile_0151_%s_stream.jsonl' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_compile_0151_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Compile0151_%s' % time.strftime('%H%M%S')
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
            status, text, err = p.tool('open-blueprint-asset', {'asset_path': path, 'focus_graph': 'EventGraph'})
            checks['open'] = not err

            start = len(read_log(log_path))
            status, text, err = p.tool('search-blueprint-nodes', {'searchTerm': 'print string', 'contextSensitive': False,
                                                                 'maxResults': 3})
            checks['search succeeds'] = not err
            checks['search result has no compile'] = '"compile"' not in text
            time.sleep(1.0)
            search_calls = compiles_per_call(read_log(log_path), start, NAME)
            report['search_compiles'] = search_calls
            checks['search: no compile'] = bool(search_calls) and all(c == 0 for _, c in search_calls)

            # A Static Mesh component, so the graph's context menu offers Set Static Mesh (a structural edit: one compile)
            guids = {}
            start = len(read_log(log_path))
            status, text, err = p.tool('add-component-class-to-actor', {'componentClass': '/Script/Engine.StaticMeshComponent',
                                                                        'componentName': 'N2CMesh'})
            checks['component added'] = not err
            try:
                comp = json.loads(text)
            except ValueError:
                comp = {}
            report['component'] = {k: comp.get(k) for k in ('componentName', 'compilationStatus')}
            checks['component result carries compile'] = isinstance(comp.get('compile'), dict)

            # A Branch from its search result's stable ID (card 0277), one add call: one compile
            status, text, err = p.tool('search-blueprint-nodes', {'searchTerm': 'branch', 'maxResults': 10})
            try:
                nodes = json.loads(text).get('nodes') or []
            except ValueError:
                nodes = []
            branch = next((n['spawnMetadata'] for n in nodes
                           if 'K2Node_IfThenElse' in ((n.get('spawnMetadata') or {}).get('actionIdentifier') or '')), None)
            checks['branch found'] = branch is not None
            if branch:
                ident = branch['actionIdentifier']
                report['branch_id'] = ident
                checks['branch id has no control character'] = not any(ord(c) < 32 for c in ident)
                status, text, err = p.tool('add-bp-node-to-active-graph', {'nodeName': branch.get('nodeName', 'Branch'),
                                                                          'actionIdentifier': ident})
                checks['branch added from its id'] = not err
                try:
                    checks['add branch result carries compile'] = isinstance(json.loads(text).get('compile'), dict)
                except ValueError:
                    checks['add branch result carries compile'] = False
            time.sleep(1.0)
            add_calls = compiles_per_call(read_log(log_path), start, NAME)
            report['add_compiles_per_call'] = add_calls
            checks['each add compiled exactly once'] = all(c == 1 for t, c in add_calls if t == 'add-bp-node-to-active-graph') \
                and any(t == 'add-bp-node-to-active-graph' for t, _ in add_calls)
            checks['searches compiled nothing'] = all(c == 0 for t, c in add_calls if t == 'search-blueprint-nodes')

            if not args.no_cli:
                with open(mcp_config_path, 'w', encoding='utf-8') as f:
                    json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
                start = len(read_log(log_path))
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
                time.sleep(1.0)
                per_call = compiles_per_call(read_log(log_path), start, NAME)
                report['cli_compiles_per_call'] = per_call

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

                added = {}
                add_errors = []
                connect_compile = None
                sequence = []
                for call in calls:
                    name = call.get('name', '').replace('mcp__nodetocode__', '')
                    text = results.get(call.get('id'), '')
                    try:
                        body = json.loads(text)
                    except ValueError:
                        body = {}
                    entry = {'tool': name}
                    if name == 'add-bp-node-to-active-graph':
                        entry['actionIdentifier'] = (call.get('input') or {}).get('actionIdentifier')
                        if not (isinstance(body, dict) and body.get('success')):
                            add_errors.append(text[:300])
                    if name == 'add-bp-node-to-active-graph' and isinstance(body, dict):
                        entry['node'] = body.get('nodeName')
                        entry['nodeGuid'] = body.get('nodeGuid')
                        entry['compile'] = (body.get('compile') or {}).get('status')
                        added[body.get('nodeGuid')] = body.get('nodeName') or ''
                    if name == 'connect-pins' and isinstance(body, dict):
                        connect_compile = body.get('compile')
                        entry['compile'] = connect_compile
                    sequence.append(entry)
                report['cli_calls'] = sequence
                report['cli_final'] = (final or {}).get('result')

                mutating = [(t, c) for t, c in per_call if t not in ('search-blueprint-nodes', 'find-nodes-in-graph',
                                                                      'get-focused-blueprint')]
                report['cli_mutating_calls'] = mutating
                checks['cli: one connect-pins call, compiled exactly once'] = [c for t, c in mutating if t == 'connect-pins'] == [1]
                report['cli_add_errors'] = add_errors
                checks['cli: the agent added the three nodes itself'] = len(added) >= 3 and \
                    any('StaticMesh' in n.replace(' ', '') for n in added.values()) \
                    and any('Print' in n for n in added.values())
                checks['cli: no add call failed'] = not add_errors
                checks['cli: each add compiled exactly once'] = all(c == 1 for t, c in mutating
                                                                    if t == 'add-bp-node-to-active-graph')
                checks['cli: read-only calls compiled nothing'] = all(c == 0 for t, c in per_call
                                                                      if t in ('search-blueprint-nodes', 'find-nodes-in-graph', 'get-focused-blueprint'))
                set_mesh = [g for g, n in added.items() if 'StaticMesh' in n.replace(' ', '')][:1] or [None]
                err_guids = [m.get('nodeGuid') for m in ((connect_compile or {}).get('messages') or [])
                             if m.get('severity') == 'Error']
                checks['cli: connect-pins result reports compile errors'] = bool(connect_compile) and \
                    connect_compile.get('errorCount', 0) >= 1 and connect_compile.get('success') is False
                checks['cli: an error names the Set Static Mesh node'] = set_mesh[0] in err_guids
                try:
                    answer = json.loads((final or {}).get('result', '').strip().strip('`').replace('json\n', '', 1))
                except ValueError:
                    answer = None
                report['cli_answer'] = answer
                checks['cli: the agent reports the error'] = isinstance(answer, dict) and answer.get('errorCount', 0) >= 1 \
                    and any('StaticMeshComponent' in e for e in answer.get('errors') or [])
                checks['cli: the agent reports the node GUID'] = isinstance(answer, dict) and bool(set_mesh) \
                    and set_mesh[0] in (answer.get('errorNodeGuids') or [])

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
