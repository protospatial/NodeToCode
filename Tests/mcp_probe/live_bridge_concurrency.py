"""The stdio bridge with a live editor: concurrent calls, a forwarded cancel, and picking the editor by project.

Stdlib only; run with `python -I`:

    python -I live_bridge_concurrency.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" \
        --second-engine G:/UE/UE_5.6 \
        --second-project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_6/NodeToCodeHost_5_6.uproject"

It starts a headless editor with only the provider's key (from the repo's .env) added to its environment, makes an
in-memory probe Blueprint under /Game/N2CTest (as live_env_translation.py does) and, with --second-project, a second
headless editor without a key. The bridge runs through its launcher with no --port: it finds the editors through
their discovery entries, and the bridge and this script never have a provider key in their environment.

1. Concurrency and cancel (bridge started with --project <first project>): translate-focused-blueprint with a
   progress token, then, while it runs, a quick tools/call (get-available-translation-targets) and a ping, which must
   both answer before the translation would; then notifications/cancelled for the translation. Nothing more may
   arrive for its id (no response, no error, no progress), a later request still answers, and the editor log shows
   the provider request cancelled and the response suppressed.
2. Editor selection (with --second-project): with both editors running, a bridge without --project answers
   initialize with an error naming both projects; --project <either> and NODETOCODE_PROJECT reach the right editor
   (run-python reports its engine version).

No key value may be in the editor logs, the bridge's stderr or the report. Report: logs/live_bridge_concurrency.json.
"""
import argparse
import json
import os
import queue
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, call_tool,  # noqa: E402
                                  stored_key_present, tool_text)

BRIDGE_DIR = os.path.normpath(os.path.join(HERE, '..', '..', 'Content', 'Python', 'mcp_bridge'))
LAUNCHER = os.path.join(BRIDGE_DIR, 'launch_bridge.bat' if os.name == 'nt' else 'launch_bridge.sh')
ENGINE_PY = 'import unreal\nresult = unreal.SystemLibrary.get_engine_version()'


def child_env(extra=None):
    """The bridge's environment: no provider key, no project selection unless given"""
    drop = set(PROVIDER_VARIABLE.values()) | {'DEEPSEEK_API', 'PYTHONUTF8', 'PYTHONIOENCODING', 'NODETOCODE_PROJECT'}
    env = {k: v for k, v in os.environ.items() if k not in drop}
    env.update(extra or {})
    return env


class Client:
    """An MCP client on the bridge's stdio that can have several requests in flight."""

    def __init__(self, args, stderr_path, env=None):
        self.stderr = open(stderr_path, 'ab')
        self.proc = subprocess.Popen([LAUNCHER] + list(args), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.stderr, env=child_env(env))
        self.t0 = time.time()
        self.messages = []  # (seconds, message)
        self.bad_lines = []
        self.cond = threading.Condition()
        self.next_id = 1
        threading.Thread(target=self._read, daemon=True).start()

    def now(self):
        return round(time.time() - self.t0, 2)

    def _read(self):
        for raw in self.proc.stdout:
            try:
                message = json.loads(raw.decode('utf-8'))
            except ValueError:
                self.bad_lines.append(raw[:300].decode('utf-8', 'replace'))
                continue
            with self.cond:
                self.messages.append((self.now(), message))
                self.cond.notify_all()

    def send(self, message):
        self.proc.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        self.proc.stdin.flush()
        return self.now()

    def request(self, method, params=None):
        rid = self.next_id
        self.next_id += 1
        message = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            message['params'] = params
        return rid, self.send(message)

    def notify(self, method, params=None):
        return self.send({'jsonrpc': '2.0', 'method': method, 'params': params or {}})

    def wait(self, predicate, timeout):
        """The first (seconds, message) matching predicate, or None"""
        deadline = time.time() + timeout
        with self.cond:
            while True:
                for at, message in self.messages:
                    if predicate(message):
                        return at, message
                left = deadline - time.time()
                if left <= 0:
                    return None
                self.cond.wait(left)

    def response(self, rid, timeout=60):
        return self.wait(lambda m: m.get('id') == rid and 'method' not in m, timeout)

    def call(self, method, params=None, timeout=60):
        rid, _ = self.request(method, params)
        found = self.response(rid, timeout)
        return found[1] if found else None

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.wait(30)
        except (OSError, subprocess.TimeoutExpired):
            pass
        if self.proc.poll() is None:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(self.proc.pid), '/T', '/F'], capture_output=True)
            else:
                self.proc.kill()
        self.stderr.close()


