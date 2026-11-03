"""get-material-n2cjson on the focused Material Editor's working copy, live through claude -p (card 0463).

Stdlib only; run with `python -I`, on each engine:

    python -I live_material_focus_0463.py --engine <UE_5.x> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_x/NodeToCodeHost_5_x.uproject" --label 5.x

It starts a headless editor with -RenderOffScreen (a Material Editor under -nullrhi alone is fatal on UE 5.6 and 5.7,
gate 0458) and Allow MCP Python Execution on. The probe reads BasicShapeMaterial by path, checks that
get-material-n2cjson with no materialPath answers NO_FOCUSED_MATERIAL, opens the material in its Material Editor through
run-python, reads the working copy (resolvedBy focus, workingCopy true, the asset's path, node ids equal to the saved
asset's), then makes an unapplied edit through run-python: a Constant 0.4635 added to the working copy named by
target.workingCopyPath with MaterialEditingLibrary. claude -p, with only mcp__nodetocode__* allowed, reads the focused
material and the asset by path and reports what each shows; the focus read must show the edit with workingCopy true
and the path read must not. The editor is closed through run-python at the end.
Report: logs/live_material_focus_0463_<label>.json.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import Probe, run_cli  # noqa: E402

BASIC = '/Engine/BasicShapes/BasicShapeMaterial'
MARK = 0.4635

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), do exactly two calls and nothing else: (1) read the material '
    'open in the user\'s Material Editor with get-material-n2cjson and no materialPath; (2) read "%s" with '
    'get-material-n2cjson and materialPath. Reply with only a JSON object {"focus": {"nodes": <number of nodes in its '
    'graphs>, "workingCopy": <target.workingCopy>, "assetPath": <target.assetPath>, "constant": <true when a node of '
    'class MaterialExpressionConstant has the value 0.4635>}, "path": {the same four fields for the second read}}.') % BASIC

OPEN = r'''
import unreal
asset = unreal.load_asset("%s")
editors = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
result = {"opened": bool(editors.open_editor_for_assets([asset]))}
''' % BASIC

EDIT = r'''
import unreal
copy = unreal.find_object(None, "{path}")
expr = unreal.MaterialEditingLibrary.create_material_expression(copy, unreal.MaterialExpressionConstant, -400, 0) if copy else None
if expr:
    expr.set_editor_property("r", {mark})
result = {{"copy": copy.get_path_name() if copy else None, "added": bool(expr),
          "expressions": unreal.MaterialEditingLibrary.get_num_material_expressions(copy) if copy else 0}}
'''

CLOSE = r'''
import unreal
asset = unreal.load_asset("%s")
unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(asset)
result = {"closed": True}
''' % BASIC


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def nodes_of(doc):
    return [n for g in (doc or {}).get('graphs') or [] for n in g.get('nodes') or []]


def node_ids(doc):
    return [((n.get('ids') or {}).get('short'), (n.get('ids') or {}).get('guid')) for n in nodes_of(doc)]


def has_mark(doc):
    for n in nodes_of(doc):
        if 'Constant' in str(n.get('class') or n.get('node_class') or n.get('nodeClass') or '') and \
                str(MARK) in json.dumps(n):
            return True
    return False


def read(p, path=None):
    status, text, err = p.tool('get-material-n2cjson', {'materialPath': path} if path else {})
    try:
        doc = strip_mark(json.loads(text)) if not err else {}
    except ValueError:
        doc = {}
    return doc, text, err


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27189)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    stem = 'live_material_focus_0463_%s' % args.label
    log_path = os.path.join(log_dir, stem + '_editor.log')
    report_path = os.path.join(log_dir, stem + '.json')
    mcp_config_path = os.path.join(log_dir, stem + '_mcp.json')
    port = free_port(args.port)
    report = {'label': args.label, 'port': port}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            saved, text, err = read(p, BASIC)
            report['saved'] = {'nodes': len(nodes_of(saved)), 'target': saved.get('target'), 'error': text[:200] if err else None}
            checks['by path: the saved asset'] = not err and len(nodes_of(saved)) > 0 and (saved.get('target') or {}).get('workingCopy') is False

            _, text, err = read(p)
            report['no_editor'] = text[:200]
            checks['no Material Editor: NO_FOCUSED_MATERIAL'] = err and text.startswith('NO_FOCUSED_MATERIAL')

            data, text = p.python(OPEN)
            report['open'] = data or text
            focus, text, err = read(p)
            target = focus.get('target') or {}
            report['focus'] = {'nodes': len(nodes_of(focus)), 'target': target, 'error': text[:200] if err else None,
                               'marked': text.lstrip().startswith('{"untrusted_content"')}
            checks['focus: resolvedBy focus, workingCopy true, the asset path, marked'] = not err and \
                target.get('resolvedBy') == 'focus' and target.get('workingCopy') is True and \
                str(target.get('assetPath', '')).startswith(BASIC) and report['focus']['marked']
            checks['focus: node ids equal the saved asset\'s'] = bool(node_ids(saved)) and node_ids(focus) == node_ids(saved)

            copy_path = target.get('workingCopyPath') or ''
            data, text = p.python(EDIT.format(path=copy_path, mark=MARK))
            report['edit'] = data or text
            checks['run-python: a Constant added to the working copy'] = data.get('added') is True

            edited, _, err = read(p)
            by_path, _, err2 = read(p, BASIC)
            report['after_edit'] = {'focus_nodes': len(nodes_of(edited)), 'focus_mark': has_mark(edited),
                                    'path_nodes': len(nodes_of(by_path)), 'path_mark': has_mark(by_path)}
            checks['focus sees the unapplied edit'] = not err and len(nodes_of(edited)) == len(nodes_of(saved)) + 1 and has_mark(edited)
            checks['by path does not see it'] = not err2 and node_ids(by_path) == node_ids(saved) and not has_mark(by_path)

            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            run = run_cli(PROMPT, mcp_config_path, args.cli_model, args.timeout, os.path.join(log_dir, stem + '_cli.jsonl'))
            report['cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                             'answer': run['answer'], 'exit': run['exit'], 'seconds': run['seconds']}
            ours = [c for c in run['calls'] if c['tool'] != 'ToolSearch']
            reads = [c for c in ours if c['tool'] == 'get-material-n2cjson' and not c['error']]
            checks['claude -p: one focus read and one path read'] = any(not c['input'].get('materialPath') for c in reads) and \
                any(str(c['input'].get('materialPath', '')).startswith(BASIC) for c in reads)
            answer = run['answer'] if isinstance(run['answer'], dict) else {}
            f_ans = answer.get('focus') or {}
            p_ans = answer.get('path') or {}
            checks['claude -p: the focus read shows the edit with workingCopy true'] = f_ans.get('workingCopy') is True and \
                f_ans.get('constant') is True and f_ans.get('nodes') == len(nodes_of(edited))
            checks['claude -p: the path read does not'] = p_ans.get('workingCopy') is False and p_ans.get('constant') is False \
                and p_ans.get('nodes') == len(nodes_of(saved))

            data, text = p.python(CLOSE)
            report['close'] = data or text
            _, text, err = read(p)
            checks['closed: NO_FOCUSED_MATERIAL again'] = err and text.startswith('NO_FOCUSED_MATERIAL')
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok'], 'after_edit': report.get('after_edit')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
