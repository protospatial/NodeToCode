"""Live probes for NodeToCode's MCP transport findings, against a headless editor on a host project.

Stdlib only; run with `python -I`:

    python -I mcp_probe.py --engine G:/UE/UE_5.8 --project G:/.../NodeToCodeHost_5_8.uproject --label 5.8

Writes a JSON report (--report) with, per probe, the finding ids, a status (reproduced, fixed or skipped) and raw
evidence. Exits 0 only when every status matches expected.json for the label. See README.md.
"""
import argparse
import http.client
import json
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from mcp_session import open_session, session_headers  # noqa: E402

TOOLS_LIST = json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'tools/list'})


def request(port, verb, body, headers, path='/mcp', keep=400):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        conn.request(verb, path, body=body.encode('utf-8') if body is not None else None, headers=headers)
        r = conn.getresponse()
        data = r.read().decode('utf-8', 'replace')
        return {'status': r.status, 'headers': {k.lower(): v for k, v in r.getheaders()}, 'body': data[:keep] if keep else data}
    except Exception as e:
        return {'status': None, 'error': str(e)}
    finally:
        conn.close()


def post(port, body, headers, path='/mcp', keep=400, session=True):
    """POST body; with session (the default) inside this probe's MCP session (Tests/mcp_probe/mcp_session.py), so
    the check under test answers rather than the missing-session 400"""
    if session and path == '/mcp':
        headers = dict(session_headers(port), **headers)
    r = request(port, 'POST', body, headers, path, keep)
    if session and path == '/mcp' and r.get('status') == 404:
        headers.update(session_headers(port, refresh=True))
        r = request(port, 'POST', body, headers, path, keep)
    return r


def probe_p1(port, saved_dir):
    """Cross-origin text/plain POST, the request a web page can send without a CORS preflight."""
    headers = {'Origin': 'https://evil.example', 'Content-Type': 'text/plain'}
    listed = post(port, TOOLS_LIST, headers)
    marker = os.path.join(saved_dir, 'n2c_probe.txt')
    if os.path.exists(marker):
        os.remove(marker)
    code = "open(r'%s','w').write('probe')" % marker.replace('\\', '/')
    call = post(port, json.dumps({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
                                  'params': {'name': 'run-python', 'arguments': {'code': code}}}), headers)
    time.sleep(1)
    wrote = os.path.exists(marker)
    if wrote:
        os.remove(marker)
    served = listed.get('status') == 200 and '"tools"' in listed.get('body', '')
    acao = listed.get('headers', {}).get('access-control-allow-origin')
    status = 'reproduced' if (served or wrote or acao == '*') else 'fixed'
    return {'ids': ['mcp-transport-1', 'dx-docs-release-1', 'p0-1'], 'status': status,
            'evidence': {'tools_list': listed, 'run_python': call, 'marker_written': wrote}}


def probe_p2(port):
    """DNS rebinding: a request whose Host is not a loopback name."""
    r = post(port, TOOLS_LIST, {'Host': 'attacker.example', 'Content-Type': 'application/json'})
    status = 'reproduced' if r.get('status') == 200 else 'fixed'
    return {'ids': ['mcp-transport-1'], 'status': status, 'evidence': r}


def probe_p3(port):
    """A two-element JSON-RPC batch, sent in a 2025-03-26 session (batches exist only in that version) and in a
    2025-11-25 one. Fixed means the 2025-03-26 batch is answered element by element and the 2025-11-25 one is rejected
    with -32600 (card 0109)."""
    batch = json.dumps([{'jsonrpc': '2.0', 'id': 1, 'method': 'tools/list'},
                        {'jsonrpc': '2.0', 'id': 2, 'method': 'ping'}])
    evidence = {}
    outcome = {}
    for version in ('2025-03-26', '2025-11-25'):
        try:
            sid = open_session(port, 'n2c-probe-p3', version=version)
        except (OSError, RuntimeError) as e:
            evidence[version] = {'error': str(e)}
            continue
        # Parse the whole body (a tools/list answer runs to tens of KB), then keep 400 chars as evidence
        r = request(port, 'POST', batch, {'Content-Type': 'application/json', 'Mcp-Session-Id': sid,
                                          'MCP-Protocol-Version': version}, keep=None)
        request(port, 'DELETE', None, {'Mcp-Session-Id': sid})
        try:
            parsed = json.loads(r.get('body', ''))
        except ValueError:
            parsed = None
        if isinstance(parsed, list) and len(parsed) == 2:
            outcome[version] = 'answered'
        elif isinstance(parsed, dict) and (parsed.get('error') or {}).get('code') == -32600:
            outcome[version] = 'rejected'
        if 'body' in r:
            r['body'] = r['body'][:400]
        evidence[version] = r
    evidence['outcome'] = outcome
    fixed = outcome.get('2025-03-26') == 'answered' and outcome.get('2025-11-25') == 'rejected'
    return {'ids': ['mcp-transport-9'], 'status': 'fixed' if fixed else 'reproduced', 'evidence': evidence}