def initialize(client):
    reply = client.call('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                       'clientInfo': {'name': 'n2c-bridge-concurrency', 'version': '1'}})
    if reply and 'result' in reply:
        client.notify('notifications/initialized')
    return reply


def concurrency_checks(args, report, checks, stderr_path):
    client = Client(['--project', args.project], stderr_path)
    try:
        init = initialize(client)
        checks['initialize_by_discovery'] = bool(init and 'result' in init)
        token = 777
        tid, sent = client.request('tools/call', {
            'name': 'translate-focused-blueprint',
            'arguments': {'provider': args.provider, 'model': args.model, 'language': 'cpp'},
            '_meta': {'progressToken': token}})
        first = client.wait(lambda m: m.get('method') == 'notifications/progress'
                            and (m.get('params') or {}).get('progressToken') == token, 60)
        report['first_progress_at'] = round(first[0] - sent, 2) if first else None
        if args.cancel_after:
            time.sleep(args.cancel_after)

        qid, qsent = client.request('tools/call', {'name': 'get-available-translation-targets', 'arguments': {}})
        quick = client.response(qid, 60)
        pid_, psent = client.request('ping')
        pong = client.response(pid_, 30)
        translate_done = client.response(tid, 0)
        report['quick'] = {'seconds': round(quick[0] - qsent, 2) if quick else None,
                           'head': tool_text(quick[1])[:200] if quick else None}
        report['ping'] = pong[1] if pong else None
        checks['quick_call_answered'] = bool(quick and 'result' in quick[1] and not quick[1]['result'].get('isError'))
        checks['ping_answered'] = bool(pong and pong[1].get('result') == {})
        checks['quick_and_ping_answered_while_translate_ran'] = translate_done is None

        cancelled_at = client.notify('notifications/cancelled', {'requestId': tid, 'reason': 'bridge live check'})
        time.sleep(args.settle)
        after = [(at, m) for at, m in client.messages if at >= cancelled_at and (
            m.get('id') == tid or (m.get('params') or {}).get('progressToken') == token)]
        report['messages_for_cancelled_call_after_cancel'] = after[:5]
        report['translate_answered'] = client.response(tid, 0)
        checks['no_response_for_cancelled_call'] = client.response(tid, 0) is None
        checks['nothing_after_cancel'] = not after
        later = client.call('tools/call', {'name': 'get-available-translation-targets', 'arguments': {}})
        checks['later_request_answers'] = bool(later and 'result' in later)
    finally:
        client.close()
    report['stdout_non_json_lines'] = client.bad_lines
    checks['stdout_only_json_rpc'] = not client.bad_lines
    return tid, token


def engine_of(client):
    reply = client.call('tools/call', {'name': 'run-python', 'arguments': {'code': ENGINE_PY}})
    return tool_text(reply)[:300] if reply else None


def selection_checks(args, report, checks, stderr_path):
    names = [os.path.splitext(os.path.basename(p))[0] for p in (args.project, args.second_project)]
    client = Client([], stderr_path)
    try:
        reply = initialize(client)
        message = ((reply or {}).get('error') or {}).get('message', '')
        report['no_selection_error'] = message[:1200]
        checks['no_selection_is_an_error_naming_both'] = bool(
            reply and 'error' in reply and reply.get('id') == 1 and all(n in message for n in names))
    finally:
        client.close()

    for label, extra_args, env, project, engine in (
            ('second_by_flag', ['--project', args.second_project], None, args.second_project, args.second_label),
            ('first_by_name', ['--project', names[0]], None, args.project, args.label),
            ('second_by_env', [], {'NODETOCODE_PROJECT': args.second_project}, args.second_project, args.second_label)):
        client = Client(extra_args, stderr_path, env)
        try:
            init = initialize(client)
            version = engine_of(client) if init and 'result' in init else None
        finally:
            client.close()
        report[label] = version
        checks[label] = bool(version and engine in version)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8', help='engine version the first editor reports, for example 5.8')
    ap.add_argument('--second-engine')
    ap.add_argument('--second-project')
    ap.add_argument('--second-label', default='5.6')
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--cancel-after', type=float, default=1.0, help='seconds after the first progress')
    ap.add_argument('--settle', type=float, default=20.0, help='seconds to watch for anything after the cancel')
    ap.add_argument('--port', type=int, default=27240)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    report_path = os.path.join(log_dir, 'live_bridge_concurrency.json')
    stderr_path = os.path.join(log_dir, 'live_bridge_concurrency_bridge_stderr.log')
    log_a = os.path.join(log_dir, 'live_bridge_concurrency_editor_a.log')
    log_b = os.path.join(log_dir, 'live_bridge_concurrency_editor_b.log')
    if os.path.exists(stderr_path):
        os.remove(stderr_path)

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % STORE_ID[args.provider]}))
        return 2

    report = {'provider': args.provider, 'model': args.model, 'launcher': LAUNCHER}
    checks = {}
    port_a = free_port(args.port)
    second = None
    with EditorSession(args.engine, args.project, port_a, log_a, allow_python=True, extra_env=keys,
                       extra_args=['-RenderOffScreen']) as a:
        report['port_a'] = port_a
        checks['editor_a_started'] = a.start()
        if checks['editor_a_started']:
            name = 'BP_N2CBridgeConcurrency_%s' % time.strftime('%H%M%S')
            _, setup = call_tool(port_a, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = tool_text(setup)[:400]
            tid, token = concurrency_checks(args, report, checks, stderr_path)
            if args.second_project:
                port_b = free_port(port_a + 1 if port_a + 1 != args.port else port_a + 2)
                second = EditorSession(args.second_engine, args.second_project, port_b, log_b, allow_python=True)
                try:
                    report['port_b'] = port_b
                    checks['editor_b_started'] = second.start()
                    if checks['editor_b_started']:
                        selection_checks(args, report, checks, stderr_path)
                finally:
                    second.stop()
    log_text = a.log_text()
    report['log'] = {
        'cancel_line': ('notifications/cancelled for request %s' % tid) in log_text,
        'llm_request_cancelled_lines': log_text.count('LLM request cancelled'),
        'response_suppressed_lines': log_text.count('request cancelled by the client; no response sent'),
    }
    checks['log_cancel_reached_the_call'] = report['log']['cancel_line']
    checks['log_provider_request_cancelled'] = report['log']['llm_request_cancelled_lines'] >= 1
    checks['log_response_suppressed'] = report['log']['response_suppressed_lines'] >= 1
    report['checks'] = checks
    report['ok'] = all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, ensure_ascii=False)
    leaks = find_leaks(keys, [p for p in (log_a, log_b, stderr_path, report_path) if os.path.exists(p)])
    report['leaks'] = leaks
    if leaks:
        report['ok'] = False
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, ensure_ascii=False)
    print(json.dumps({'ok': report['ok'], 'checks': checks, 'leaks': leaks}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
