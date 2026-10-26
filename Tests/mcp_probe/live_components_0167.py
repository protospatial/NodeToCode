"""Component add and delete through USubobjectDataSubsystem, live through claude -p (card 0167).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET for a parallel lane):

    python -I live_components_0167.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint in memory under a fresh /Game/N2CTest/Components0167_<time>
folder (never saved). Then:

1. claude -p, with only mcp__nodetocode__* allowed, by blueprintPath: adds a SpringArm CameraBoom, a Camera
   FollowCamera under CameraBoom, a SceneComponent Lens under FollowCamera (a parent that is not the root), a
   RotatingMovement Spin (not a scene component), lists the components, deletes CameraBoom keeping its children,
   then deletes FollowCamera with its children.
2. The probe checks each call's result (attachedTo, compile success, the children's new parent) and lists the
   components after the agent is done.
3. The probe undoes the last two steps (TRANSACTION UNDO through run-python), compiles and lists after each: Lens
   under FollowCamera again, then CameraBoom with FollowCamera under it.

Checks: every agent call succeeded and compiled without errors; FollowCamera was attached to CameraBoom and Lens to
FollowCamera, also in the agent's list; Spin was attached to nothing; deleting CameraBoom moved FollowCamera to
DefaultSceneRoot; deleting FollowCamera with its children removed Lens too; the final list holds DefaultSceneRoot and
Spin only; each undo restores the hierarchy. Afterwards the folder is deleted. Report:
logs/live_components_0167_<label>.json.
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

NAME = 'BP_N2CComponents0167'

SETUP_PY = r'''
import unreal
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", "%(folder)s", unreal.Blueprint, factory)
result = {"created": bp is not None, "path": bp.get_path_name() if bp else None}
'''

UNDO_PY = r'''
import unreal
unreal.SystemLibrary.execute_console_command(None, "TRANSACTION UNDO")
result = {"undone": True}
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), work on the Actor Blueprint "%(bp)s", passing it as '
    'blueprintPath to every call. In this order: (1) add-component-class-to-actor with componentClass '
    '"/Script/Engine.SpringArmComponent" and componentName "CameraBoom"; (2) add-component-class-to-actor with '
    'componentClass "/Script/Engine.CameraComponent", componentName "FollowCamera" and parentComponent "CameraBoom"; '
    '(3) add-component-class-to-actor with componentClass "/Script/Engine.SceneComponent", componentName "Lens" and '
    'parentComponent "FollowCamera"; (4) add-component-class-to-actor with componentClass '
    '"/Script/Engine.RotatingMovementComponent" and componentName "Spin"; (5) list-components-in-actor; (6) '
    'delete-component-in-actor with componentName "CameraBoom" and deleteChildren false; (7) delete-component-in-actor '
    'with componentName "FollowCamera" and deleteChildren true. Do nothing else. Reply with only a JSON object '
    '{"followCameraAttachedTo": <attachedTo from call 2>, "lensAttachedTo": <attachedTo from call 3>, '
    '"followCameraNewParent": <newParent of FollowCamera in call 6\'s affectedChildren>, "deletedWithFollowCamera": '
    '[<names in call 7\'s affectedChildren>]}.')


def walk(components, parent=None):
    """(name, parentName) for every component in a list-components-in-actor tree"""
    for c in components or []:
        yield c.get('name'), parent
        yield from walk(c.get('children'), c.get('name'))


def hierarchy(p, bp):
    status, text, err = p.tool('list-components-in-actor', {'blueprintPath': bp})
    try:
        body = json.loads(text)
    except ValueError:
        return None, text[:400]
    return dict(walk(body.get('components'))), body


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27199)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_components_0167_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_components_0167_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_components_0167_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Components0167_%s' % time.strftime('%H%M%S')
    bp = '%s/%s' % (folder, NAME)
    report = {'label': args.label, 'port': port, 'folder': folder}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            try:
                data, text = p.python(SETUP_PY % {'name': NAME, 'folder': folder})
                report['setup'] = data or text
                checks['setup: Blueprint created'] = data.get('created') is True
                if checks['setup: Blueprint created']:
                    # 1. The agent's seven calls
                    cli = run_cli(PROMPT % {'bp': bp}, mcp_config_path, args.cli_model, args.timeout,
                                  os.path.join(log_dir, 'live_components_0167_%s_cli.jsonl' % args.label))
                    calls = cli['calls']
                    report['cli'] = {'calls': [(c['tool'], c['input'], c['error'], c['text'][:600]) for c in calls],
                                     'answer': cli['answer'], 'exit': cli['exit'], 'seconds': cli['seconds']}
                    adds = [c for c in calls if c['tool'] == 'add-component-class-to-actor']
                    deletes = [c for c in calls if c['tool'] == 'delete-component-in-actor']
                    lists = [c for c in calls if c['tool'] == 'list-components-in-actor']
                    by_name = {(c['input'] or {}).get('componentName'): c for c in adds}

                    def compiled(c):
                        body = (c or {}).get('body') or {}
                        return bool(c) and not c['error'] and (body.get('compile') or {}).get('errorCount') == 0

                    checks['agent: four adds and two deletes, each compiled without errors'] = \
                        len(adds) == 4 and len(deletes) == 2 and all(compiled(c) for c in adds + deletes)
                    checks['agent: CameraBoom attached to DefaultSceneRoot'] = \
                        ((by_name.get('CameraBoom') or {}).get('body') or {}).get('attachedTo') == 'DefaultSceneRoot'
                    checks['agent: FollowCamera attached to CameraBoom'] = \
                        ((by_name.get('FollowCamera') or {}).get('body') or {}).get('attachedTo') == 'CameraBoom'
                    checks['agent: Lens attached to FollowCamera'] = \
                        ((by_name.get('Lens') or {}).get('body') or {}).get('attachedTo') == 'FollowCamera'
                    spin = (by_name.get('Spin') or {}).get('body') or {}
                    checks['agent: Spin (not a scene component) attached to nothing'] = \
                        bool(spin) and 'attachedTo' not in spin and spin.get('isSceneComponent') is False
                    listed = dict(walk(((lists[0] if lists else {}).get('body') or {}).get('components')))
                    checks['agent: its list shows the tree'] = listed.get('CameraBoom') == 'DefaultSceneRoot' and \
                        listed.get('FollowCamera') == 'CameraBoom' and listed.get('Lens') == 'FollowCamera' and \
                        'Spin' in listed and listed.get('Spin') is None
                    first = (deletes[0] if deletes else {}).get('body') or {}
                    moved = {c.get('name'): c.get('newParent') for c in first.get('affectedChildren') or []}
                    checks['agent: deleting CameraBoom moved FollowCamera to DefaultSceneRoot'] = \
                        moved == {'FollowCamera': 'DefaultSceneRoot'}
                    second = (deletes[1] if len(deletes) > 1 else {}).get('body') or {}
                    gone = {c.get('name'): c.get('action') for c in second.get('affectedChildren') or []}
                    checks['agent: deleting FollowCamera with its children deleted Lens'] = gone == {'Lens': 'deleted'}
                    answer = cli['answer'] if isinstance(cli['answer'], dict) else {}
                    checks['agent: its answer agrees'] = answer.get('followCameraAttachedTo') == 'CameraBoom' and \
                        answer.get('lensAttachedTo') == 'FollowCamera' and \
                        answer.get('followCameraNewParent') == 'DefaultSceneRoot' and \
                        answer.get('deletedWithFollowCamera') == ['Lens']

                    # 2. The probe's own list
                    tree, body = hierarchy(p, bp)
                    report['final'] = tree
                    checks['probe: DefaultSceneRoot and Spin are left'] = tree == {'DefaultSceneRoot': None, 'Spin': None}

                    # 3. Undo the two deletes, compiling and listing after each
                    report['undo'] = []
                    for step, expected in (
                            ('undo delete FollowCamera', {'DefaultSceneRoot': None, 'FollowCamera': 'DefaultSceneRoot',
                                                          'Lens': 'FollowCamera', 'Spin': None}),
                            ('undo delete CameraBoom', {'DefaultSceneRoot': None, 'CameraBoom': 'DefaultSceneRoot',
                                                        'FollowCamera': 'CameraBoom', 'Lens': 'FollowCamera',
                                                        'Spin': None})):
                        p.python(UNDO_PY)
                        status, text, err = p.tool('compile-blueprint', {'blueprintPath': bp})
                        tree, body = hierarchy(p, bp)
                        report['undo'].append({'step': step, 'compile': text[:300], 'tree': tree})
                        checks['probe: %s restores the hierarchy' % step] = tree == expected
            finally:
                data, text = p.python(CLEANUP_PY % {'folder': folder})
                report['cleanup'] = data or text
                checks['cleanup: folder deleted'] = data.get('exists_after') is False

    report['checks'] = checks
    report['pass'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    for name, ok in checks.items():
        print('%s  %s' % ('PASS' if ok else 'FAIL', name))
    print('report: %s' % report_path)
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
