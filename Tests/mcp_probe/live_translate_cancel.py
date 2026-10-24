"""Cancel a live translate-focused-blueprint over MCP and check the cancel is real.

Stdlib only; run with `python -I`:

    python -I live_translate_cancel.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" \
        --provider anthropic --model claude-haiku-5-5

It starts a headless editor with only the provider's key (from the repo's .env) added to its environment, makes an
in-memory probe Blueprint under /Game/N2CTest (as live_env_translation.py does), starts a translation, cancels it
about --cancel-after seconds in, then watches for --settle seconds.

--via notification (the default; MCP basic/utilities/cancellation, card 0110) sends notifications/cancelled
{requestId} in the call's session. It passes when the notification is accepted with 202 and no body, the call's own
response carries no JSON-RPC response at all and ends soon after the cancel (a text/event-stream closes on UE 5.8; a
held JSON request ends with 202 and no body on 5.6/5.7), no progress arrives after it, the editor log shows the LLM
request was cancelled, the task completed exactly once and its response was suppressed, and the deprecated
nodetocode/cancelTask then answers task_already_completed (and logs its deprecation warning).

--via cancelTask sends the deprecated nodetocode/cancelTask {progressToken}. It passes when the cancel answers
cancellation_initiated, the call's own response carries exactly one JSON-RPC response, the cancelled error, and no
second response or progress event arrives after it, the log shows the LLM request was cancelled and the task completed
exactly once, and a second cancel answers task_already_completed.

Both then check the editor still answers /mcp/health, a control translation run to the end returns code (so the
provider really works and the cancel was not vacuous), and no key value is in the editor log or the report. Key
values are never printed. Report: logs/live_translate_cancel.json (logs/live_translate_cancel_<via>.json with --via
cancelTask).
"""
import argparse
import http.client
import json
import os
import re
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, ToolCallStream, call_tool,  # noqa: E402
                                  post, run_tool_call, stored_key_present, task_id_for_token, tool_text)


