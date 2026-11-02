"""A client that goes away mid-translation: does the call stop? (card 0512, gate 0382's cancel item)

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I live_cancel_close_0512.py --engine <UE root> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" \
        --provider anthropic --model claude-haiku-5-5 [--claude-esc] [--claude-interrupt]

One headless editor with only the provider's key (from the repo's .env) in its environment and Python execution on;
the in-memory probe Blueprint (probe_blueprint.py) is focused. Then, each case against the same editor:

  raw_stream  a raw 2026-07-28 tools/call of translate-focused-blueprint (no progressToken) with
              Accept: application/json, text/event-stream; the socket is closed --cancel-after seconds in
  raw_json    the same with Accept: application/json only (the call cannot stream: one JSON reply)
  control     a held call (Accept: application/json) whose client stays connected: it must finish with its response
              and no cancel line (the connection check must not cancel a live client)
  raw_cancel  the same streamed call, then notifications/cancelled {requestId} as a separate 2026-07-28 POST
              (the stream is left open)
  claude_esc  (--claude-esc, needs the desktop) interactive Claude Code in its own console, through a logging TCP proxy,
              starts the translation; Esc is sent to that console (WScript SendKeys) once the LLM request is out
  claude_break (--claude-break) gate 0382's case: `claude -p` gets CTRL_BREAK (Ctrl+C) once the LLM request is out
  claude_interrupt (--claude-interrupt) the same through `claude -p --input-format stream-json`, interrupted with the
              SDK's control_request {subtype: interrupt}, the path the SDK's interrupt() and the TUI's Esc share

For each case the report records what the editor log shows after the cancel moment: the task id, a cancel line
("closed the stream of call", "notifications/cancelled", "request cancelled by the client", "LLM request cancelled"),
whether the provider's response was handled ("Response from") and how the task completed. Passing: a cancel line, the
task completed once, and no response handled after the cancel. For the Claude Code cases the proxy log records each
request's method, Accept header, JSON-RPC method and which side closed the connection first.
Report: logs/live_cancel_close_0512.json (proxy traffic: logs/live_cancel_close_0512_<case>_proxy.jsonl).
No key value is printed or written.
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, call_tool, tool_text  # noqa: E402

VERSION = '2026-07-28'
META = {'io.modelcontextprotocol/protocolVersion': VERSION,
        'io.modelcontextprotocol/clientInfo': {'name': 'n2c-0512', 'version': '1'},
        'io.modelcontextprotocol/clientCapabilities': {}}
CANCEL_RE = re.compile(r'closed the stream of call|closed its connection|notifications/cancelled|request cancelled by the client|'
                       r'LLM request cancelled|Cancellation requested for async task|session \S+ ended')


def read_log(path, start):
    try:
        with open(path, 'rb') as f:
            f.seek(start)
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def log_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def wait_for(path, start, pattern, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(path, start)
        m = re.search(pattern, text)
        if m:
            return m, text
        time.sleep(0.25)
    return None, read_log(path, start)


def modern_request(port, rid, method, params, accept):
    body = json.dumps({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': dict(params, _meta=META)}
                      if rid is not None else {'jsonrpc': '2.0', 'method': method, 'params': dict(params, _meta=META)})
    body = body.encode('utf-8')
    head = ('POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/json\r\nAccept: %s\r\n'
            'MCP-Protocol-Version: %s\r\nMcp-Method: %s\r\n%sContent-Length: %d\r\n\r\n'
            % (port, accept, VERSION, method, ('Mcp-Name: %s\r\n' % params['name']) if 'name' in params else '',
               len(body))).encode('ascii')
    return head + body


def analyse(text, cancel_offset):
    """What the log shows: the task, and what happened after the cancel moment (offset into text)"""
    started = re.search(r'Started task ([0-9A-F]{32}) for tool translate-focused-blueprint \(progress token ([^)]*)\), answering (.*)', text)
    after = text[cancel_offset:] if cancel_offset is not None else ''
    task = started.group(1) if started else None
    done = re.findall(r'Async task %s completed \(Tool: [^,]+, IsError: (\w+)\)' % task, text) if task else []
    return {
        'task': task,
        'answering': started.group(3).strip() if started else None,
        'cancel_lines': [l.strip()[-220:] for l in after.splitlines() if CANCEL_RE.search(l)][:8],
        'response_handled_after_cancel': bool(re.search(r'Response from \w+', after)),
        'completed': done,
    }


def case_raw(port, log, mode, cancel_after, settle):
    """mode: stream, json or notify"""
    accept = 'application/json' if mode == 'json' else 'application/json, text/event-stream'
    rid = 'n2c-0512-%s-%d' % (mode, int(time.time()))
    start = log_size(log)
    sock = socket.create_connection(('127.0.0.1', port), timeout=5)
    sock.sendall(modern_request(port, rid, 'tools/call', {'name': 'translate-focused-blueprint', 'arguments': ARGS}, accept))
    got = b''
    sock.settimeout(0.5)
    t0 = time.time()
    # wait until the provider request is out (or cancel_after passes), reading whatever the server writes
    while time.time() - t0 < cancel_after:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            got += chunk
        except socket.timeout:
            pass
    r = {'mode': mode, 'accept': accept, 'bytes_before_cancel': len(got),
         'status_line': got.split(b'\r\n', 1)[0].decode('latin-1') if got else None}
    cancel_offset = len(read_log(log, start))
    if mode == 'notify':
        # notifications/cancelled on its own connection; the call's stream is left open
        s2 = socket.create_connection(('127.0.0.1', port), timeout=10)
        s2.sendall(modern_request(port, None, 'notifications/cancelled', {'requestId': rid, 'reason': 'n2c-0512'}, accept))
        resp = b''
        try:
            while b'\r\n\r\n' not in resp:
                chunk = s2.recv(4096)
                if not chunk:
                    break
                resp += chunk
        except socket.timeout:
            pass
        r['notify_status'] = resp.split(b'\r\n', 1)[0].decode('latin-1')
        s2.close()
        # read the call's own stream to its end
        sock.settimeout(1.0)
        t1 = time.time()
        while time.time() - t1 < settle:
            try:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                got += chunk
            except socket.timeout:
                pass
        r['call_stream_had_response'] = b'"result"' in got or b'"error"' in got
        sock.close()
    else:
        sock.close()
        time.sleep(settle)
    r['closed_at_s'] = round(cancel_after, 1)
    r.update(analyse(read_log(log, start), cancel_offset))
    if mode == 'notify':
        # 2026-07-28 defines no client notification on Streamable HTTP: the server answers 202 and ignores it, and the
        # call runs on (informational: no pass or fail)
        r['ignored_as_specified'] = r['notify_status'].startswith('HTTP/1.1 202') and r['response_handled_after_cancel']
        r['pass'] = None
        return r
    r['pass'] = bool(r['task'] and r['cancel_lines'] and not r['response_handled_after_cancel'] and len(r['completed']) == 1)
    return r


def case_control(port, log, timeout):
    """A held call (Accept: application/json) whose client stays: it must finish with its response, not be cancelled"""
    rid = 'n2c-0512-control-%d' % int(time.time())
    start = log_size(log)
    sock = socket.create_connection(('127.0.0.1', port), timeout=5)
    sock.sendall(modern_request(port, rid, 'tools/call', {'name': 'translate-focused-blueprint', 'arguments': ARGS},
                                'application/json'))
    sock.settimeout(1.0)
    got = b''
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            got += chunk
            if b'"result"' in got and got.rstrip().endswith(b'}'):
                break
        except socket.timeout:
            pass
    sock.close()
    r = {'seconds': round(time.time() - t0, 1), 'status_line': got.split(b'\r\n', 1)[0].decode('latin-1') if got else None,
         'response_received': b'"result"' in got}
    r.update(analyse(read_log(log, start), 0))
    r['pass'] = bool(r['task'] and r['response_received'] and not r['cancel_lines'] and r['completed'] == ['No'])
    return r


class Proxy:
    """A logging TCP proxy in front of the editor: each client-to-server chunk and each close, with timestamps"""

    def __init__(self, upstream_port, log_path):
        self.upstream = upstream_port
        self.lsock = socket.socket()
        self.lsock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.lsock.bind(('127.0.0.1', 0))
        self.lsock.listen(16)
        self.port = self.lsock.getsockname()[1]
        self.events = []
        self.lock = threading.Lock()
        self.log_path = log_path
        self.next_conn = 0
        threading.Thread(target=self._accept, daemon=True).start()

    def _event(self, **e):
        e['t'] = round(time.time(), 3)
        with self.lock:
            self.events.append(e)

    def _accept(self):
        while True:
            try:
                c, _ = self.lsock.accept()
            except OSError:
                return
            self.next_conn += 1
            cid = self.next_conn
            try:
                u = socket.create_connection(('127.0.0.1', self.upstream), timeout=10)
                u.settimeout(None)
            except OSError as e:
                self._event(conn=cid, what='upstream_failed', error=str(e))
                c.close()
                continue
            self._event(conn=cid, what='open')
            threading.Thread(target=self._pipe, args=(cid, c, u, 'client'), daemon=True).start()
            threading.Thread(target=self._pipe, args=(cid, u, c, 'server'), daemon=True).start()

    def _pipe(self, cid, src, dst, side):
        while True:
            try:
                data = src.recv(65536)
            except OSError:
                data = b''
            if not data:
                self._event(conn=cid, what='closed_by_' + side)
                for s in (dst, src):
                    try:
                        s.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                return
            if side == 'client':
                text = data.decode('utf-8', 'replace')
                text = re.sub(r'(?im)^(authorization:).*$', r'\1 <redacted>', text)
                self._event(conn=cid, what='request_bytes', text=text[:4000])
            else:
                self._event(conn=cid, what='response_bytes', n=len(data), head=data[:200].decode('utf-8', 'replace'))
            try:
                dst.sendall(data)
            except OSError:
                pass

    def summary(self):
        """Per request: connection, Accept, JSON-RPC method, and closes"""
        out = []
        with self.lock:
            events = list(self.events)
        with open(self.log_path, 'w', encoding='utf-8') as f:
            for e in events:
                f.write(json.dumps(e) + '\n')
        for e in events:
            if e['what'] == 'request_bytes':
                accept = re.search(r'(?im)^accept:\s*(.*)$', e['text'])
                method = re.search(r'"method"\s*:\s*"([^"]+)"', e['text'])
                proto = re.search(r'(?im)^mcp-protocol-version:\s*(.*)$', e['text'])
                tool = re.search(r'"name"\s*:\s*"([^"]+)"', e['text'])
                if method or accept:
                    out.append({'conn': e['conn'], 't': e['t'], 'method': method.group(1) if method else None,
                                'tool': tool.group(1) if tool and method and method.group(1) == 'tools/call' else None,
                                'accept': accept.group(1).strip() if accept else None,
                                'protocol': proto.group(1).strip() if proto else None})
            elif e['what'].startswith('closed_by_'):
                out.append({'conn': e['conn'], 't': e['t'], 'close': e['what']})
        return out

    def close(self):
        self.lsock.close()


def mcp_config(path, port):
    with open(path, 'w', encoding='ascii') as f:
        json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
    return path


PROMPT = ('Call the mcp__nodetocode__translate-focused-blueprint tool exactly once with arguments '
          '{"provider": "%s", "model": "%s", "language": "cpp"}. Then reply DONE.')


def claude_env():
    """Claude Code's environment without provider keys (the editor holds its own)"""
    env = dict(os.environ)
    for v in list(PROVIDER_VARIABLE.values()) + ['DEEPSEEK_API']:
        env.pop(v, None)
    return env


