"""create-blueprint-class through IAssetTools and UBlueprintFactory, live through claude -p (card 0166).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET for a parallel lane):

    python -I live_create_blueprint_0166.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor. Then:

1. claude -p, with only mcp__nodetocode__* allowed, creates a Widget Blueprint from /Script/UMG.UserWidget in a fresh
   /Game/N2CTest/Create0166_<time> folder with create-blueprint-class, then repeats the same call.
2. The probe calls create-blueprint-class itself: an Actor Blueprint in that now existing folder, an AnimInstance
   parent, and the interface type with an Actor parent.
3. run-python reads the saved assets back.

Checks: the first agent call succeeds with saved true and assetClass WidgetBlueprint; the repeated call fails because
the asset exists; the Actor Blueprint is created in the existing folder; the AnimInstance parent and the
interface/Actor pair are refused; the Widget Blueprint loads as a WidgetBlueprint (its widget tree is not exposed to
Python; NodeToCode.MCP.CreateBlueprintClass checks it). Afterwards the folder is deleted. Report: logs/live_create_blueprint_0166_<label>.json.
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

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), call create-blueprint-class with blueprintName "MainMenu", '
    'parentClassPath "/Script/UMG.UserWidget", assetPath "%(folder)s" and openInEditor false. Then call '
    'create-blueprint-class a second time with exactly the same arguments. Do nothing else. Reply with only a JSON '
    'object {"firstAssetClass": <blueprintInfo.assetClass of the first result>, "secondFailed": <true or false>}.')

READBACK_PY = r'''
import unreal
folder = "%(folder)s"
widget = unreal.load_asset(folder + "/BP_MainMenu")
actor = unreal.load_asset(folder + "/BP_Door")
result = {
    "widget_class": widget.get_class().get_name() if widget else None,
    "actor_class": actor.get_class().get_name() if actor else None,
    "anim_exists": unreal.EditorAssetLibrary.does_asset_exist(folder + "/BP_Anim"),
    "iface_exists": unreal.EditorAssetLibrary.does_asset_exist(folder + "/BP_NotIface"),
}
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27198)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_create_blueprint_0166_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_create_blueprint_0166_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_create_blueprint_0166_%s_mcp.json' % args.label)

    port = free_port(args.port)
    folder = '/Game/N2CTest/Create0166_%s' % time.strftime('%H%M%S')
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
                # 1. The agent creates a Widget Blueprint, then repeats the call
                cli = run_cli(PROMPT % {'folder': folder}, mcp_config_path, args.cli_model, args.timeout,
                              os.path.join(log_dir, 'live_create_blueprint_0166_%s_cli.jsonl' % args.label))
                creates = [c for c in cli['calls'] if c['tool'] == 'create-blueprint-class']
                report['cli'] = {'calls': [(c['tool'], c['input'], c['error'], c['text'][:400]) for c in cli['calls']],
                                 'answer': cli['answer'], 'exit': cli['exit'], 'seconds': cli['seconds']}
                first = creates[0] if creates else None
                second = creates[1] if len(creates) > 1 else None
                checks['agent: first call made a saved Widget Blueprint'] = bool(first) and not first['error'] and \
                    (first['body'] or {}).get('saved') is True and \
                    ((first['body'] or {}).get('blueprintInfo') or {}).get('assetClass') == 'WidgetBlueprint'
                checks['agent: the repeated call failed because the asset exists'] = bool(second) and \
                    second['error'] and 'already exists' in second['text']
                checks['agent: its answer agrees'] = isinstance(cli['answer'], dict) and \
                    cli['answer'].get('firstAssetClass') == 'WidgetBlueprint' and cli['answer'].get('secondFailed') is True

                # 2. The probe's own calls
                base = {'assetPath': folder, 'openInEditor': False}
                status, text, err = p.tool('create-blueprint-class', dict(base, blueprintName='Door',
                                                                          parentClassPath='/Script/Engine.Actor'))
                report['door'] = text[:600]
                try:
                    door = json.loads(text)
                except ValueError:
                    door = {}
                checks['probe: Actor Blueprint in the existing folder'] = not err and door.get('saved') is True
                status, text, err = p.tool('create-blueprint-class', dict(base, blueprintName='Anim',
                                                                          parentClassPath='/Script/Engine.AnimInstance'))
                report['anim'] = text[:600]
                checks['probe: AnimInstance parent refused'] = err and 'skeleton' in text
                status, text, err = p.tool('create-blueprint-class', dict(base, blueprintName='NotIface',
                                                                          parentClassPath='/Script/Engine.Actor',
                                                                          blueprintType='Interface'))
                report['iface'] = text[:600]
                checks['probe: interface type with an Actor parent refused'] = err and 'CoreUObject.Interface' in text

                # 3. Read the assets back
                data, text = p.python(READBACK_PY % {'folder': folder})
                report['readback'] = data or text
                checks['readback: a WidgetBlueprint'] = data.get('widget_class') == 'WidgetBlueprint'
                checks['readback: the Actor Blueprint is a Blueprint'] = data.get('actor_class') == 'Blueprint'
                checks['readback: refused calls created nothing'] = data.get('anim_exists') is False and \
                    data.get('iface_exists') is False
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
