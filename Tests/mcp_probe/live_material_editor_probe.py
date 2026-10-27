"""Open and close the Material Editor on BasicShapeMaterial in each headless session mode, and see which survive.

Stdlib only; run with `python -I`:

    python -I live_material_editor_probe.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" [--modes ...]

Card 0458. Three modes per engine, each its own editor session (editor_session.py; the NodeToCode MCP server's
run-python tool drives it, so Allow MCP Python Execution is set for the session and restored afterwards):

- nullrhi: UnrealEditor-Cmd -nullrhi, no -RenderOffScreen (what build.ps1 -Test passes).
- nullrhi_offscreen: -nullrhi -RenderOffScreen (the headless live scripts' Blueprint-editor arguments).
- gpu_offscreen: no -nullrhi, -RenderOffScreen (a real RHI, no window shown).

In each mode it loads /Engine/BasicShapes/BasicShapeMaterial, opens it with AssetEditorSubsystem.open_editor_for_assets,
lets the editor tick for --settle seconds, then checks the editor answers MaterialEditingLibrary.get_material_selected_nodes
(that call finds the IMaterialEditor for the asset) and that a new UPreviewMaterial copy exists in the transient
package with the original's expression count. FMaterialEditor's GetMaterialInterface() returns that copy (UE 5.6-5.8
MaterialEditor.cpp, `return Material;`, the StaticDuplicateObject(..., UPreviewMaterial::StaticClass()) made in
InitEditorForMaterial), so its presence is what the card asks. Then it closes the editor with
close_all_editors_for_asset (it opened when that closes one editor), ticks again, checks the editor is still alive,
and closes again (it closed when no editor is left: 0); AssetEditorSubsystem's edited-asset list is not exposed to
Python. A mode fails when the editor process exits, the MCP health check stops answering, or the log has a Fatal
error, an access violation, an assertion or an unhandled exception. Report: logs/live_material_editor_probe_<ver>.json.
"""
import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_env_translation import call_tool, tool_text  # noqa: E402

MATERIAL = '/Engine/BasicShapes/BasicShapeMaterial'
MODES = ('nullrhi', 'nullrhi_offscreen', 'gpu_offscreen')
CRASH = re.compile(r'Fatal error|EXCEPTION_ACCESS_VIOLATION|Unhandled Exception|Assertion failed|'
                   r'appError called|Critical error', re.I)

# Runs in the editor: the UPreviewMaterial objects alive now (the Material Editor's working copies)
PREVIEWS_PY = r'''
import unreal
def previews():
    cls = getattr(unreal, "PreviewMaterial", None)
    out = []
    if cls is not None:
        for o in unreal.ObjectIterator(cls):
            out.append(o)
    return out
'''

OPEN_PY = PREVIEWS_PY + r'''
m = unreal.load_asset("%(material)s")
before = [p.get_path_name() for p in previews()]
sub = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
opened = sub.open_editor_for_assets([m]) if m else None
result = {"loaded": m is not None, "open_returned": opened, "previews_before": before,
          "preview_class_exposed": getattr(unreal, "PreviewMaterial", None) is not None,
          "original_expressions": unreal.MaterialEditingLibrary.get_num_material_expressions(m) if m else None}
'''

CHECK_PY = PREVIEWS_PY + r'''
m = unreal.load_asset("%(material)s")
sub = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
try:
    sel = unreal.MaterialEditingLibrary.get_material_selected_nodes(m)
    sel = len(sel)
except Exception as e:
    sel = "error: %%s" %% e
before = set(%(before)s)
new = []
for p in previews():
    path = p.get_path_name()
    if path in before:
        continue
    new.append({"path": path, "outer": p.get_outer().get_path_name() if p.get_outer() else None,
                "class": p.get_class().get_name(),
                "expressions": unreal.MaterialEditingLibrary.get_num_material_expressions(p)})
result = {"selected_nodes_call": sel, "new_previews": new}
'''

CLOSE_PY = r'''
import unreal
m = unreal.load_asset("%(material)s")
sub = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
closed = sub.close_all_editors_for_asset(m)
result = {"closed": closed}
'''

# A second close finds no editor left for the asset (0) when the first one closed it
AFTER_PY = CLOSE_PY


class ModeSession(EditorSession):
    def __init__(self, *a, mode='nullrhi', **kw):
        super().__init__(*a, **kw)
        self.mode = mode

    def args(self):
        a = super().args()
        if self.mode == 'gpu_offscreen':
            a = [x for x in a if x != '-nullrhi']
        if self.mode in ('nullrhi_offscreen', 'gpu_offscreen'):
            a.append('-RenderOffScreen')
        return a