def response_of(data):
    try:
        message = json.loads(data)
    except ValueError:
        return {}, ''
    return message, tool_text(message)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--cancel-after', type=float, default=1.5)
    ap.add_argument('--settle', type=float, default=30.0, help='seconds to watch for late events after the cancel')
    ap.add_argument('--timeout', type=int, default=300, help='bound on the control translation')
    ap.add_argument('--port', type=int, default=27180)
    ap.add_argument('--via', default='notification', choices=['notification', 'cancelTask'],
                    help='notifications/cancelled (standard) or the deprecated nodetocode/cancelTask')
    ap.add_argument('--log', default=None)
    ap.add_argument('--report', default=None)
    args = ap.parse_args()
    suffix = '' if args.via == 'notification' else '_' + args.via
    args.log = args.log or os.path.join(HERE, 'logs', 'live_translate_cancel%s_editor.log' % suffix)
    args.report = args.report or os.path.join(HERE, 'logs', 'live_translate_cancel%s.json' % suffix)

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
    report = {'provider': args.provider, 'model': args.model, 'port': port, 'cancel_after': args.cancel_after,
              'settle': args.settle, 'via': args.via}
    checks = {}

    with EditorSession(args.engine, args.project, port, args.log, allow_python=True, extra_env=keys) as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CCancelProbe_%s' % time.strftime('%H%M%S')
            status, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = tool_text(setup)[:1000] or setup

            # 1. Start a translation and cancel it in flight
            token = 'cancel-probe-%d' % int(time.time())
            arguments = {'provider': args.provider, 'model': args.model, 'language': args.language}
            stream = ToolCallStream(port, 'translate-focused-blueprint', arguments, 2, meta={'progressToken': token})
            report['task_started'] = True
            time.sleep(args.cancel_after)
            responses_before_cancel = [e for e in stream.events if e[1] == 'response']
            report['responses_before_cancel'] = len(responses_before_cancel)
            cancel_sent = round(time.time() - stream.t0, 2)
            if args.via == 'notification':
                # Same session as the call (mcp_session.py caches one per port); requestId is the tools/call's id 2
                status, body = post(port, {'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                                           'params': {'requestId': 2, 'reason': 'live_translate_cancel probe'}})
                report['cancel'] = {'at': cancel_sent, 'status': status, 'body': body[:200]}
            else:
                status, body = post(port, {'jsonrpc': '2.0', 'id': 3, 'method': 'nodetocode/cancelTask',
                                           'params': {'progressToken': token}})
                report['cancel'] = {'at': cancel_sent, 'status': status, 'body': json.loads(body).get('result')}
            time.sleep(args.settle)
            status, body = post(port, {'jsonrpc': '2.0', 'id': 4, 'method': 'nodetocode/cancelTask',
                                       'params': {'progressToken': token}})
            report['second_cancel'] = json.loads(body).get('result')
            stream.close()

            responses = [e for e in stream.events if e[1] == 'response']
            report['stream'] = {'events': len(stream.events), 'closed_at': stream.closed_at,
                                'content_type': stream.content_type,
                                'progress_events_before_cancel': len([e for e in stream.events if e[1] == 'progress' and e[0] < cancel_sent]),
                                'responses': [{'at': t, 'text': response_of(d)[1][:200],
                                               'isError': bool((response_of(d)[0].get('result') or {}).get('isError'))}
                                              for t, _, d in responses]}
            first_response_at = responses[0][0] if responses else None
            late = [e for e in stream.events if first_response_at is not None and e[0] > first_response_at]
            report['stream']['events_after_response'] = [(t, e) for t, e, _ in late]

            report['stream']['status'] = stream.status
            checks['no_result_before_cancel'] = not responses_before_cancel
            if args.via == 'notification':
                late_progress = [t for t, e, _ in stream.events if e == 'progress' and t > cancel_sent + 1.0]
                report['stream']['progress_after_cancel'] = late_progress
                checks['notification_accepted_202'] = report['cancel']['status'] == 202 and report['cancel']['body'] == ''
                checks['no_response_sent'] = not responses
                checks['call_ended_soon_after_cancel'] = stream.closed_at is not None and stream.closed_at - cancel_sent < 2.0
                checks['no_progress_after_cancel'] = not late_progress
                if 'text/event-stream' not in (stream.content_type or ''):
                    # UE 5.6/5.7 (or a call cancelled before its first event): the held request ends with 202
                    checks['held_request_ended_202'] = stream.status == 202
            else:
                checks['cancel_initiated'] = (report['cancel']['body'] or {}).get('status') == 'cancellation_initiated'
                checks['one_response'] = len(responses) == 1
                checks['response_is_cancelled'] = bool(responses) and report['stream']['responses'][0]['isError'] \
                    and 'cancelled' in report['stream']['responses'][0]['text']
                checks['response_soon_after_cancel'] = first_response_at is not None and first_response_at - cancel_sent < 2.0
                checks['no_late_events'] = not late
            checks['second_cancel_already_completed'] = (report['second_cancel'] or {}).get('status') == 'task_already_completed'
            if 'text/event-stream' in (stream.content_type or ''):
                # UE 5.8 streams: progress arrived on the same POST before the cancel
                checks['progress_before_cancel'] = report['stream']['progress_events_before_cancel'] >= 1

            checks['editor_alive_after_cancel'] = s.health()

            # 2. Control: the same translation run to the end returns code
            started_at = time.time()
            events = run_tool_call(port, 'translate-focused-blueprint', arguments, 5,
                                   meta={'progressToken': token + '-control'}, timeout=args.timeout)
            if events:
                final = [d for e, d in events if e == 'response']
                message, text = response_of(final[-1]) if final else ({}, '')
                try:
                    translation = json.loads(text)
                except ValueError:
                    translation = None
                graphs = (translation or {}).get('graphs') or [] if isinstance(translation, dict) else []
                code_chars = sum(len((g.get('code') or {}).get('graphImplementation', '') or '')
                                 + len((g.get('code') or {}).get('graphDeclaration', '') or '')
                                 for g in graphs if isinstance(g, dict))
                report['control'] = {'seconds': round(time.time() - started_at, 1), 'graphs': len(graphs),
                                     'code_chars': code_chars,
                                     'isError': bool((message.get('result') or {}).get('isError')),
                                     'head': text[:200] if not code_chars else ''}
                checks['control_translation_returns_code'] = code_chars > 0
            checks['editor_alive_at_end'] = s.health()

    log_text = s.log_text()
    report['task_id'] = task_id_for_token(log_text, token) if report.get('task_started') else None
    if report.get('task_id'):
        tid = report['task_id']
        report['log'] = {
            'task_completed_lines': len(re.findall(r'Async task %s completed' % tid, log_text, re.I)),
            'llm_request_cancelled_lines': log_text.count('LLM request cancelled'),
            'completed_multiple_times': log_text.count('attempted to complete multiple times'),
            'already_completed_when_cancelled': log_text.count('had already completed when it was cancelled'),
            'response_suppressed_lines': len(re.findall(r'Async task %s: request cancelled by the client; no response sent' % tid,
                                                        log_text, re.I)),
            'cancel_task_deprecation_lines': log_text.count('nodetocode/cancelTask is deprecated'),
        }
        checks['log_task_completed_once'] = report['log']['task_completed_lines'] == 1
        checks['log_request_cancelled'] = report['log']['llm_request_cancelled_lines'] >= 1
        checks['log_no_double_completion'] = report['log']['completed_multiple_times'] == 0
        if args.via == 'notification':
            checks['log_response_suppressed'] = report['log']['response_suppressed_lines'] == 1
        else:
            checks['log_response_not_suppressed'] = report['log']['response_suppressed_lines'] == 0
        checks['log_cancel_task_deprecated'] = report['log']['cancel_task_deprecation_lines'] >= 1
    report['checks'] = checks

    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [args.log, args.report])
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(checks) and all(checks.values()) and not leaks and 'task_id' in report and bool(report['task_id'])
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
