"""Live probes for NodeToCode's MCP transport findings, against a headless editor on a host project.

Stdlib only; run with `python -I`:

    python -I mcp_probe.py --engine G:/UE/UE_5.8 --project G:/.../NodeToCodeHost_5_8.uproject --label 5.8

Writes a JSON report (--report) with, per probe, the finding ids, a status (reproduced, fixed, skipped or not_applicable) and raw
evidence. Exits 0 only when every status matches expected.json for the label. See README.md.
"""
import argparse
import http.client
import json
import os
import re
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


def probe_p5(port, pid=None):
    """Nothing listens on port+1 any more. Until card 0113 a notification server (cpp-httplib, /mcp/events and
    /mcp/notifications) listened there; mcp-transport-14 was that it accepted connections on non-loopback addresses.
    Reproduced when a connection to port+1 succeeds on 127.0.0.1 or any non-loopback local IPv4 address, or (Windows
    netstat) the editor process listens on port+1. The session picks a port whose port+1 was free at start."""
    addrs = {'127.0.0.1'}
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addrs.add(info[4][0])
    except socket.gaierror:
        pass
    accepted = {}
    for ip in sorted(addrs):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2)
            accepted[ip] = s.connect_ex((ip, port + 1)) == 0
    editor_listens = None
    if os.name == 'nt' and pid:
        try:
            out = subprocess.run(['netstat', '-ano', '-p', 'TCP'], capture_output=True, text=True, timeout=30).stdout
            editor_listens = any(len(p) >= 5 and p[0] == 'TCP' and p[3] == 'LISTENING' and p[1].endswith(':%d' % (port + 1))
                                 and p[4] == str(pid) for p in (line.split() for line in out.splitlines()))
        except (OSError, subprocess.SubprocessError):
            editor_listens = None
    status = 'reproduced' if any(accepted.values()) or editor_listens else 'fixed'
    return {'ids': ['mcp-transport-14', 'next-1'], 'status': status,
            'evidence': {'port_plus_1': port + 1, 'accepted': accepted, 'editor_listens_netstat': editor_listens}}


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
    # Reproduced: the editor says it serves on the held port. Fixed (card 0112): it says the held port is in use, and
    # either serves on another port (the fallback range) or reports that it did not start.
    claims = [l.strip()[:200] for l in text.splitlines() if 'MCP HTTP server started' in l or 'MCP HTTP server initialized' in l]
    claimed_ports = sorted({int(m.group(1)) for l in claims for m in [re.search(r'started on port (\d+)', l)] if m})
    failures = [l.strip()[:200] for l in text.splitlines() if 'Failed to start MCP' in l or 'port' in l.lower() and 'in use' in l.lower()]
    if port in claimed_ports:
        status = 'reproduced'
    elif failures:
        status = 'fixed'
    else:
        status = 'skipped'
    return {'ids': ['mcp-transport-10'], 'status': status,
            'evidence': {'held_port': port, 'claimed_ports': claimed_ports, 'success_lines': claims, 'failure_lines': failures}}


def probe_p6(port, pid):
    """Request-size caps on the notification server (port+1). That server was removed in card 0113, so there is
    nothing to send oversized requests to; P5 checks that port+1 is closed. The /mcp server is the engine's HTTPServer,
    whose limits are the engine's."""
    return {'ids': ['auth-settings-module-17', 'dx-docs-release-4', 'p0-9'], 'status': 'not_applicable',
            'evidence': {'reason': 'the port+1 notification server was removed (card 0113); P5 checks port+1 is closed'}}


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


