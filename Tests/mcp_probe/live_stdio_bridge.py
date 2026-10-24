"""The stdio bridge (Content/Python/mcp_bridge) end to end: UTF-8 round trip, streamed or held translation, Claude CLI.

Stdlib only; run with `python -I`:

    python -I live_stdio_bridge.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor with only the provider's key (from the repo's .env) added to its environment and makes an
in-memory probe Blueprint under /Game/N2CTest (as live_env_translation.py does). Then, with the bridge started through
its launcher (launch_bridge.bat or launch_bridge.sh, which picks UE's Python through ue_python_finder.py) as a stdio
child with --project <the project> (it finds the editor through its discovery entry; --by-port passes --port
instead), and the provider key removed from the bridge's environment:

1. A small stdio client sends raw UTF-8 JSON (as JSON.stringify writes it) and checks: run-python receives
   'café あ日本 🙂' as the same code points; create-variable makes a member variable whose name has é and 日本 and
   whose tooltip has an emoji, and get-blueprint-member-variables reads both back unchanged; translate-focused-blueprint
   with a numeric progressToken returns a translation with code, with progress notifications (token still a number)
   arriving before the response on UE 5.8, and none on 5.6/5.7 where the call is held. Every stdout line is one
   JSON-RPC message.
2. The headless Claude Code CLI with an --mcp-config whose nodetocode server is the launcher as a stdio command
   (--strict-mcp-config --allowedTools "mcp__nodetocode__*" --output-format stream-json) calls run-python with the
   same text and translate-focused-blueprint once; both tool results are checked.

The editor log says how each translation was answered (text/event-stream or one JSON response). No key value may be in
the editor log, the bridge's stderr, the CLI's output or the report. Report: logs/live_stdio_bridge_<label>.json.
"""
import argparse
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_claude_cli_translate import code_chars_of  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, call_tool,  # noqa: E402
                                  stored_key_present, tool_text)

BRIDGE_DIR = os.path.normpath(os.path.join(HERE, '..', '..', 'Content', 'Python', 'mcp_bridge'))
LAUNCHER = os.path.join(BRIDGE_DIR, 'launch_bridge.bat' if os.name == 'nt' else 'launch_bridge.sh')

TEXT = 'café あ日本 🙂'
ECHO_PY = 's = "%s"\nresult = {"text": s, "codepoints": [ord(c) for c in s]}' % TEXT
VARIABLE = 'Vitesse_é_日本'
TOOLTIP = 'Tooltip café 日本 🙂'
CLI_TOOL = 'mcp__nodetocode__%s'
PROMPT = ('Use the nodetocode MCP tools. First call %s exactly once with this exact code argument (keep every '
          'character as given):\n%s\nThen call %s exactly once with arguments {"provider": "%s", "model": "%s", '
          '"language": "%s"}. Do not call any other tool. Then reply with only the word DONE.')


def child_env():
    """The bridge's and the CLI's environment: no provider key"""
    drop = set(PROVIDER_VARIABLE.values()) | {'DEEPSEEK_API', 'PYTHONUTF8', 'PYTHONIOENCODING'}
    return {k: v for k, v in os.environ.items() if k not in drop}


class StdioClient:
    """An MCP client on the bridge's stdio: writes raw UTF-8 JSON lines, reads every stdout line as one message."""

    def __init__(self, cmd, stderr_path):
        self.stderr = open(stderr_path, 'wb')
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr,
                                     env=child_env())
        self.messages = queue.Queue()
        self.bad_lines = []
        self.next_id = 1
        self.t0 = time.time()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in self.proc.stdout:
            try:
                self.messages.put((round(time.time() - self.t0, 2), json.loads(raw.decode('utf-8'))))
            except ValueError:
                self.bad_lines.append(raw[:300].decode('utf-8', 'replace'))

    def _send(self, message):
        self.proc.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        self.proc.stdin.flush()

    def notify(self, method, params=None):
        self._send({'jsonrpc': '2.0', 'method': method, 'params': params or {}})

    def request(self, method, params, timeout=120):
        """(response, [(seconds, notification)], seconds the request was sent at)"""
        rid = self.next_id
        self.next_id += 1
        sent = round(time.time() - self.t0, 2)
        self._send({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params})
        notes = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                at, message = self.messages.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                break
            if message.get('id') == rid and 'method' not in message:
                return message, notes, sent, at
            notes.append((at, message))
        return None, notes, sent, None

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


def tool_json(response):
    try:
        return json.loads(tool_text(response))
    except (ValueError, TypeError):
        return None


def bridge_args(port, args):
    """The bridge finds the editor through its discovery entry by project (--by-port: names the port instead)"""
    return ['--port', str(port)] if args.by_port else ['--project', os.path.abspath(args.project)]


