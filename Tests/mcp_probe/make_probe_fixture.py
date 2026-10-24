"""Rebuild the committed probe fixture Tests/Fixtures/N2CProbe/BP_N2CProbe.uasset (card 0097).

Stdlib only; run with `python -I`, on UE 5.6 so 5.7 and 5.8 can load the result:

    python -I make_probe_fixture.py --engine G:/UE/UE_5.6 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject"

On a headless editor (editor_session.py, -RenderOffScreen) it creates /Game/N2CProbe/BP_N2CProbe (parent Actor) with
run-python, adds Event BeginPlay and Print String to its EventGraph with add-bp-node-to-active-graph, wires
BeginPlay.then -> PrintString.execute with connect-pins, sets In String to "N2CProbe BeginPlay", compiles, saves,
quits, copies the .uasset to Tests/Fixtures/N2CProbe/ and deletes the host copy. mcp_probe.py copies it back into the
host's Content/N2CProbe/ for the content probes (P10-P12) and removes it afterwards.
"""
import argparse
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
import e05_regressions as e05  # noqa: E402

FIXTURE_DIR = os.path.join(os.path.dirname(HERE), 'Fixtures', 'N2CProbe')
ASSET = '/Game/N2CProbe/BP_N2CProbe'

CREATE_PY = r'''
import json, unreal
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("BP_N2CProbe", "/Game/N2CProbe", unreal.Blueprint, factory)
result = {"created": bp is not None}
if bp:
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    result["opened"] = json.loads(unreal.N2CPythonBridge.open_blueprint("%s", "EventGraph")).get("success")
''' % ASSET

SAVE_PY = r'''
import json, unreal
bp = unreal.load_asset("%s")
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
saved = unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
raw = json.loads(unreal.N2CPythonBridge.get_focused_blueprint_json())
result = {"saved": saved, "n2c": raw.get("data", {}).get("n2c_json")}
''' % ASSET


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    args = ap.parse_args()
    content = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Content', 'N2CProbe')
    if os.path.exists(content):
        raise SystemExit('%s exists; remove it first' % content)
    port = free_port(27190)
    session = EditorSession(args.engine, args.project, port, os.path.join(HERE, 'logs', 'make_probe_fixture.log'),
                            allow_python=True, extra_args=['-RenderOffScreen'])
    url = 'http://127.0.0.1:%d/mcp' % port
    try:
        if not session.start():
            raise SystemExit('editor did not start')
        steps, text = e05.run_python(url, CREATE_PY)
        print('create', steps, text if not steps else '')
        begin = e05.add_node(url, 'BeginPlay', 'Event', 'ReceiveBeginPlay', 0)
        printer = e05.add_node(url, 'Print String', 'CallFunction', 'PrintString', 400)
        print('nodes', json.dumps(begin), json.dumps(printer))
        data, error, text = e05.tool_json(url, 'connect-pins', {'connections': [e05.link(begin, 'then', printer, 'execute')]})
        print('connect', error, text[:300])
        data, error, text = e05.tool_json(url, 'set-input-pin-value', {
            'nodeGuid': printer['guid'], 'pinGuid': printer['in']['InString'], 'value': 'N2CProbe BeginPlay'})
        print('set value', error, text[:200])
        steps, text = e05.run_python(url, SAVE_PY)
        print('save', json.dumps(steps)[:3000], text if not steps else '')
        try:
            e05.run_python(url, 'import unreal\nunreal.SystemLibrary.quit_editor()\nresult = {}')
        except Exception:
            pass
        deadline = time.time() + 60
        while time.time() < deadline and session.proc.poll() is None:
            time.sleep(0.5)
    finally:
        session.stop()
    src = os.path.join(content, 'BP_N2CProbe.uasset')
    if not os.path.isfile(src):
        raise SystemExit('no %s' % src)
    os.makedirs(FIXTURE_DIR, exist_ok=True)
    shutil.copy2(src, os.path.join(FIXTURE_DIR, 'BP_N2CProbe.uasset'))
    shutil.rmtree(content)
    print('fixture written to', FIXTURE_DIR)
    return 0


if __name__ == '__main__':
    sys.exit(main())