def probe_p8(port):
    """Per-client sessions on /mcp (card 0109): two initializes get two different Mcp-Session-Ids, each answering in
    its own session; a POST without Mcp-Session-Id is 400 and one with an unknown id 404; an unsupported
    MCP-Protocol-Version is 400; GET is 405 with Allow: POST, DELETE; a client response is 202 with no body; DELETE
    ends a session (2xx) and the next request on it is 404 while the other session still answers."""
    ids = ['next-1', 'mcp-transport-16', 'mcp-transport-18', 'mcp-transport-19']
    ping = json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'ping'})
    json_headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    checks, ev = {}, {}
    try:
        a = open_session(port, 'n2c-probe-p8-a')
        b = open_session(port, 'n2c-probe-p8-b', version='2025-06-18')
    except (OSError, RuntimeError) as e:
        return {'ids': ids, 'status': 'reproduced', 'evidence': {'error': str(e)}}
    checks['distinct_sessions'] = a != b

    def in_session(sid, body=ping, extra=None):
        headers = dict(json_headers, **{'Mcp-Session-Id': sid})
        headers.update(extra or {})
        return request(port, 'POST', body, headers)

    ra = in_session(a)
    rb = in_session(b)
    checks['a_answers_in_a'] = ra.get('status') == 200 and ra.get('headers', {}).get('mcp-session-id') == a
    checks['b_answers_in_b'] = rb.get('status') == 200 and rb.get('headers', {}).get('mcp-session-id') == b
    ev['no_session'] = request(port, 'POST', ping, json_headers)
    checks['no_session_400'] = ev['no_session'].get('status') == 400
    ev['unknown_session'] = in_session('n2c-probe-no-such-session')
    checks['unknown_session_404'] = ev['unknown_session'].get('status') == 404
    ev['bad_version'] = in_session(a, extra={'MCP-Protocol-Version': '1999-01-01'})
    checks['bad_version_400'] = ev['bad_version'].get('status') == 400
    ev['client_response'] = in_session(a, body=json.dumps({'jsonrpc': '2.0', 'id': 99, 'result': {}}))
    checks['client_response_202'] = ev['client_response'].get('status') == 202 and ev['client_response'].get('body') == ''
    ev['get'] = request(port, 'GET', None, {'Accept': 'text/event-stream', 'Mcp-Session-Id': a})
    checks['get_405_allow'] = ev['get'].get('status') == 405 and ev['get'].get('headers', {}).get('allow') == 'POST, DELETE'
    ev['delete'] = request(port, 'DELETE', None, {'Mcp-Session-Id': a})
    checks['delete_2xx'] = ev['delete'].get('status') in (200, 202, 204)
    ev['after_delete'] = in_session(a)
    checks['ended_session_404'] = ev['after_delete'].get('status') == 404
    checks['other_session_still_answers'] = in_session(b).get('status') == 200
    request(port, 'DELETE', None, {'Mcp-Session-Id': b})
    ev['checks'] = checks
    return {'ids': ids, 'status': 'fixed' if all(checks.values()) else 'reproduced', 'evidence': ev}


def probe_p5(port):
    """Whether the notification server (port+1) accepts a connection on a non-loopback local address."""
    addrs = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith('127.'):
                addrs.add(ip)
    except socket.gaierror:
        pass
    if not addrs:
        return {'ids': ['mcp-transport-14'], 'status': 'skipped', 'evidence': {'reason': 'no non-loopback IPv4 address'}}
    results = {}
    for ip in sorted(addrs):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2)
            results[ip] = s.connect_ex((ip, port + 1)) == 0
    status = 'reproduced' if any(results.values()) else 'fixed'
    return {'ids': ['mcp-transport-14'], 'status': status, 'evidence': {'accepted': results}}


