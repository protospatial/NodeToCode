"""Exit the editor while a live translate-focused-blueprint is in flight and check it shuts down cleanly.

Stdlib only; run with `python -I`:

    python -I live_translate_exit.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" \
        --provider anthropic --model claude-haiku-5-5

It starts a headless editor with only the provider's key (from the repo's .env) added to its environment, makes an
in-memory probe Blueprint under /Game/N2CTest (as live_env_translation.py does), starts a translation, reads its task
stream, and about --quit-after seconds in closes the Blueprint editor and asks the editor to quit
(unreal.SystemLibrary.quit_editor() over run-python, the normal exit path; use --windowed on UE 5.6 and 5.7). It passes when the editor exits by itself with code 0, in under 10 s (not waiting for the LLM),
the log
shows the exit cancelled the running task (OnEnginePreExit) before the module shut down and UObjects were purged, the
LLM request was cancelled and the task completed exactly once, no LLM response was handled
after the cancel, there is no crash, ensure or "context not found" line, the log reaches "Exiting.", and no key value
is in the editor log or the report. Key values are never printed. Report: logs/live_translate_exit.json.
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
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, call_tool,  # noqa: E402
                                  stored_key_present, tool_text)
from live_translate_cancel import TaskStream  # noqa: E402

# Closes the probe Blueprint's editor, then asks the editor to quit. The request is already in flight by then.
QUIT_PY = ('import unreal\n'
           'asset = unreal.load_asset("%(path)s")\n'
           'closed = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(asset) if asset else -1\n'
           'unreal.SystemLibrary.quit_editor()\n'
           'print("closed %%d editors, quit requested" %% closed)\n')


class WindowedSession(EditorSession):
    """The editor with a window (no -nullrhi). UE 5.6 and 5.7 cannot do this probe headless, with or without a
    translation: closing the Blueprint editor under -nullrhi fails GetRestoredDimensions, and on 5.6 exiting with it
    still open asserts PreviewScene.GetWorld(). Windowed, closing then quitting exits cleanly. UE 5.8 runs headless."""

    def args(self):
        return [a for a in super().args() if a != '-nullrhi']


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--quit-after', type=float, default=1.0)
    ap.add_argument('--exit-timeout', type=float, default=120.0)
    ap.add_argument('--port', type=int, default=27190)
    ap.add_argument('--windowed', action='store_true',
                    help='run the editor with a window (no -nullrhi); needed on UE 5.6 and 5.7, see WindowedSession')
    ap.add_argument('--log', default=os.path.join(HERE, 'logs', 'live_translate_exit_editor.log'))
    ap.add_argument('--report', default=os.path.join(HERE, 'logs', 'live_translate_exit.json'))
    args = ap.parse_args()

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % STORE_ID[args.provider]}))
        return 2

    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    port = free_port(args.port)
    report = {'provider': args.provider, 'model': args.model, 'port': port, 'quit_after': args.quit_after}
    checks = {}

    session_class = WindowedSession if args.windowed else EditorSession
    report['windowed'] = args.windowed
    with session_class(args.engine, args.project, port, args.log, allow_python=True, extra_env=keys) as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CExitProbe_%s' % time.strftime('%H%M%S')
            status, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = tool_text(setup)[:1000] or setup

            token = 'exit-probe-%d' % int(time.time())
            arguments = {'provider': args.provider, 'model': args.model, 'language': args.language}
            status, started = call_tool(port, 'translate-focused-blueprint', arguments, 2, meta={'progressToken': token})
            task_id = (started.get('result') or {}).get('taskId')
            report['task_id'] = task_id
            if task_id:
                stream = TaskStream(port, task_id)
                time.sleep(args.quit_after)
                report['responses_before_quit'] = len([e for e in stream.events if e[1] == 'response'])
                quit_at = time.time()
                status, quit_result = call_tool(port, 'run-python', {'code': QUIT_PY % {'path': '%s/%s' % (ASSET_DIR, name)}}, 3)
                report['quit_call'] = {'status': status, 'text': tool_text(quit_result)[:200]}
                try:
                    report['exit_code'] = s.proc.wait(args.exit_timeout)
                    report['exit_seconds'] = round(time.time() - quit_at, 1)
                except Exception:
                    report['exit_code'] = None
                stream.close()
                report['stream'] = {'events': [(t, e) for t, e, _ in stream.events], 'closed_at': stream.closed_at}

    log_text = s.log_text()
    if report.get('task_id'):
        tid = report['task_id'].replace('-', '').upper()
        # The cancel runs on OnEnginePreExit (start of UEngine::PreExit), before the module shuts down ("SSE server
        # stopped") and the UObject subsystem closes
        cancel_pos = log_text.find('Requested cancellation for all 1 running tasks')
        quit_pos = log_text.find('Cmd: QUIT_EDITOR')
        shutdown_pos = log_text.find('SSE server stopped')
        purge_pos = log_text.find('Object subsystem successfully closed')
        after_cancel = log_text[cancel_pos:] if cancel_pos >= 0 else ''
        report['log'] = {
            'cancel_all_line': cancel_pos >= 0,
            'cancel_before_purge': 0 <= quit_pos < cancel_pos and all(p < 0 or cancel_pos < p for p in (shutdown_pos, purge_pos)),
            'task_completed_lines': len(re.findall(r'Async task %s completed' % tid, log_text, re.I)),
            'llm_request_cancelled_lines': log_text.count('LLM request cancelled'),
            'llm_response_after_cancel': len(re.findall(r'Successfully parsed LLM response|Successfully saved translation', after_cancel)),
            'context_not_found': log_text.count('context not found'),
            'completed_multiple_times': log_text.count('attempted to complete multiple times'),
            'crash_lines': len(re.findall(r'Unhandled Exception|Fatal error|EXCEPTION_ACCESS_VIOLATION|Assertion failed',
                                          log_text)),
            'ensure_lines': len(re.findall(r'Ensure condition failed', log_text)),
            'exiting_line': 'Exiting.' in log_text,
        }
        checks['no_result_before_quit'] = report.get('responses_before_quit') == 0
        checks['editor_exited_cleanly'] = report.get('exit_code') == 0
        # Exit must not wait for the LLM (a flush of the HTTP manager at exit would otherwise wait for the request)
        checks['exit_without_waiting_for_llm'] = (report.get('exit_seconds') or 1e9) < 10.0
        checks['log_cancel_all_on_exit'] = report['log']['cancel_all_line']
        checks['log_cancel_before_purge'] = report['log']['cancel_before_purge']
        checks['log_task_completed_once'] = report['log']['task_completed_lines'] == 1
        checks['log_request_cancelled'] = report['log']['llm_request_cancelled_lines'] >= 1
        checks['log_no_late_response'] = report['log']['llm_response_after_cancel'] == 0
        checks['log_no_context_not_found'] = report['log']['context_not_found'] == 0
        checks['log_no_double_completion'] = report['log']['completed_multiple_times'] == 0
        checks['log_no_crash_or_ensure'] = report['log']['crash_lines'] == 0 and report['log']['ensure_lines'] == 0
        checks['log_reaches_exiting'] = report['log']['exiting_line']
    report['checks'] = checks

    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [args.log, args.report])
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(checks) and all(checks.values()) and not leaks
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