def run_py(port, code, rid):
    st, r = call_tool(port, 'run-python', {'code': code}, rid)
    text = tool_text(r)
    is_error = bool((r.get('result') or {}).get('isError')) if isinstance(r, dict) else True
    try:
        parsed = json.loads(text)
    except ValueError:
        parsed = None
    return {'status': st, 'is_error': is_error, 'parsed': parsed, 'text': text[:1500]}


def result_of(step):
    """The script's `result` value (run-python returns it as `data`) or None"""
    p = step.get('parsed')
    if isinstance(p, dict):
        return p.get('data')
    return None


def crash_lines(log):
    return [l.strip()[:300] for l in log.splitlines() if CRASH.search(l)][:8]


def alive(s):
    return s.proc.poll() is None and s.health(timeout=30)


def run_mode(engine, project, mode, port, logdir, ver, settle):
    log_path = os.path.join(logdir, 'material_editor_%s_%s.log' % (ver, mode))
    s = ModeSession(engine, project, port, log_path, allow_python=True, mode=mode)
    rec = {'mode': mode, 'args': None, 'log': log_path}
    try:
        rec['args'] = [a for a in s.args()[2:]]
        t0 = time.time()
        rec['started'] = s.start(timeout=600)
        rec['seconds_to_health'] = round(time.time() - t0, 1)
        if not rec['started']:
            rec['exit_code'] = s.proc.poll()
            return rec
        o = run_py(port, OPEN_PY % {'material': MATERIAL}, 1)
        rec['open'] = o
        ores = result_of(o) or {}
        time.sleep(settle)
        rec['alive_after_open'] = alive(s)
        if rec['alive_after_open']:
            c = run_py(port, CHECK_PY % {'material': MATERIAL, 'before': json.dumps(ores.get('previews_before') or [])}, 2)
            rec['check'] = c
            cres = result_of(c) or {}
            news = cres.get('new_previews') or []
            rec['preview_copy'] = bool(news) and all(n.get('expressions') == ores.get('original_expressions') for n in news)
            x = run_py(port, CLOSE_PY % {'material': MATERIAL}, 3)
            rec['close'] = x
            rec['opens'] = (result_of(x) or {}).get('closed', 0) >= 1
            time.sleep(settle)
            rec['alive_after_close'] = alive(s)
            if rec['alive_after_close']:
                a = run_py(port, AFTER_PY % {'material': MATERIAL}, 4)
                rec['after'] = a
                rec['closes'] = (result_of(a) or {}).get('closed') == 0
        rec['exit_code'] = s.proc.poll()
    except Exception as e:  # report and go on to the next mode
        rec['error'] = '%s: %s' % (type(e).__name__, e)
    finally:
        s.stop()
        rec['restore_ok'] = s.restore_ok
        log = s.log_text()
        rec['crash_lines'] = crash_lines(log)
        rec['crash'] = bool(rec['crash_lines']) or (rec.get('exit_code') not in (None, 0))
        rec['survives'] = bool(rec.get('alive_after_open') and rec.get('alive_after_close') and not rec['crash_lines'])
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--modes', nargs='+', default=list(MODES), choices=MODES)
    ap.add_argument('--port', type=int, default=27230)
    ap.add_argument('--settle', type=float, default=20.0, help='seconds the editor ticks after open and after close')
    args = ap.parse_args()
    ver = os.path.basename(os.path.normpath(args.engine)).replace('UE_', '')
    logdir = os.path.join(HERE, 'logs')
    os.makedirs(logdir, exist_ok=True)
    report = {'engine': args.engine, 'project': args.project, 'material': MATERIAL, 'modes': []}
    out = os.path.join(logdir, 'live_material_editor_probe_%s.json' % ver)
    for mode in args.modes:
        port = free_port(args.port)
        rec = run_mode(args.engine, args.project, mode, port, logdir, ver, args.settle)
        report['modes'].append(rec)
        json.dump(report, open(out, 'w', encoding='utf-8'), indent=1)
        print('%s %s: started=%s opens=%s preview_copy=%s closes=%s survives=%s crash=%s' % (
            ver, mode, rec.get('started'), rec.get('opens'), rec.get('preview_copy'), rec.get('closes'),
            rec.get('survives'), rec.get('crash_lines') or rec.get('crash')))
    print('report: %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