def probe_p4(args, port, log_dir):
    """Port conflict: another process holds the MCP port; does the editor say it started anyway?

    A plain socket listener stands in for the second editor the finding describes (same effect on the bind,
    without copying the project)."""
    holder = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    holder.bind(('127.0.0.1', port))
    holder.listen(1)
    log_path = os.path.join(log_dir, 'p4_editor.log')
    try:
        # -ExecCmds=QUIT_EDITOR runs on the first tick, after StartupModule has tried the bind and logged the
        # outcome, so the editor closes itself cleanly; /mcp/health is not polled (the holder never answers)
        s = EditorSession(args.engine, args.project, port, log_path, allow_python=False,
                          extra_args=['-ExecCmds=QUIT_EDITOR'])
        s.start(timeout=0)
        try:
            s.proc.wait(240)
        except subprocess.TimeoutExpired:
            pass
        s.stop()
        text = s.log_text()
    finally:
        holder.close()
    claims = [l.strip()[:200] for l in text.splitlines() if 'MCP HTTP server started' in l or 'MCP HTTP server initialized' in l]
    failures = [l.strip()[:200] for l in text.splitlines() if 'Failed to start MCP' in l or 'port' in l.lower() and 'in use' in l.lower()]
    status = 'reproduced' if claims and not failures else ('fixed' if failures else 'skipped')
    return {'ids': ['mcp-transport-10'], 'status': status, 'evidence': {'success_lines': claims, 'failure_lines': failures}}


def raw_exchange(port, chunks, timeout=20):
    """Send raw request bytes to 127.0.0.1:port and read the status line. Returns (status or None, error).

    The server may answer and close while the request is still being sent; a send error is recorded and the
    response is still read if one arrived. A 200 status line means the request was accepted."""
    status, error = None, None
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        try:
            s.connect(('127.0.0.1', port))
        except OSError as e:
            return None, 'connect: %s' % e
        try:
            for c in chunks:
                s.sendall(c)
        except OSError as e:
            error = 'send: %s' % e
        buf = b''
        try:
            while b'\r\n' not in buf and len(buf) < 4096:
                data = s.recv(4096)
                if not data:
                    break
                buf += data
        except OSError as e:
            error = (error + '; ' if error else '') + 'recv: %s' % e
        line = buf.split(b'\r\n', 1)[0].decode('latin-1', 'replace')
        parts = line.split(' ')
        if len(parts) >= 2 and parts[0].startswith('HTTP/') and parts[1].isdigit():
            status = int(parts[1])
    return status, error


def working_set_kb(pid):
    """Working set of a process in KB (Windows tasklist), or None."""
    if os.name != 'nt' or not pid:
        return None
    try:
        out = subprocess.run(['tasklist', '/FI', 'PID eq %d' % pid, '/FO', 'CSV', '/NH'],
                             capture_output=True, text=True, timeout=30).stdout
        field = out.strip().splitlines()[0].split('","')[-1]
        return int(''.join(ch for ch in field if ch.isdigit()))
    except (OSError, IndexError, ValueError, subprocess.SubprocessError):
        return None


def probe_p6(port, pid):
    """Request-size caps on the notification server (port+1): 10,000 headers, a 10 MB chunked body and a 4 KB
    request target sent to /mcp/notifications must each be refused (no 200), the server must keep answering, and
    the editor's working set is recorded before and after."""
    sse = port + 1
    head = b'GET /mcp/notifications HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n' % sse
    alive_req = [b'GET /not-a-route HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n' % sse]
    before_alive = raw_exchange(sse, alive_req)
    mem_before = working_set_kb(pid)

    many_headers = head + b''.join(b'X-Pad-%05d: v\r\n' % i for i in range(10000)) + b'\r\n'
    headers_result = raw_exchange(sse, [many_headers])

    chunk = b'x' * 65536
    def chunked():
        yield head + b'Transfer-Encoding: chunked\r\n\r\n'
        for _ in range(160):  # 160 x 64 KB = 10 MB
            yield b'%x\r\n' % len(chunk) + chunk + b'\r\n'
        yield b'0\r\n\r\n'
    body_result = raw_exchange(sse, chunked())

    long_uri = [b'GET /mcp/notifications?' + b'a' * 4096 + b' HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n' % sse]
    uri_result = raw_exchange(sse, long_uri)

    time.sleep(2)
    mem_after = working_set_kb(pid)
    after_alive = raw_exchange(sse, alive_req)

    cases = {'headers_10000': headers_result, 'chunked_body_10mb': body_result, 'uri_4kb': uri_result}
    refused = all(r[0] != 200 for r in cases.values())
    answering = before_alive[0] is not None and after_alive[0] == before_alive[0]
    status = 'fixed' if (refused and answering) else 'reproduced'
    evidence = {k: {'status': v[0], 'error': v[1]} for k, v in cases.items()}
    evidence['alive_before'] = before_alive[0]
    evidence['alive_after'] = after_alive[0]
    evidence['working_set_kb'] = {'before': mem_before, 'after': mem_after,
                                  'delta': (mem_after - mem_before) if mem_before and mem_after else None}
    return {'ids': ['auth-settings-module-17', 'dx-docs-release-4', 'p0-9'], 'status': status, 'evidence': evidence}


