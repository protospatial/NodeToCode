"""Gate 0377: MCP translate, cancel and GC safety against a live provider, in a windowed editor.

Stdlib only; run with `python -I`, one engine at a time:

    python -I live_gate_0377.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts UnrealEditor.exe (windowed, not -Cmd, not -unattended) with only the provider's key from the repo's .env
added to its environment, and the provider and model written into the host's Saved/Config NodeToCode.ini for the
session (restored at the end). It makes an in-memory probe Blueprint under /Game/N2CTest (never saved), opens it, and
runs the card's checks:

  1. translate-focused-blueprint returns a translation with code;
  2. nodetocode/cancelTask sent --cancel-at seconds into a translation answers cancellation_initiated, the task's one
     response is the cancelled error, and the editor keeps answering /mcp/health for --watch seconds (default 180)
     with no crash line in the log;
  4. obj gc + obj list class=<service class> before, then --translations MCP translations, then obj gc + obj list
     again: the counts match;
  3. a toolbar translation while `obj gc` runs every 5 s: the probe starts a Slate post-tick timer that runs `obj gc`
     every --gc-interval seconds, writes logs/0377/ready_<label>, and waits for logs/0377/toolbar_done_<label>. In
     between, a person (or computer use) presses Translate in the N2C graph overlay (the Blueprint editor's toolbar
     translation; the plugin adds no Blueprint-toolbar button) and
     screenshots the result. The probe then stops the timer and checks the log: at least one GC ran while the
     request was in flight, the response was parsed, and no crash or ensure line.

The editor is closed after the in-memory probe Blueprint is deleted, so no save prompt blocks the exit. Key values
are never printed; the editor log and the report are scanned for them. Report: logs/0377/gate_<label>.json.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, ToolCallStream, call_tool,  # noqa: E402
                                  post, run_tool_call, stored_key_present, task_id_for_token, tool_text)
from live_translate_cancel import response_of  # noqa: E402

SERVICE_CLASS = {'anthropic': 'N2CAnthropicService', 'openai': 'N2COpenAIService', 'gemini': 'N2CGeminiService',
                 'deepseek': 'N2CDeepSeekService'}
PROVIDER_ENUM = {'anthropic': 'Anthropic', 'openai': 'OpenAI', 'gemini': 'Gemini', 'deepseek': 'DeepSeek'}
# Settings model entries for the toolbar translation (the toolbar uses Project Settings, not the MCP arguments)
MODEL_SETTING = {('anthropic', 'claude-haiku-5-5'): ('AnthropicModelId', 'claude-haiku-5-5'),
                 ('gemini', 'gemini-3.5-flash-lite'): ('GeminiModelId', 'gemini-3.5-flash-lite')}

CRASH_RE = re.compile(r'Unhandled Exception|Fatal error|EXCEPTION_ACCESS_VIOLATION|Assertion failed|'
                      r'Ensure condition failed')

# A console command run through the editor, bracketed by log markers so its output can be found in the log
CONSOLE_PY = r'''
import unreal
unreal.log("N2C0377 BEGIN %(mark)s")
unreal.SystemLibrary.execute_console_command(None, "%(cmd)s")
unreal.log("N2C0377 END %(mark)s")
result = "ran"
'''

# Runs `obj gc` every interval seconds from a Slate post-tick callback (the game thread, like a typed console command)
GC_TIMER_START_PY = r'''
import sys, time, unreal
state = {"last": 0.0, "count": 0}
def _n2c0377_tick(dt):
    now = time.time()
    if now - state["last"] >= %(interval)f:
        state["last"] = now
        state["count"] += 1
        unreal.log("N2C0377 GC %%d" %% state["count"])
        unreal.SystemLibrary.execute_console_command(None, "obj gc")
state["handle"] = unreal.register_slate_post_tick_callback(_n2c0377_tick)
sys.n2c0377_gc = state
result = "started"
'''

GC_TIMER_STOP_PY = r'''
import sys, unreal
state = getattr(sys, "n2c0377_gc", None)
if state:
    unreal.unregister_slate_post_tick_callback(state["handle"])
    del sys.n2c0377_gc
result = {"gc_runs": state["count"] if state else -1}
'''

FOCUS_PY = r'''
import json, unreal
result = json.loads(unreal.N2CPythonBridge.open_blueprint("%(path)s", "EventGraph"))
'''

# Closes the probe Blueprint's editor and deletes the in-memory asset (it was never saved, so nothing is left to ask
# about at exit), then asks the editor to quit
QUIT_PY = r'''
import unreal
steps = {}
try:
    asset = unreal.load_asset("%(path)s")
    if asset:
        steps["closed"] = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(asset)
        steps["deleted"] = unreal.EditorAssetLibrary.delete_loaded_asset(asset)
        asset = None
except Exception as e:
    steps["error"] = repr(e)
unreal.SystemLibrary.quit_editor()
result = steps
'''


class WindowedEditor(EditorSession):
    """UnrealEditor.exe with a window. The GUI exe has no useful stdout, so the log goes to -abslog."""

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self.exe = os.path.join(os.path.dirname(self.exe), 'UnrealEditor.exe')
        self.abslog = os.path.splitext(self.log_path)[0] + '.ue.log'

    def args(self):
        a = [x for x in super().args() if x not in ('-nullrhi', '-unattended', '-stdout', '-FullStdOutLogOutput')]
        return a + ['-abslog=%s' % self.abslog]

    def log_text(self):
        try:
            raw = open(self.abslog, 'rb').read()
        except OSError:
            return ''
        return raw.replace(b'\x00', b'').decode('utf-8', 'replace')


def run_py(port, code, rid):
    status, response = call_tool(port, 'run-python', {'code': code}, rid)
    text = tool_text(response)
    try:
        return json.loads(text).get('data')
    except (ValueError, AttributeError):
        return text or response


def health(port):
    try:
        with urllib.request.urlopen('http://127.0.0.1:%d/mcp/health' % port, timeout=5) as r:
            return r.status == 200
    except Exception:
        return False


def console(session, port, cmd, rid):
    """Runs a console command and returns its log output (the lines between the markers)"""
    mark = '%d_%d' % (rid, int(time.time() * 1000))
    run_py(port, CONSOLE_PY % {'mark': mark, 'cmd': cmd}, rid)
    for _ in range(20):
        text = session.log_text()
        b, e = text.find('LogPython: N2C0377 BEGIN %s' % mark), text.find('LogPython: N2C0377 END %s' % mark)
        if 0 <= b < e:
            return text[b:e]
        time.sleep(0.5)
    return ''


def obj_count(session, port, service_class, rid):
    out = console(session, port, 'obj list class=%s' % service_class, rid)
    m = re.search(r'(\d+) Objects \(Total', out)
    rows = [l for l in out.splitlines() if service_class in l and 'obj list' not in l.lower() and 'Objects (' not in l]
    return {'count': int(m.group(1)) if m else None, 'rows': [r.split('LogObj', 1)[-1].strip()[:160] for r in rows[:30]]}


def translate(port, arguments, rid, timeout):
    """One MCP translation run to the end: seconds, graphs, code characters and whether it was an error"""
    t0 = time.time()
    events = run_tool_call(port, 'translate-focused-blueprint', arguments, rid,
                           meta={'progressToken': 'gate0377-%d-%d' % (rid, int(t0))}, timeout=timeout)
    if not events:
        return {'started': False, 'head': 'no response'}
    final = [d for e, d in events if e == 'response']
    message, text = response_of(final[-1]) if final else ({}, '')
    try:
        translation = json.loads(text)
    except ValueError:
        translation = None
    graphs = (translation.get('graphs') or []) if isinstance(translation, dict) else []
    code_chars = sum(len((g.get('code') or {}).get('graphImplementation', '') or '')
                     + len((g.get('code') or {}).get('graphDeclaration', '') or '') for g in graphs if isinstance(g, dict))
    return {'seconds': round(time.time() - t0, 1), 'graphs': len(graphs), 'code_chars': code_chars,
            'isError': bool((message.get('result') or {}).get('isError')), 'timed_out': any(e == 'timeout' for e, _ in events),
            'head': '' if code_chars else text[:300]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--cancel-at', type=float, default=2.0)
    ap.add_argument('--watch', type=float, default=180.0, help='seconds the editor must stay up after the cancel')
    ap.add_argument('--translations', type=int, default=10)
    ap.add_argument('--gc-interval', type=float, default=5.0)
    ap.add_argument('--toolbar-wait', type=float, default=1800.0, help='seconds to wait for the toolbar_done marker')
    ap.add_argument('--skip-toolbar', action='store_true')
    ap.add_argument('--toolbar-only', action='store_true', help='run check 3 only (a separate session)')
    ap.add_argument('--timeout', type=int, default=300, help='bound on each translation')
    ap.add_argument('--port', type=int, default=27200)
    args = ap.parse_args()

    out_dir = os.path.join(HERE, 'logs', '0377')
    os.makedirs(out_dir, exist_ok=True)
    log_path = os.path.join(out_dir, 'gate_%s_editor.log' % args.label)
    report_path = os.path.join(out_dir, 'gate_%s.json' % args.label)
    ready_marker = os.path.join(out_dir, 'ready_%s' % args.label)
    done_marker = os.path.join(out_dir, 'toolbar_done_%s' % args.label)
    for p in (ready_marker, done_marker):
        if os.path.exists(p):
            os.remove(p)

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % STORE_ID[args.provider]}))
        return 2

    port = free_port(args.port)
    service_class = SERVICE_CLASS[args.provider]
    report = {'label': args.label, 'engine': args.engine, 'provider': args.provider, 'model': args.model,
              'service_class': service_class, 'port': port, 'windowed': True}
    checks = {}
    arguments = {'provider': args.provider, 'model': args.model, 'language': args.language}

    s = WindowedEditor(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys)
    # The toolbar translation reads Project Settings: provider and model for this session only (restored by stop())
    s._patch('NodeToCode.ini', '[/Script/NodeToCode.N2CSettings]', 'Provider', PROVIDER_ENUM[args.provider])
    # Debug logs the toolbar's 'ExecuteCollectNodesForEditor called' line, the start of the toolbar request
    s._patch('NodeToCode.ini', '[/Script/NodeToCode.N2CSettings]', 'MinSeverity', 'Debug')
    if (args.provider, args.model) in MODEL_SETTING:
        name, value = MODEL_SETTING[(args.provider, args.model)]
        s._patch('NodeToCode.ini', '[/Script/NodeToCode.N2CSettings]', name, value)
    try:
        t_start = time.time()
        report['started'] = s.start(timeout=900)
        report['start_seconds'] = round(time.time() - t_start, 1)
        print('started=%s in %ss pid=%s port=%d' % (report['started'], report['start_seconds'], s.proc.pid, port), flush=True)
        if not report['started']:
            raise RuntimeError('editor did not start')
        name = 'BP_N2CGate0377_%s' % time.strftime('%H%M%S')
        asset_path = '%s/%s' % (ASSET_DIR, name)
        report['asset'] = asset_path
        report['setup'] = run_py(port, SETUP_PY % {'dir': ASSET_DIR, 'name': name}, 1)
        print('setup', json.dumps(report['setup']), flush=True)

        if not args.toolbar_only:
            # 1. A translation returns code
            report['check1'] = translate(port, arguments, 10, args.timeout)
            checks['1_translation_returns_code'] = report['check1'].get('code_chars', 0) > 0 and not report['check1'].get('isError')
            print('check1', json.dumps(report['check1']), flush=True)

            # 2. Cancel --cancel-at seconds in, then the editor stays up for --watch seconds
            token = 'gate0377-cancel-%d' % int(time.time())
            stream = ToolCallStream(port, 'translate-focused-blueprint', arguments, 20, meta={'progressToken': token})
            c2 = {}
            time.sleep(max(0.0, args.cancel_at - (time.time() - stream.t0)))
            c2['responses_before_cancel'] = len([e for e in stream.events if e[1] == 'response'])
            c2['cancel_sent_at'] = round(time.time() - stream.t0, 2)
            status, body = post(port, {'jsonrpc': '2.0', 'id': 21, 'method': 'nodetocode/cancelTask',
                                       'params': {'progressToken': token}})
            c2['cancel'] = json.loads(body).get('result')
            watch_start = time.time()
            samples, down = 0, []
            while time.time() - watch_start < args.watch:
                samples += 1
                if not health(port) or s.proc.poll() is not None:
                    down.append(round(time.time() - watch_start, 1))
                time.sleep(5.0)
            c2['watch_seconds'] = round(time.time() - watch_start, 1)
            c2['health_samples'] = samples
            c2['health_failures_at'] = down
            status, body = post(port, {'jsonrpc': '2.0', 'id': 22, 'method': 'nodetocode/cancelTask',
                                       'params': {'progressToken': token}})
            c2['second_cancel'] = json.loads(body).get('result')
            stream.close()
            responses = [e for e in stream.events if e[1] == 'response']
            c2['responses'] = [{'at': t, 'text': response_of(d)[1][:160],
                                'isError': bool((response_of(d)[0].get('result') or {}).get('isError'))} for t, _, d in responses]
            first = responses[0][0] if responses else None
            c2['events_after_response'] = len([e for e in stream.events if first is not None and e[0] > first])
            log_now = s.log_text()
            tid = c2['task_id'] = task_id_for_token(log_now, token) or 'unknown'
            c2['log_task_completed_lines'] = len(re.findall(r'Async task %s completed' % tid, log_now, re.I))
            c2['log_llm_request_cancelled'] = log_now.count('LLM request cancelled')
            c2['log_crash_lines'] = len(CRASH_RE.findall(log_now))
            checks['2_cancel_initiated'] = (c2['cancel'] or {}).get('status') == 'cancellation_initiated'
            checks['2_cancel_before_result'] = c2['responses_before_cancel'] == 0
            checks['2_one_cancelled_response'] = len(responses) == 1 and c2['responses'][0]['isError'] \
                and 'cancelled' in c2['responses'][0]['text']
            checks['2_no_late_events'] = c2['events_after_response'] == 0
            checks['2_editor_up_for_watch'] = not down and c2['watch_seconds'] >= args.watch - 1
            checks['2_second_cancel_already_completed'] = (c2['second_cancel'] or {}).get('status') == 'task_already_completed'
            checks['2_log_task_completed_once'] = c2['log_task_completed_lines'] == 1
            checks['2_log_no_crash'] = c2['log_crash_lines'] == 0
            report['check2'] = c2
            print('check2', json.dumps(c2), flush=True)

            # 4. Service object count before and after N translations, each after obj gc
            console(s, port, 'obj gc', 30)
            time.sleep(1.0)
            c4 = {'before': obj_count(s, port, service_class, 31), 'runs': []}
            for i in range(args.translations):
                r = translate(port, arguments, 40 + i, args.timeout)
                c4['runs'].append({k: r.get(k) for k in ('seconds', 'code_chars', 'isError', 'timed_out')})
                print('check4 run %d' % (i + 1), json.dumps(c4['runs'][-1]), flush=True)
            c4['after_no_gc'] = obj_count(s, port, service_class, 60)
            console(s, port, 'obj gc', 61)
            time.sleep(1.0)
            c4['after'] = obj_count(s, port, service_class, 62)
            c4['translations_with_code'] = sum(1 for r in c4['runs'] if r.get('code_chars'))
            checks['4_all_translations_returned_code'] = c4['translations_with_code'] == args.translations
            checks['4_count_unchanged'] = c4['before']['count'] is not None and c4['before']['count'] == c4['after']['count']
            report['check4'] = c4
            print('check4', json.dumps({k: v for k, v in c4.items() if k != 'runs'}), flush=True)

        # 3. Toolbar translation with obj gc every interval seconds
        if not args.skip_toolbar:
            report['focus'] = run_py(port, FOCUS_PY % {'path': asset_path}, 70)
            gc_mark = len(s.log_text())
            report['gc_timer'] = run_py(port, GC_TIMER_START_PY % {'interval': args.gc_interval}, 71)
            open(ready_marker, 'w').write(asset_path)
            print('READY for toolbar translation: press Translate in the N2C graph overlay of %s, then create %s'
                  % (asset_path, done_marker), flush=True)
            wait_start = time.time()
            while not os.path.exists(done_marker) and time.time() - wait_start < args.toolbar_wait:
                if s.proc.poll() is not None:
                    break
                time.sleep(1.0)
            c3 = {'waited_seconds': round(time.time() - wait_start, 1), 'done_marker': os.path.exists(done_marker),
                  'editor_alive': s.proc.poll() is None and health(port)}
            if c3['editor_alive']:
                c3.update(run_py(port, GC_TIMER_STOP_PY, 72) or {})
            seg = s.log_text()[gc_mark:]
            # Timeline of the toolbar request inside the GC segment: GC ticks before, during and after
            gc_pos = [m.start() for m in re.finditer(r'N2C0377 GC \d+', seg)]
            # The graph overlay's Translate button (the editor's toolbar translation) logs RequestOverlayTranslation;
            # the older Collect path logged ExecuteCollectNodesForEditor
            starts = [p for p in (seg.find('RequestOverlayTranslation'), seg.find('ExecuteCollectNodesForEditor called'),
                                  seg.find('Node to Code window shown')) if p >= 0]
            start_pos = min(starts) if starts else -1
            parsed_pos = seg.find('Successfully parsed LLM response')
            c3['gc_lines'] = len(gc_pos)
            c3['toolbar_started'] = start_pos >= 0
            c3['response_parsed'] = parsed_pos >= 0
            c3['gc_during_request'] = len([p for p in gc_pos if start_pos >= 0 and start_pos < p < (parsed_pos if parsed_pos >= 0 else len(seg))])
            c3['parse_failed_lines'] = seg.count('Failed to parse LLM response')
            c3['crash_lines'] = len(CRASH_RE.findall(seg))
            c3['toolbar_lines'] = [l[:120] for l in seg.splitlines()
                                   if 'ExecuteCollectNodesForEditor called' in l or 'RequestOverlayTranslation' in l
                                   or 'Successfully parsed LLM response' in l][:4]
            checks['3_toolbar_translation_parsed'] = c3['toolbar_started'] and c3['response_parsed'] and c3['parse_failed_lines'] == 0
            checks['3_gc_ran_during_request'] = c3['gc_during_request'] >= 1
            checks['3_no_crash'] = c3['editor_alive'] and c3['crash_lines'] == 0
            report['check3'] = c3
            print('check3', json.dumps(c3), flush=True)

        checks['editor_alive_at_end'] = s.proc.poll() is None and health(port)
        # Close the Blueprint editor, mark the package clean and quit, so the exit is the normal one
        if checks['editor_alive_at_end']:
            report['quit'] = run_py(port, QUIT_PY % {'path': asset_path}, 90)
            try:
                report['exit_code'] = s.proc.wait(120)
            except Exception:
                report['exit_code'] = None
            checks['editor_exited_cleanly'] = report['exit_code'] == 0
    except Exception as e:  # report what happened; the finally block stops the editor and restores config
        report['error'] = repr(e)
    finally:
        s.stop()

    log_text = s.log_text()
    report['log'] = {'crash_lines': len(CRASH_RE.findall(log_text)), 'path': s.abslog,
                     'context_not_found': log_text.count('context not found'),
                     'completed_multiple_times': log_text.count('attempted to complete multiple times')}
    checks['log_no_crash_or_ensure'] = report['log']['crash_lines'] == 0
    checks['log_no_double_completion'] = report['log']['completed_multiple_times'] == 0
    report['checks'] = checks
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [log_path, s.abslog, report_path, out_dir])
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(checks) and all(checks.values()) and not leaks and 'error' not in report
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'ok': report['ok'], 'checks': checks, 'leaks': report['leaks'], 'error': report.get('error')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