def case_claude_esc(port, log, args, log_dir):
    proxy = Proxy(port, os.path.join(log_dir, 'live_cancel_close_0512_claude_esc_proxy.jsonl'))
    cfg = mcp_config(os.path.join(log_dir, 'live_cancel_close_0512_esc_mcp.json'), proxy.port)
    inner = os.path.join(log_dir, 'live_cancel_close_0512_esc_inner.ps1')
    prompt = PROMPT % (args.provider, args.model)
    with open(inner, 'w', encoding='utf-8') as f:
        f.write("foreach ($v in 'ANTHROPIC_API_KEY','OPENAI_API_KEY','GEMINI_API_KEY','DEEPSEEK_API_KEY','DEEPSEEK_API') "
                "{ Remove-Item \"env:$v\" -ErrorAction SilentlyContinue }\n")
        f.write("$host.UI.RawUI.WindowTitle = 'N2C-0512-ESC'\n")
        f.write("claude '%s' --mcp-config '%s' --strict-mcp-config --allowedTools 'mcp__nodetocode__*' --model haiku\n"
                % (prompt.replace("'", "''"), cfg))
    start = log_size(log)
    proc = subprocess.Popen(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
                             "$p = Start-Process powershell.exe -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-NoExit','-File','%s' -PassThru; $p.Id" % inner],
                            stdout=subprocess.PIPE, text=True)
    console_pid = int((proc.communicate(timeout=60)[0] or '0').strip() or 0)
    r = {'console_pid': console_pid}
    m, _ = wait_for(log, start, r'Sending request to|Node translation complete|Started task', 180)
    r['request_seen'] = bool(m)
    m, _ = wait_for(log, start, r'Sending request to \w+|Request to \w+', 30)
    time.sleep(2.0)
    cancel_offset = len(read_log(log, start))
    # by window title: on Windows 11 the console may be hosted by Windows Terminal, whose process is not console_pid
    ps = ("$ws = New-Object -ComObject WScript.Shell; $ok = $ws.AppActivate('N2C-0512-ESC'); "
          "if (-not $ok) { $ok = $ws.AppActivate(%d) }; Start-Sleep -Milliseconds 400; "
          "$ws.SendKeys('{ESC}'); Write-Output $ok" % console_pid)
    out = subprocess.run(['powershell.exe', '-NoProfile', '-Command', ps], capture_output=True, text=True, timeout=30)
    r['esc_sent'] = True
    r['activate'] = out.stdout.strip()
    time.sleep(args.settle)
    r['proxy'] = proxy.summary()
    subprocess.run(['taskkill', '/T', '/F', '/PID', str(console_pid)], capture_output=True)
    proxy.close()
    r.update(analyse(read_log(log, start), cancel_offset))
    r['pass'] = bool(r['task'] and r['cancel_lines'] and not r['response_handled_after_cancel'] and len(r['completed']) == 1)
    return r


