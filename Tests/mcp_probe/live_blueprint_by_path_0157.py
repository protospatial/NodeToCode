"""list-blueprint-graphs and get-blueprint-n2cjson by asset path with no Blueprint editor open, live through claude -p
(card 0157).

Stdlib only; run with `python -I`:

    python -I live_blueprint_by_path_0157.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint with a function under a fresh /Game/N2CTest/ByPath0157_<time>
folder (through run-python), closes every editor of it, and checks that get-open-blueprint-editors lists none. Then claude -p, with
only mcp__nodetocode__* allowed, lists the Blueprint's graphs (list-blueprint-graphs) and reads the function by path
(get-blueprint-n2cjson), answering with what it got.

Checks: the agent called both tools with the scratch blueprintPath, neither failed, it opened no editor
(open-blueprint-asset, open-blueprint-function) and the editor still has none open afterwards; list-blueprint-graphs
returned the event graph and the function with GraphGuids, and get-blueprint-n2cjson returned the function's N2C JSON
(its graph named, the entry node in it); both results start with untrusted_content; the agent's answer matches. The
probe also reads /Engine/EngineSky/BP_Sky_Sphere (every graph) with toFile and checks the file parses. Afterwards the
scratch folder is deleted (nothing was saved) and the export file removed. Report: logs/live_blueprint_by_path_0157_<label>.json.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import Probe, run_cli  # noqa: E402

NAME = 'BP_N2CByPath0157'
FUNCTION = 'N2CByPathFn'
ENGINE_BLUEPRINT = '/Engine/EngineSky/BP_Sky_Sphere'

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
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
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
    'Using only the nodetocode MCP tools (not run-python, and do not open any editor), work on the Blueprint at '
    'blueprintPath "%(path)s": 1) list its graphs with list-blueprint-graphs; 2) read its function graph %(fn)s as N2C '
    'JSON with get-blueprint-n2cjson (pass blueprintPath and that graph\'s graphGuid). Do nothing else. Reply with only a '
    'JSON object {"graphs": [<graph names from step 1>], "functionGuid": <the function\'s graphGuid>, '
    '"nodeCount": <how many nodes the N2C JSON of step 2 has in that graph>}.')


def open_editor_count(probe):
    """How many Blueprint editors get-open-blueprint-editors reports; -1 when it fails"""
    status, text, err = probe.tool('get-open-blueprint-editors', {})
    try:
        return -1 if err else int(json.loads(text).get('count'))
    except (ValueError, TypeError, AttributeError):
        return -1


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27198)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_blueprint_by_path_0157_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_blueprint_by_path_0157_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_blueprint_by_path_0157_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/ByPath0157_%s' % time.strftime('%H%M%S')
    path = '%s/%s.%s' % (folder, NAME, NAME)
    report = {'label': args.label, 'port': port, 'folder': folder}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME, 'fn': FUNCTION})
            report['setup'] = data or text
            checks['setup: Blueprint and function made'] = data.get('created') is True and data.get('function') is True
            checks['setup: no Blueprint editor open'] = open_editor_count(p) == 0
            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)

            run = run_cli(PROMPT % {'path': path, 'fn': FUNCTION}, mcp_config_path, args.cli_model, args.timeout,
                          os.path.join(log_dir, 'live_blueprint_by_path_0157_%s_cli.jsonl' % args.label))
            report['cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                             'answer': run['answer'], 'exit': run['exit'], 'seconds': run['seconds'], 'final': run['final']}
            lists = [c for c in run['calls'] if c['tool'] == 'list-blueprint-graphs']
            reads = [c for c in run['calls'] if c['tool'] == 'get-blueprint-n2cjson']
            checks['the agent listed the graphs by blueprintPath'] = bool(lists) and not lists[-1]['error'] \
                and lists[-1]['input'].get('blueprintPath') == path
            checks['the agent read the function by blueprintPath'] = bool(reads) and not reads[-1]['error'] \
                and reads[-1]['input'].get('blueprintPath') == path
            checks['no call failed'] = bool(run['calls']) and not any(c['error'] for c in run['calls'])
            checks['the agent opened no editor'] = not any(
                c['tool'] in ('open-blueprint-asset', 'open-blueprint-function') for c in run['calls'])

            listed = strip_mark(lists[-1]['body']) if lists else {}
            graphs = {g.get('name'): g for g in listed.get('graphs') or []}
            report['listed'] = listed
            fn = graphs.get(FUNCTION) or {}
            checks['list: event graph and function, with GraphGuids and kinds'] = \
                (graphs.get('EventGraph') or {}).get('kind') == 'event' and fn.get('kind') == 'function' \
                and len(fn.get('graphGuid') or '') == 32
            checks['list: result marked untrusted first'] = bool(lists) and lists[-1]['text'].lstrip().startswith(
                '{"untrusted_content"')

            document = strip_mark(reads[-1]['body']) if reads else {}
            doc_graphs = {g.get('name'): g for g in document.get('graphs') or []}
            nodes = (doc_graphs.get(FUNCTION) or {}).get('nodes') or []
            report['read'] = {'version': document.get('version'), 'graphs': list(doc_graphs),
                              'function_nodes': [n.get('class') for n in nodes],
                              'graphGuid': reads[-1]['input'].get('graphGuid') if reads else None}
            checks['read: the function graph with its entry node'] = document.get('version') == '2.0.0' and \
                any(n.get('class') == 'K2Node_FunctionEntry' for n in nodes)
            checks['read: by the listed graphGuid'] = bool(reads) and \
                (reads[-1]['input'].get('graphGuid') or '').replace('-', '').upper() == (fn.get('graphGuid') or '-').upper()
            checks['read: result marked untrusted first'] = bool(reads) and reads[-1]['text'].lstrip().startswith(
                '{"untrusted_content"')
            answer = run['answer'] if isinstance(run['answer'], dict) else {}
            checks['the answer matches the results'] = FUNCTION in (answer.get('graphs') or []) and \
                (answer.get('functionGuid') or '').replace('-', '').upper() == (fn.get('graphGuid') or '-').upper() \
                and answer.get('nodeCount') == len(nodes)

            report['open_after'] = open_editor_count(p)
            checks['no Blueprint editor open after the run'] = report['open_after'] == 0

            # A larger engine Blueprint, every graph, to a file
            status, text, err = p.tool('get-blueprint-n2cjson', {'blueprintPath': ENGINE_BLUEPRINT, 'toFile': True})
            try:
                spilled = strip_mark(json.loads(text))
            except ValueError:
                spilled = {}
            report['engine_blueprint'] = spilled
            file_ok = False
            if spilled.get('path') and os.path.isfile(spilled['path']):
                with open(spilled['path'], encoding='utf-8') as f:
                    exported = f.read()
                try:
                    exported_doc = json.loads(exported)
                    file_ok = len(exported_doc.get('graphs') or []) == spilled.get('graphCount') and \
                        len(exported.encode('utf-8')) == spilled.get('bytes')
                except ValueError:
                    pass
                report['engine_blueprint']['chars'] = len(exported)
                # Kept in logs/ (gitignored) for token counts; the export itself is removed
                with open(os.path.join(log_dir, 'live_blueprint_by_path_0157_%s_export.json' % args.label), 'w',
                          encoding='utf-8') as f:
                    f.write(exported)
                os.remove(spilled['path'])
            checks['engine Blueprint: every graph written to a file that parses'] = not err and file_ok \
                and spilled.get('reason') == 'toFile'

            data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME})
            report['cleanup'] = data or text
            checks['scratch folder deleted'] = data.get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok'], 'cli': report.get('cli', {}).get('calls')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