def probe_p7(port):
    """translate-focused-blueprint answered on the same POST. Two calls, each with a numeric progressToken: one whose
    Accept lists text/event-stream (UE 5.8 answers with an SSE stream of notifications/progress and then the response;
    5.6/5.7 with one JSON response) and one that accepts only JSON (one JSON response everywhere). Headless no
    Blueprint is focused, so each result is an isError CallToolResult. Fixed when both responses carry a CallToolResult
    (a content array), neither has the old 'accepted'/sseUrl result, and any progress echoes the token as a number."""
    from live_env_translation import ToolCallStream  # noqa: E402
    calls = {}
    for label, accept in (('stream', ToolCallStream.ACCEPT_STREAM), ('json', 'application/json')):
        call = ToolCallStream(port, 'translate-focused-blueprint', {}, 7, meta={'progressToken': 7}, accept=accept, timeout=60)
        if not call.wait(60):
            call.close()
        responses = call.responses()
        progress = [json.loads(d) for _, e, d in call.events if e == 'progress']
        try:
            message = json.loads(responses[0][2]) if responses else {}
        except ValueError:
            message = {}
        result = message.get('result') or {}
        calls[label] = {
            'status': call.status, 'content_type': call.content_type, 'events': [(t, e) for t, e, _ in call.events],
            'responses': len(responses), 'closed': call.closed_at is not None,
            'call_tool_result': isinstance(result.get('content'), list) and len(result['content']) > 0,
            'is_error': bool(result.get('isError')), 'id': message.get('id'),
            'text_head': ''.join(c.get('text', '') for c in result.get('content') or [] if isinstance(c, dict))[:200],
            'old_accepted_shape': 'sseUrl' in (responses[0][2] if responses else '') or result.get('status') == 'accepted',
            'progress_tokens': sorted({type(m.get('params', {}).get('progressToken')).__name__ for m in progress}),
            'progress_values': [m.get('params', {}).get('progress') for m in progress],
            'progress_messages': [m.get('params', {}).get('message') for m in progress],
        }
    ok = all(c['responses'] == 1 and c['call_tool_result'] and not c['old_accepted_shape'] and c['id'] == 7 and c['closed']
             and c['progress_tokens'] in ([], ['int']) for c in calls.values())
    return {'ids': ['p0-10', 'mcp-transport-3', 'mcp-transport-21', 'dx-docs-release-8'],
            'status': 'fixed' if ok else 'reproduced', 'evidence': calls}


def session_probes(port, pid, saved_dir):
    """The probes that run against an editor already answering on port (all but P4, which needs its own editor).
    Tests/smoke/smoke_check.py calls this inside its own session."""
    return {'P1': probe_p1(port, saved_dir), 'P2': probe_p2(port), 'P3': probe_p3(port), 'P5': probe_p5(port),
            'P6': probe_p6(port, pid), 'P7': probe_p7(port), 'P8': probe_p8(port)}


def mismatches(results, label):
    """{probe: {expected, actual}} for every status that differs from expected.json for the label"""
    expected = json.load(open(os.path.join(HERE, 'expected.json'), encoding='utf-8')).get(label, {})
    return {k: {'expected': expected.get(k), 'actual': v['status']} for k, v in results.items()
            if expected.get(k) != v['status']}


def main():
    ap = argparse.ArgumentParser(description='NodeToCode MCP transport probes')
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True, help='engine label in expected.json, e.g. 5.8')
    ap.add_argument('--port', type=int, default=27150)
    ap.add_argument('--report', default=None)
    ap.add_argument('--skip-p4', action='store_true', help='skip the port-conflict probe (it launches a second editor)')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    port = free_port(args.port)
    saved_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved')
    results = {}
    session = EditorSession(args.engine, args.project, port, os.path.join(log_dir, 'editor_%s.log' % args.label))
    try:
        if not session.start():
            results['session'] = {'status': 'skipped', 'evidence': {'reason': 'editor did not answer /mcp/health'}}
        else:
            results.update(session_probes(port, session.proc.pid, saved_dir))
    finally:
        session.stop()
    if args.skip_p4:
        results['P4'] = {'ids': ['mcp-transport-10'], 'status': 'skipped', 'evidence': {'reason': '--skip-p4'}}
    else:
        results['P4'] = probe_p4(args, free_port(port + 2), log_dir)

    diff = mismatches(results, args.label)
    report = {'label': args.label, 'port': port, 'results': results, 'mismatches': diff}
    out = args.report or os.path.join(log_dir, 'report_%s.json' % args.label)
    with open(out, 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1)
    print(json.dumps({k: v['status'] for k, v in results.items()}), 'mismatches:', json.dumps(diff))
    return 0 if not diff else 1


if __name__ == '__main__':
    sys.exit(main())