def case_claude_interrupt(port, log, args, log_dir):
    proxy = Proxy(port, os.path.join(log_dir, 'live_cancel_close_0512_claude_interrupt_proxy.jsonl'))
    cfg = mcp_config(os.path.join(log_dir, 'live_cancel_close_0512_int_mcp.json'), proxy.port)
    start = log_size(log)
    cmd = ['claude', '-p', '--input-format', 'stream-json', '--output-format', 'stream-json', '--verbose',
           '--mcp-config', cfg, '--strict-mcp-config', '--allowedTools', 'mcp__nodetocode__*', '--model', 'haiku']
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=claude_env(), text=True, encoding='utf-8', shell=(os.name == 'nt'))
    out_lines = []
    threading.Thread(target=lambda: [out_lines.append(l) for l in proc.stdout], daemon=True).start()
    proc.stdin.write(json.dumps({'type': 'user', 'message': {'role': 'user', 'content': PROMPT % (args.provider, args.model)}}) + '\n')
    proc.stdin.flush()
    r = {}
    m, _ = wait_for(log, start, r'Started task [0-9A-F]{32} for tool translate-focused-blueprint', 180)
    r['call_started'] = bool(m)
    wait_for(log, start, r'Sending request to \w+|Request to \w+', 30)
    time.sleep(2.0)
    cancel_offset = len(read_log(log, start))
    proc.stdin.write(json.dumps({'type': 'control_request', 'request_id': 'n2c-0512-int',
                                 'request': {'subtype': 'interrupt'}}) + '\n')
    proc.stdin.flush()
    time.sleep(args.settle)
    try:
        proc.stdin.close()
    except OSError:
        pass
    try:
        proc.wait(30)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.pid)], capture_output=True)
    r['interrupt_acknowledged'] = any('"n2c-0512-int"' in l and 'control_response' in l for l in out_lines)
    r['proxy'] = proxy.summary()
    proxy.close()
    r.update(analyse(read_log(log, start), cancel_offset))
    r['pass'] = bool(r['task'] and r['cancel_lines'] and not r['response_handled_after_cancel'] and len(r['completed']) == 1)
    return r