def probe_p9(port):
    """Capabilities, error codes and cancellation (card 0110, MCP 2025-11-25): initialize advertises no logging and
    returns instructions; resources/templates/list lists nodetocode://blueprint/{name} and resources/list no longer
    carries resourceTemplates; an unknown tool and an unknown prompt are -32602, a missing required prompt argument
    (python-scripting's task) is -32602, an unknown resource uri is -32002 with the uri in data; notifications/cancelled
    for an id with no running call is accepted with 202 and no body. The live cancel of a running call is
    live_translate_cancel.py's (it needs a provider key)."""
    ids = ['next-1', 'mcp-transport-15', 'mcp-transport-19', 'mcp-transport-20',
           'mcp-tools-python-translation-resources-16', 'mcp-tools-python-translation-resources-23', 'imp-mcp-transport-5']
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    checks, ev = {}, {}

    def rpc(rid, method, params=None):
        body = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            body['params'] = params
        r = post(port, json.dumps(body), headers, keep=None)
        try:
            message = json.loads(r.get('body') or '')
        except ValueError:
            message = {}
        ev[method if method not in ev else '%s#%d' % (method, rid)] = {'status': r.get('status'),
                                                                        'body': (r.get('body') or '')[:300]}
        return message

    def error_code(message):
        return (message.get('error') or {}).get('code')

    init = request(port, 'POST', json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
        'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'n2c-probe-p9', 'version': '1'}}}),
        headers, keep=None)
    try:
        result = json.loads(init.get('body') or '').get('result') or {}
    except ValueError:
        result = {}
    if init.get('headers', {}).get('mcp-session-id'):
        request(port, 'DELETE', None, {'Mcp-Session-Id': init['headers']['mcp-session-id']})
    capabilities = result.get('capabilities') or {}
    ev['capabilities'] = capabilities
    checks['no_logging_capability'] = bool(capabilities) and 'logging' not in capabilities
    checks['instructions'] = isinstance(result.get('instructions'), str) and 'get-focused-blueprint' in result['instructions']

    templates = (rpc(2, 'resources/templates/list').get('result') or {}).get('resourceTemplates')
    checks['templates_list'] = isinstance(templates, list) and any(
        t.get('uriTemplate') == 'nodetocode://blueprint/{name}' for t in templates if isinstance(t, dict))
    listed = rpc(3, 'resources/list').get('result') or {}
    checks['resources_list_without_templates'] = isinstance(listed.get('resources'), list) and 'resourceTemplates' not in listed

    checks['unknown_tool_32602'] = error_code(rpc(4, 'tools/call', {'name': 'n2c-probe-no-such-tool', 'arguments': {}})) == -32602
    missing = rpc(5, 'resources/read', {'uri': 'nodetocode://no/such"resource'})
    checks['unknown_resource_32002'] = error_code(missing) == -32002 \
        and ((missing.get('error') or {}).get('data') or {}).get('uri') == 'nodetocode://no/such"resource'
    checks['unknown_prompt_32602'] = error_code(rpc(6, 'prompts/get', {'name': 'n2c-probe-no-such-prompt'})) == -32602
    checks['missing_prompt_argument_32602'] = error_code(rpc(7, 'prompts/get', {'name': 'python-scripting'})) == -32602

    cancelled = post(port, json.dumps({'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                                       'params': {'requestId': 424242, 'reason': 'probe'}}), headers)
    ev['cancel_unknown'] = cancelled
    checks['cancel_unknown_id_202'] = cancelled.get('status') == 202 and cancelled.get('body') == ''
    checks['still_answers'] = 'result' in rpc(8, 'ping')
    ev['checks'] = checks
    return {'ids': ids, 'status': 'fixed' if all(checks.values()) else 'reproduced', 'evidence': ev}


def session_probes(port, pid, saved_dir):
    """The probes that run against an editor already answering on port (all but P4, which needs its own editor).
    Tests/smoke/smoke_check.py calls this inside its own session."""
    return {'P1': probe_p1(port, saved_dir), 'P2': probe_p2(port), 'P3': probe_p3(port), 'P5': probe_p5(port, pid),
            'P6': probe_p6(port, pid), 'P7': probe_p7(port), 'P8': probe_p8(port), 'P9': probe_p9(port)}


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
