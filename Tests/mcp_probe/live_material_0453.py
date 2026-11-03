"""get-material-n2cjson on engine materials, live through claude -p, plus the Python wrapper (card 0453).

Stdlib only; run with `python -I`, on each engine:

    python -I live_material_0453.py --engine <UE_5.x> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_x/NodeToCodeHost_5_x.uproject" --label 5.x

It starts a headless editor (Allow MCP Python Execution on, for the Python check). claude -p, with only
mcp__nodetocode__* allowed, reads /Engine/EngineMaterials/WorldGridMaterial with get-material-n2cjson and answers with
the node count and the material domain it sees. Then the probe calls the tool itself on BasicShapeMaterial,
WorldGridMaterial and the CameraDepthFade material function (each: a document with nodes, asset_kind Material,
target.workingCopy false, marked untrusted first), with toFile (the export parses and is removed), calls
get-blueprint-n2cjson on WorldGridMaterial (UNSUPPORTED_ASSET_TYPE naming get-material-n2cjson), and runs
nodetocode.get_material_n2cjson through run-python (same node count). Report: logs/live_material_0453_<label>.json.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import Probe, run_cli  # noqa: E402

WORLD_GRID = '/Engine/EngineMaterials/WorldGridMaterial'
MATERIALS = [('/Engine/BasicShapes/BasicShapeMaterial', 'material'), (WORLD_GRID, 'material'),
             ('/Engine/Functions/Engine_MaterialFunctions01/Opacity/CameraDepthFade', 'material-function')]

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), read the material "%s" as N2C JSON with '
    'get-material-n2cjson, one call. Do nothing else. Reply with only a JSON object {"nodes": <the number of nodes in '
    'its graphs>, "workingCopy": <the target.workingCopy value>}.') % WORLD_GRID

PYTHON = r'''
import nodetocode
r = nodetocode.get_material_n2cjson("%s")
doc = r.get("data") or {}
result = {"success": r.get("success"), "error": r.get("error"),
          "nodes": sum(len(g.get("nodes") or []) for g in doc.get("graphs") or [])}
''' % WORLD_GRID


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def node_count(doc):
    return sum(len(g.get('nodes') or []) for g in (doc or {}).get('graphs') or [])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27188)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_material_0453_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_material_0453_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_material_0453_%s_mcp.json' % args.label)
    port = free_port(args.port)
    report = {'label': args.label, 'port': port, 'materials': {}}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            counts = {}
            for path, kind in MATERIALS:
                status, text, err = p.tool('get-material-n2cjson', {'materialPath': path})
                try:
                    doc = strip_mark(json.loads(text))
                except ValueError:
                    doc = {}
                target = doc.get('target') or {}
                counts[path] = node_count(doc)
                report['materials'][path] = {'nodes': counts[path], 'asset_kind': (doc.get('metadata') or {}).get('asset_kind'),
                                             'target': target, 'chars': len(text), 'error': text[:200] if err else None}
                checks['%s: a document with nodes, the saved asset, marked' % path.rsplit('/', 1)[1]] = not err and \
                    counts[path] > 0 and target.get('kind') == kind and target.get('workingCopy') is False and \
                    text.lstrip().startswith('{"untrusted_content"')

            status, text, err = p.tool('get-material-n2cjson', {'materialPath': WORLD_GRID, 'toFile': True})
            try:
                saved = strip_mark(json.loads(text))
            except ValueError:
                saved = {}
            file_ok = False
            if saved.get('path') and os.path.isfile(saved['path']):
                with open(saved['path'], 'rb') as f:
                    exported = f.read()
                try:
                    file_ok = node_count(json.loads(exported.decode('utf-8'))) == counts.get(WORLD_GRID) and len(exported) == saved.get('bytes')
                except ValueError:
                    pass
                os.remove(saved['path'])
            report['to_file'] = {k: saved.get(k) for k in ('path', 'bytes', 'reason', 'nodeCount')}
            checks['toFile: the export has the same nodes and the stated size'] = not err and file_ok

            status, text, err = p.tool('get-blueprint-n2cjson', {'blueprintPath': WORLD_GRID})
            report['blueprint_tool'] = text[:300]
            checks['get-blueprint-n2cjson on a material names get-material-n2cjson'] = err and \
                text.startswith('UNSUPPORTED_ASSET_TYPE') and 'get-material-n2cjson' in text

            data, text = p.python(PYTHON)
            report['python'] = data or text
            checks['python: get_material_n2cjson gives the same nodes'] = data.get('success') is True and data.get('nodes') == counts.get(WORLD_GRID)

            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            run = run_cli(PROMPT, mcp_config_path, args.cli_model, args.timeout,
                          os.path.join(log_dir, 'live_material_0453_%s_cli.jsonl' % args.label))
            report['cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                             'answer': run['answer'], 'exit': run['exit'], 'seconds': run['seconds']}
            ours = [c for c in run['calls'] if c['tool'] != 'ToolSearch']
            reads = [c for c in ours if c['tool'] == 'get-material-n2cjson']
            report['refused_guesses'] = [(c['input'], c['text'][:200]) for c in ours if c['error']]
            checks['claude -p: read WorldGridMaterial with get-material-n2cjson'] = bool(reads) and not reads[-1]['error'] \
                and reads[-1]['input'].get('materialPath', '').startswith(WORLD_GRID)
            checks['claude -p: every failed call was a refused argument guess'] = all('INVALID_ARGUMENT' in c['text'] for c in ours if c['error'])
            answer = run['answer'] if isinstance(run['answer'], dict) else {}
            checks['claude -p: the answer matches'] = answer.get('nodes') == counts.get(WORLD_GRID) and answer.get('workingCopy') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok'], 'materials': report['materials']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