def case_claude_break(port, log, args, log_dir):
    """Gate 0382's case: `claude -p` starts the translation and gets CTRL_BREAK (Ctrl+C) once the LLM request is out"""
    import shutil
    import signal
    proxy = Proxy(port, os.path.join(log_dir, 'live_cancel_close_0512_claude_break_proxy.jsonl'))
    cfg = mcp_config(os.path.join(log_dir, 'live_cancel_close_0512_break_mcp.json'), proxy.port)
    exe = shutil.which('claude') or 'claude'
    start = log_size(log)
    proc = subprocess.Popen([exe, '-p', PROMPT % (args.provider, args.model), '--mcp-config', cfg, '--strict-mcp-config',
                             '--allowedTools', 'mcp__nodetocode__*', '--model', 'haiku'],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=claude_env(),
                            creationflags=getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0))
    r = {}
    m, _ = wait_for(log, start, r'Started task [0-9A-F]{32} for tool translate-focused-blueprint', 180)
    r['call_started'] = bool(m)
    wait_for(log, start, r'Sending request to \w+', 30)
    time.sleep(2.0)
    cancel_offset = len(read_log(log, start))
    if os.name == 'nt':
        os.kill(proc.pid, signal.CTRL_BREAK_EVENT)
    else:
        proc.send_signal(signal.SIGINT)
    try:
        r['cli_exit'] = proc.wait(30)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.pid)], capture_output=True)
        r['cli_exit'] = 'killed'
    time.sleep(args.settle)
    r['proxy'] = proxy.summary()
    proxy.close()
    r.update(analyse(read_log(log, start), cancel_offset))
    r['pass'] = bool(r['task'] and r['cancel_lines'] and not r['response_handled_after_cancel'] and len(r['completed']) == 1)
    return r


