"""MCP 2025-11-25 tasks for translate-focused-blueprint against a running editor (card 0211).

Stdlib only; run with `python -I`, on any engine (tasks are plain JSON requests, no stream):

    python -I live_tasks_0211.py --engine <UE_5.8> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and, in one session: checks that initialize advertises capabilities.tasks and that
tools/list gives translate-focused-blueprint execution.taskSupport "optional"; calls translate-focused-blueprint
with params.task (no Blueprint is focused, so the call fails quickly and no provider is called) and checks that the
answer is a CreateTaskResult, that tasks/get reaches status failed, that tasks/result returns the isError
CallToolResult with the related-task _meta, that tasks/list lists the task, and that tasks/cancel of the finished
task is -32602. Report: logs/live_tasks_0211_<label>.json.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_env_translation import post  # noqa: E402
from mcp_session import session_headers  # noqa: E402


def call(port, rid, method, params):
    status, body = post(port, {'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params})
    try:
        return status, json.loads(body)
    except ValueError:
        return status, {'raw': body[:500]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27190)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_tasks_0211_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_tasks_0211_%s.json' % args.label)
    port = free_port(args.port)
    report = {'label': args.label, 'port': port}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=False) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            session_headers(port)  # opens the session this script's posts use
            # initialize again on its own to read the capabilities
            status, init = call(port, 1, 'initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                                         'clientInfo': {'name': 'live-tasks-0211', 'version': '1'}})
            tasks_cap = ((init.get('result') or {}).get('capabilities') or {}).get('tasks') or {}
            report['tasks_capability'] = tasks_cap
            checks['initialize: tasks capability with tools/call'] = 'cancel' in tasks_cap and 'list' in tasks_cap and \
                'call' in ((tasks_cap.get('requests') or {}).get('tools') or {})

            status, listed = call(port, 2, 'tools/list', {})
            entry = next((t for t in (listed.get('result') or {}).get('tools') or [] if t.get('name') == 'translate-focused-blueprint'), {})
            report['execution'] = entry.get('execution')
            checks['tools/list: translate-focused-blueprint taskSupport optional'] = (entry.get('execution') or {}).get('taskSupport') == 'optional'

            started = time.time()
            status, created = call(port, 3, 'tools/call', {'name': 'translate-focused-blueprint', 'arguments': {}, 'task': {'ttl': 120000}})
            task = (created.get('result') or {}).get('task') or {}
            report['created'] = task
            report['created_seconds'] = round(time.time() - started, 2)
            # The call fails inside its start (no Blueprint focused), so the task may already be failed when it is created
            checks['tools/call with task: a CreateTaskResult at once'] = status == 200 and task.get('status') in ('working', 'failed') \
                and bool(task.get('taskId')) and task.get('ttl') == 120000
            task_id = task.get('taskId') or ''

            final = {}
            deadline = time.time() + 30
            while time.time() < deadline:
                status, got = call(port, 4, 'tasks/get', {'taskId': task_id})
                final = got.get('result') or {}
                if final.get('status') not in (None, 'working'):
                    break
                time.sleep(0.5)
            report['final'] = final
            checks['tasks/get: failed (no Blueprint focused)'] = final.get('status') == 'failed'

            status, result = call(port, 5, 'tasks/result', {'taskId': task_id})
            res = result.get('result') or {}
            report['result'] = {'isError': res.get('isError'), 'text': ((res.get('content') or [{}])[0].get('text') or '')[:200],
                                'meta': res.get('_meta')}
            checks['tasks/result: the isError CallToolResult with related-task'] = res.get('isError') is True and \
                ((res.get('_meta') or {}).get('io.modelcontextprotocol/related-task') or {}).get('taskId') == task_id

            status, lst = call(port, 6, 'tasks/list', {})
            checks['tasks/list: lists it'] = any(t.get('taskId') == task_id for t in (lst.get('result') or {}).get('tasks') or [])
            status, cancel = call(port, 7, 'tasks/cancel', {'taskId': task_id})
            checks['tasks/cancel of a finished task: -32602'] = (cancel.get('error') or {}).get('code') == -32602
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