def stdio_client_checks(port, args, report, checks, stderr_path):
    client = StdioClient([LAUNCHER] + bridge_args(port, args), stderr_path)
    try:
        init, _, _, _ = client.request('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                                                      'clientInfo': {'name': 'n2c-stdio-probe', 'version': '1'}})
        checks['initialize'] = bool(init and 'result' in init)
        client.notify('notifications/initialized')

        # run-python: the editor's Python sees the same code points
        echo, _, _, _ = client.request('tools/call', {'name': 'run-python', 'arguments': {'code': ECHO_PY}})
        echo_text = tool_text(echo)
        report['echo'] = echo_text[:600]
        expected_points = [ord(c) for c in TEXT]
        checks['run_python_utf8_round_trip'] = (TEXT in echo_text and str(expected_points).replace(' ', '')
                                                in echo_text.replace(' ', ''))

        # A member variable with é and 日本 in its name and an emoji in its tooltip
        created, _, _, _ = client.request('tools/call', {'name': 'create-variable', 'arguments': {
            'variableName': VARIABLE, 'typeIdentifier': 'float', 'tooltip': TOOLTIP}})
        report['create_variable'] = tool_text(created)[:600]
        listed, _, _, _ = client.request('tools/call', {'name': 'get-blueprint-member-variables', 'arguments': {}})
        listed_text = tool_text(listed)
        report['member_variables_head'] = listed_text[:600]
        checks['variable_name_round_trip'] = VARIABLE in listed_text
        checks['variable_tooltip_round_trip'] = TOOLTIP in listed_text

        # The translation: streamed on 5.8 (progress first), held on 5.6/5.7
        token = 4242
        response, notes, sent, answered_at = client.request('tools/call', {
            'name': 'translate-focused-blueprint',
            'arguments': {'provider': args.provider, 'model': args.model, 'language': args.language},
            '_meta': {'progressToken': token}}, timeout=args.timeout)
        progress = [(at, n) for at, n in notes if n.get('method') == 'notifications/progress']
        text = tool_text(response)
        chars, graphs = code_chars_of(text)
        report['translate'] = {
            'seconds': round(answered_at - sent, 1) if answered_at else None,
            'progress_count': len(progress),
            'first_progress_at': round(progress[0][0] - sent, 2) if progress else None,
            'progress_tokens': sorted({repr(n['params'].get('progressToken')) for _, n in progress}),
            'progress_messages': [n['params'].get('message') for _, n in progress][:5],
            'is_error': bool((response or {}).get('result', {}).get('isError')) if response else None,
            'code_chars': chars, 'graphs': graphs, 'head': text[:300]}
        checks['translate_has_code'] = chars > 0
        if args.expect_stream:
            checks['progress_before_response'] = bool(progress) and progress[0][0] < answered_at
            checks['progress_token_still_a_number'] = all(n['params'].get('progressToken') == token
                                                          and isinstance(n['params'].get('progressToken'), int)
                                                          for _, n in progress)
        else:
            checks['held_without_progress'] = response is not None and not progress
    finally:
        client.close()
    report['stdout_non_json_lines'] = client.bad_lines
    checks['stdout_only_json_rpc'] = not client.bad_lines


def claude_cli_checks(port, args, report, checks, log_dir):
    config_path = os.path.join(log_dir, 'live_stdio_bridge_%s_mcp.json' % args.label)
    stream_path = os.path.join(log_dir, 'live_stdio_bridge_%s_cli.jsonl' % args.label)
    with open(config_path, 'w', encoding='utf-8') as f:
        json.dump({'mcpServers': {'nodetocode': {'type': 'stdio', 'command': LAUNCHER,
                                                 'args': bridge_args(port, args)}}}, f)
    prompt = PROMPT % (CLI_TOOL % 'run-python', ECHO_PY, CLI_TOOL % 'translate-focused-blueprint', args.provider,
                       args.model, args.language)
    cmd = [shutil.which('claude') or 'claude', '-p', prompt, '--mcp-config', config_path, '--strict-mcp-config',
           '--allowedTools', 'mcp__nodetocode__*', '--output-format', 'stream-json', '--verbose',
           '--model', args.cli_model]
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=args.timeout, env=child_env(), stdin=subprocess.DEVNULL)
        report['cli_exit'] = proc.returncode
        out = proc.stdout.decode('utf-8', 'replace')
        report['cli_stderr_head'] = proc.stderr.decode('utf-8', 'replace')[:600]
    except subprocess.TimeoutExpired as e:
        report['cli_exit'] = 'timeout'
        out = (e.stdout or b'').decode('utf-8', 'replace')
    report['cli_seconds'] = round(time.time() - t0, 1)
    with open(stream_path, 'w', encoding='utf-8') as f:
        f.write(out)

    uses, results, final, servers = {}, {}, None, None
    for line in out.splitlines():
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if msg.get('type') == 'system' and msg.get('subtype') == 'init':
            servers = msg.get('mcp_servers')
        if msg.get('type') == 'result':
            final = msg
        content = (msg.get('message') or {}).get('content')
        if isinstance(content, list):
            for block in content:
                if block.get('type') == 'tool_use':
                    uses[block.get('id')] = block
                elif block.get('type') == 'tool_result':
                    results[block.get('tool_use_id')] = block

    def result_text(use):
        content = (results.get(use.get('id')) or {}).get('content')
        return content if isinstance(content, str) else ''.join(
            c.get('text', '') for c in (content or []) if isinstance(c, dict))

    report['cli_mcp_servers'] = servers
    report['cli_tool_uses'] = [{'name': u.get('name'), 'input': u.get('input')} for u in uses.values()]
    report['cli_final'] = {k: (final or {}).get(k) for k in ('subtype', 'is_error', 'result', 'num_turns')}
    checks['cli_connected_stdio_server'] = any(s.get('name') == 'nodetocode' and s.get('status') == 'connected'
                                               for s in (servers or []))
    py_uses = [u for u in uses.values() if u.get('name') == CLI_TOOL % 'run-python']
    tr_uses = [u for u in uses.values() if u.get('name') == CLI_TOOL % 'translate-focused-blueprint']
    if py_uses:
        echo_text = result_text(py_uses[0])
        report['cli_echo'] = echo_text[:400]
        checks['cli_run_python_utf8_round_trip'] = TEXT in echo_text and str(
            [ord(c) for c in TEXT]).replace(' ', '') in echo_text.replace(' ', '')
    else:
        checks['cli_called_run_python'] = False
    if tr_uses:
        text = result_text(tr_uses[0])
        chars, graphs = code_chars_of(text)
        report['cli_translate'] = {'is_error': bool((results.get(tr_uses[0].get('id')) or {}).get('is_error')),
                                   'code_chars': chars, 'graphs': graphs, 'head': text[:300]}
        checks['cli_translate_has_code'] = chars > 0
    else:
        checks['cli_called_translate'] = False
    return stream_path, config_path


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5', help='model the editor translates with')
    ap.add_argument('--cli-model', default='haiku', help='model the Claude CLI itself runs on')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--port', type=int, default=27210)
    ap.add_argument('--skip-cli', action='store_true', help='only the stdio client part')
    ap.add_argument('--by-port', action='store_true', help='start the bridge with --port instead of --project')
    args = ap.parse_args()
    args.expect_stream = '5.8' in args.label or os.path.basename(os.path.normpath(args.engine)).endswith('5.8')

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_stdio_bridge_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_stdio_bridge_%s.json' % args.label)
    stderr_path = os.path.join(log_dir, 'live_stdio_bridge_%s_bridge_stderr.log' % args.label)

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % STORE_ID[args.provider]}))
        return 2

    port = free_port(args.port)
    report = {'label': args.label, 'provider': args.provider, 'model': args.model, 'port': port,
              'launcher': LAUNCHER, 'expect_stream': args.expect_stream}
    checks = {}
    extra_paths = []
    with EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                       extra_args=['-RenderOffScreen']) as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CBridgeProbe_%s' % time.strftime('%H%M%S')
            _, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = tool_text(setup)[:600] or setup
            stdio_client_checks(port, args, report, checks, stderr_path)
            if not args.skip_cli:
                extra_paths += claude_cli_checks(port, args, report, checks, log_dir)
            checks['editor_alive_at_end'] = s.health()

    log_text = s.log_text()
    answered = re.findall(r'Started task ([0-9A-F]{32}) for tool translate-focused-blueprint \(progress token ([^)]*)\), '
                          r'answering ([^\r\n]*)', log_text)
    report['editor_answered'] = [{'task': t, 'token': k, 'how': h} for t, k, h in answered]
    if answered:
        how = answered[0][2]
        checks['editor_answered_as_expected'] = ('text/event-stream' in how) == args.expect_stream
    report['checks'] = checks
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, ensure_ascii=False)
    leaks = find_leaks(keys, [log_path, report_path, stderr_path] + extra_paths)
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(checks) and all(checks.values()) and not leaks
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1, ensure_ascii=False)
    sys.stdout.reconfigure(encoding='utf-8')
    print(json.dumps(report, indent=1, ensure_ascii=False))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