ARGS = {}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--port', type=int, default=27295)
    ap.add_argument('--cancel-after', type=float, default=4.0, help='seconds after the call starts')
    ap.add_argument('--settle', type=float, default=25.0)
    ap.add_argument('--cases', default='raw_stream,raw_json,raw_cancel')
    ap.add_argument('--claude-esc', action='store_true')
    ap.add_argument('--claude-interrupt', action='store_true')
    ap.add_argument('--claude-break', action='store_true')
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)
    ARGS.update({'provider': args.provider, 'model': args.model, 'language': 'cpp'})

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    port = free_port(args.port)
    log = os.path.join(args.log_dir, 'live_cancel_close_0512_editor.log')
    report = {'engine': args.engine, 'port': port, 'model': args.model, 'cases': {}}
    cases = [c for c in args.cases.split(',') if c]
    if args.claude_interrupt:
        cases.append('claude_interrupt')
    if args.claude_break:
        cases.append('claude_break')
    if args.claude_esc:
        cases.append('claude_esc')
    with EditorSession(args.engine, args.project, port, log, allow_python=True, extra_env=keys,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CCancelProbe_%s' % time.strftime('%H%M%S')
            _, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = tool_text(setup)[:200]
            for case in cases:
                try:
                    if case == 'control':
                        res = case_control(port, log, 180)
                    elif case == 'raw_stream':
                        res = case_raw(port, log, 'stream', args.cancel_after, args.settle)
                    elif case == 'raw_json':
                        res = case_raw(port, log, 'json', args.cancel_after, args.settle)
                    elif case == 'raw_cancel':
                        res = case_raw(port, log, 'notify', args.cancel_after, args.settle)
                    elif case == 'claude_interrupt':
                        res = case_claude_interrupt(port, log, args, args.log_dir)
                    elif case == 'claude_break':
                        res = case_claude_break(port, log, args, args.log_dir)
                    elif case == 'claude_esc':
                        res = case_claude_esc(port, log, args, args.log_dir)
                    else:
                        res = {'error': 'unknown case'}
                except Exception as e:  # report and go on with the next case
                    res = {'error': '%s: %s' % (type(e).__name__, e)}
                report['cases'][case] = res
                time.sleep(3)
    report['restore_ok'] = s.restore_ok
    report['key_leaked'] = bool(find_leaks(keys, [log]))
    with open(os.path.join(args.log_dir, 'live_cancel_close_0512.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
