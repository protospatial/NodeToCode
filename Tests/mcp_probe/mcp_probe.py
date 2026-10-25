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


MAX_BODY = 8 * 1024 * 1024  # FN2CMcpProtocol::MaxRequestBodyBytes (card 0425)


def sized_ping(size):
    """A JSON-RPC ping whose UTF-8 body is exactly size bytes (padded with a params string)"""
    head, tail = '{"jsonrpc":"2.0","id":6,"method":"ping","params":{"pad":"', '"}}'
    return head + 'x' * (size - len(head) - len(tail)) + tail


def probe_p6(port, pid):
    """/mcp request body cap (card 0425). Until then a body of any size was read and parsed (live 0090: a 32 MB ping
    was answered 200). In this probe's session: a ping one byte over 8 MiB, and a 1 MiB ping as a control. Fixed when
    the oversized one is answered 413 with a JSON-RPC -32600 error (null id) and the control 200 with a result;
    reproduced when the oversized one is answered 200. The engine's HTTPServer still reads the whole body before
    NodeToCode sees it (no pre-body hook on 5.6-5.8); the evidence records how long that took."""
    headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    start = time.time()
    over = post(port, sized_ping(MAX_BODY + 1), headers)
    over_seconds = round(time.time() - start, 2)
    control = post(port, sized_ping(1024 * 1024), headers)
    try:
        over_msg = json.loads(over.get('body') or '')
    except ValueError:
        over_msg = {}
    try:
        control_msg = json.loads(control.get('body') or '')
    except ValueError:
        control_msg = {}
    over_ok = (over.get('status') == 413 and over_msg.get('id', 'missing') is None
               and (over_msg.get('error') or {}).get('code') == -32600)
    control_ok = control.get('status') == 200 and 'result' in control_msg
    status = 'fixed' if over_ok and control_ok else 'reproduced' if over.get('status') == 200 else 'skipped'
    return {'ids': ['auth-settings-module-17', 'dx-docs-release-4', 'p0-9'], 'status': status, 'evidence': {
        'limit_bytes': MAX_BODY, 'over': {'bytes': MAX_BODY + 1, 'status': over.get('status'),
                                          'body': over.get('body'), 'error': over.get('error'), 'seconds': over_seconds},
        'control_1MiB': {'status': control.get('status'), 'body': (control.get('body') or '')[:200],
                         'error': control.get('error')}}}


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


# ---------------------------------------------------------------------------------------------------------------------
# Content probes (card 0097): P10-P12 on the committed fixture Tests/Fixtures/N2CProbe/BP_N2CProbe.uasset, an Actor
# Blueprint whose Event BeginPlay is wired to a Print String ("N2CProbe BeginPlay"). main() copies it into the host's
# Content/N2CProbe/ before the editor starts and removes it after (make_probe_fixture.py rebuilds it on UE 5.6).
# ---------------------------------------------------------------------------------------------------------------------

FIXTURE = os.path.join(os.path.dirname(HERE), 'Fixtures', 'N2CProbe', 'BP_N2CProbe.uasset')
FIXTURE_ASSET = '/Game/N2CProbe/BP_N2CProbe'

# Focus the fixture's EventGraph and describe it: N2C JSON node count, execution and data links, BeginPlay's state,
# and whether the package is dirty
FIXTURE_STATE_PY = r'''
import json, unreal
opened = json.loads(unreal.N2CPythonBridge.open_blueprint("%(asset)s", "EventGraph"))
state = {"opened": bool(opened.get("success")), "open_error": opened.get("error")}
if state["opened"]:
    raw = json.loads(unreal.N2CPythonBridge.get_focused_blueprint_json())
    graphs = ((raw.get("data") or {}).get("n2c_json") or {}).get("graphs") or []
    graph = next((g for g in graphs if g.get("name") == "EventGraph"), None)
    if graph:
        flows = graph.get("flows") or {}
        nodes = graph.get("nodes") or []
        begin = [n for n in nodes if n.get("member_name") == "ReceiveBeginPlay"]
        state.update({
            "nodes": len(nodes),
            # One per entry ("N1.P2->N3.P4", N2C JSON 2.0.0 since card 0136); a 1.x chain has one per arrow
            "exec_links": sum(f.count("->") for f in flows.get("execution") or []),
            # N2C JSON 2.0.0: a data value is one input pin or a list of them (fan-out)
            "data_links": sum(len(v) if isinstance(v, list) else 1 for v in (flows.get("data") or {}).values()),
            "execution": flows.get("execution"),
            "begin_play": len(begin),
            "begin_play_exec_linked": any(p.get("connected") for n in begin for p in n.get("output_pins") or []
                                          if p.get("type") is None),
            "node_names": [n.get("member_name") or n.get("title") or n.get("class") for n in nodes]})
    else:
        state["n2c_error"] = raw.get("error")
dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
state["dirty"] = "%(asset)s" in dirty
result = state
'''

# The bridge calls of python-bridge-1 with an apostrophe in the text: each raw string goes through json.loads
APOSTROPHE_PY = r'''
import json, unreal
B = unreal.N2CPythonBridge
out = {}
def attempt(name, fn):
    try:
        raw = fn()
    except Exception as e:
        out[name] = {"raised": str(e)[:200]}
        return {}
    try:
        msg = json.loads(raw)
        out[name] = {"parsed": True, "success": msg.get("success"), "error": msg.get("error"),
                     "data_head": json.dumps(msg.get("data"))[:200]}
        return msg
    except ValueError as e:
        out[name] = {"parsed": False, "json_error": str(e)[:200], "raw_head": raw[:200]}
        return {}
opened = json.loads(B.open_blueprint("%(asset)s", "EventGraph")).get("success")
# Leftovers of an aborted run
try:
    for t in (json.loads(B.list_tags("", "it's")).get("data") or {}).get("tags") or []:
        B.remove_tag(t.get("graph_guid", ""), "it's")
except ValueError:
    pass
tagged = attempt("tag_focused_graph", lambda: B.tag_focused_graph("it's", "N2CProbe", "the probe's tag"))
guid = (tagged.get("data") or {}).get("graph_guid", "")
listed = attempt("list_tags", lambda: B.list_tags("", "it's"))
out["list_tags"]["names"] = [t.get("tag") for t in (listed.get("data") or {}).get("tags") or []]
attempt("remove_tag", lambda: B.remove_tag(guid, "it's"))
attempt("remove_tag_missing", lambda: B.remove_tag(guid, "it's"))
attempt("open_blueprint_function", lambda: B.open_blueprint_function("Missing'Func"))
result = {"opened": opened, "graph_guid": guid, "calls": out}
'''

# Opens the fixture, places one fixture actor in the editor level (UE 5.8 only, see below) so PIE has a live instance
# for the compile to reinstance (its BeginPlay prints "N2CProbe BeginPlay"), and requests PIE. On UE 5.6 and 5.7
# headless (-nullrhi -RenderOffScreen), placing any Blueprint actor through EditorActorSubsystem.spawn_actor_from_class
# (the viewport placement path) is a fatal EXCEPTION_INT_DIVIDE_BY_ZERO in the engine, with or without NodeToCode
# involved (a plain AActor places fine), and stock Python has no game-world spawn (BeginDeferredActorSpawnFromClass
# is internal), so there PIE runs without a fixture instance and the evidence says so
PIE_START_PY = r'''
import json, unreal
opened = json.loads(unreal.N2CPythonBridge.open_blueprint("%(asset)s", "EventGraph")).get("success")
version = unreal.SystemLibrary.get_engine_version()
placed = None
if not version.startswith(("5.6", "5.7")):
    bp = unreal.load_asset("%(asset)s")
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        bp.generated_class(), unreal.Vector(0, 0, 0)) if bp else None
    placed = actor.get_name() if actor else None
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
result = {"opened": opened, "engine": version, "placed_in_level": placed,
          "live_instance": "placed in the editor level" if placed else
                           "none: placing a Blueprint actor headless is fatal on UE 5.6/5.7" if placed is None and
                           version.startswith(("5.6", "5.7")) else "placement failed",
          "editor_world": unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()}
'''

# Removes the fixture actors P12 placed in the editor level
PIE_CLEANUP_PY = r'''
import unreal
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
removed = 0
for a in eas.get_all_level_actors():
    if "N2CProbe" in a.get_class().get_name():
        removed += 1 if eas.destroy_actor(a) else 0
result = {"removed_actors": removed}
'''

PIE_STATE_PY = r'''
import json, unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
probes = []
if world:
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        cls = a.get_class().get_name()
        if "N2CProbe" in cls:
            probes.append(cls)
result = {"in_pie": les.is_in_play_in_editor(), "game_world": world.get_path_name() if world else None,
          "pie_probe_actors": probes}
'''

PIE_REFOCUS_PY = r'''
import json, unreal
result = {"opened": json.loads(unreal.N2CPythonBridge.open_blueprint("%(asset)s", "EventGraph")).get("success")}
'''

PIE_END_PY = r'''
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
result = {"requested": True}
'''

def _helpers():
    import e05_regressions  # noqa: E402  (its MCP helpers: run_python, tool_json, graph_nodes)
    return e05_regressions


def install_fixture(project):
    """Copy the fixture into the host's Content/N2CProbe/; returns the folder to remove afterwards (None when the
    folder was already there, so only the file is put back)"""
    import shutil
    folder = os.path.join(os.path.dirname(os.path.abspath(project)), 'Content', 'N2CProbe')
    created = not os.path.isdir(folder)
    os.makedirs(folder, exist_ok=True)
    shutil.copy2(FIXTURE, os.path.join(folder, os.path.basename(FIXTURE)))
    return folder if created else None


def remove_fixture(project, folder):
    import shutil
    if folder:
        shutil.rmtree(folder, ignore_errors=True)
    else:
        path = os.path.join(os.path.dirname(os.path.abspath(project)), 'Content', 'N2CProbe', os.path.basename(FIXTURE))
        if os.path.exists(path):
            os.remove(path)


def log_problems(text):
    """NodeToCode error lines, ensures and crash lines in an editor log excerpt"""
    keep = []
    for line in text.splitlines():
        low = line.lower()
        if ('LogNodeToCode' in line and 'Error' in line) or 'ensure condition failed' in low \
                or 'Fatal' in line or 'Unhandled Exception' in line or 'Assertion failed' in line \
                or ('LogBlueprint' in line and 'Error' in line) or 'LogOutputDevice: Error' in line:
            keep.append(line.strip()[:240])
    return keep


def probe_p10(url):
    """search-blueprint-nodes {"searchTerm": "begin play"} on the fixture's focused EventGraph, whose Event BeginPlay
    is wired to a Print String. Reproduced when the search changes the node count, the link count, BeginPlay or its
    exec link, or dirties the package (mcp-tools-graph-functions-1/-2: the spawner returned the existing BeginPlay and
    the tool removed it outside a transaction). Skipped when no graph can be focused."""
    ids = ['gap-1', 'mcp-tools-graph-functions-1', 'mcp-tools-graph-functions-2', 'p0-7']
    h = _helpers()
    before, raw = h.run_python(url, FIXTURE_STATE_PY % {'asset': FIXTURE_ASSET})
    if not before.get('opened') or 'nodes' not in before:
        return {'ids': ids, 'status': 'skipped',
                'evidence': {'reason': 'could not focus the fixture EventGraph', 'state': before, 'raw': raw}}
    begin_before, _ = h.graph_nodes(url, 'BeginPlay')
    data, error, text = h.tool_json(url, 'search-blueprint-nodes', {'searchTerm': 'begin play'})
    after, _ = h.run_python(url, FIXTURE_STATE_PY % {'asset': FIXTURE_ASSET})
    begin_after, _ = h.graph_nodes(url, 'BeginPlay')
    keys = ('nodes', 'exec_links', 'data_links', 'begin_play', 'begin_play_exec_linked', 'dirty')
    changed = {k: [before.get(k), after.get(k)] for k in keys if before.get(k) != after.get(k)}
    begin_guid_kept = bool(begin_before) and set(begin_before) <= set(begin_after)
    status = 'reproduced' if changed or not begin_guid_kept or not after.get('begin_play_exec_linked') else 'fixed'
    return {'ids': ids, 'status': status, 'evidence': {
        'search_error': error, 'search_results': len((data or {}).get('nodes', [])),
        'search_names': [n.get('title') or n.get('member_name') for n in (data or {}).get('nodes', [])][:10],
        'before': {k: before.get(k) for k in keys + ('execution',)},
        'after': {k: after.get(k) for k in keys + ('execution',)},
        'begin_play_guids_before': sorted(begin_before), 'begin_play_guids_after': sorted(begin_after),
        'changed': changed}}


def probe_p11(url):
    """The Python bridge's results with an apostrophe in the text go through json.loads: tag_focused_graph("it's"),
    list_tags(tag="it's"), remove_tag(<guid>, "it's") (and again, for its "not found" error, which quotes the tag)
    and open_blueprint_function("Missing'Func") (its error quotes the name). Reproduced when any fails to parse
    (python-bridge-1: ReplaceCharWithEscapedChar wrote the escape \\', which JSON does not have)."""
    ids = ['gap-1', 'python-bridge-1', 'qw-14']
    h = _helpers()
    data, raw = h.run_python(url, APOSTROPHE_PY % {'asset': FIXTURE_ASSET})
    calls = data.get('calls') or {}
    if not data.get('opened') or not calls:
        return {'ids': ids, 'status': 'skipped', 'evidence': {'reason': 'could not focus the fixture', 'raw': raw}}
    failed = sorted(k for k, v in calls.items() if not v.get('parsed'))
    # Fixed also needs the calls to have done what they say: tag, list it by its name, remove it, then the two errors
    # carry the apostrophe text
    behaved = (calls.get('tag_focused_graph', {}).get('success') is True
               and "it's" in calls.get('list_tags', {}).get('names', [])
               and calls.get('remove_tag', {}).get('success') is True
               and "it's" in (calls.get('remove_tag_missing', {}).get('error') or '')
               and "Missing'Func" in (calls.get('open_blueprint_function', {}).get('error') or ''))
    status = 'reproduced' if failed else ('fixed' if behaved else 'reproduced')
    return {'ids': ids, 'status': status, 'evidence': {'failed_to_parse': failed, 'behaved': behaved, 'calls': calls}}


def probe_p12(url, log_text):
    """create-variable and compile-blueprint on the fixture while Play In Editor runs (gap-1 step 7). Starts PIE with
    LevelEditorSubsystem.editor_request_begin_play() (on UE 5.8 after placing a fixture actor in the level, so the
    compile has a live PIE instance to reinstance; see PIE_START_PY), waits for is_in_play_in_editor(), calls both tools, ends PIE and lists the fixture's
    variables. Records the tool results, NodeToCode error, ensure and crash lines from the log, whether the editor
    still answers, and the variables after PIE. Reproduced when create-variable succeeds during PIE (card 0170 makes
    mutating tools refuse with PIE_ACTIVE); fixed when it is refused and nothing else went wrong. Skipped when PIE does
    not start."""
    ids = ['gap-1', 'next-10']
    h = _helpers()
    ev = {}
    mark = len(log_text())
    ev['start'], _ = h.run_python(url, PIE_START_PY % {'asset': FIXTURE_ASSET})
    deadline = time.time() + 90
    state = {}
    while time.time() < deadline:
        state, raw = h.run_python(url, PIE_STATE_PY)
        if state.get('in_pie') and state.get('game_world'):
            break
        time.sleep(1)
    ev['pie'] = state
    if not state.get('in_pie'):
        ev['cleanup'], _ = h.run_python(url, PIE_CLEANUP_PY)
        return {'ids': ids, 'status': 'skipped', 'evidence': dict(ev, reason='PIE did not start within 90 s',
                                                                 log=log_problems(log_text()[mark:]))}
    time.sleep(2)  # let the placed instance's BeginPlay run and print
    ev['refocus'], _ = h.run_python(url, PIE_REFOCUS_PY % {'asset': FIXTURE_ASSET})
    data, error, text = h.tool_json(url, 'create-variable', {'variableName': 'N2CProbePieVar', 'typeIdentifier': 'bool'})
    ev['create_variable'] = {'isError': error, 'text': text[:400]}
    data, error, text = h.tool_json(url, 'compile-blueprint', {})
    ev['compile'] = {'isError': error, 'text': text[:400], 'success': (data or {}).get('success'),
                     'status': (data or {}).get('compilationStatus')}
    ev['pie_after_tools'], _ = h.run_python(url, PIE_STATE_PY)
    h.run_python(url, PIE_END_PY)
    deadline = time.time() + 60
    while time.time() < deadline:
        ended, _ = h.run_python(url, PIE_STATE_PY)
        if ended.get('in_pie') is False:
            break
        time.sleep(1)
    ev['pie_ended'] = ended.get('in_pie') is False
    h.run_python(url, PIE_REFOCUS_PY % {'asset': FIXTURE_ASSET})
    data, error, text = h.tool_json(url, 'get-blueprint-member-variables', {})
    ev['variables_after'] = [v.get('name') for v in (data or {}).get('variables', [])] if not error else text[:200]
    ev['cleanup'], _ = h.run_python(url, PIE_CLEANUP_PY)
    ev['editor_answers'] = 'result' in h.rpc(url, 'ping')
    excerpt = log_text()[mark:]
    ev['begin_play_printed'] = 'N2CProbe BeginPlay' in excerpt
    ev['log_problems'] = log_problems(excerpt)[:30]
    ev['ensures'] = sum(1 for l in excerpt.splitlines() if 'ensure condition failed' in l.lower())
    refused = ev['create_variable']['isError'] and 'PIE' in ev['create_variable']['text']
    clean = ev['editor_answers'] and not ev['ensures'] and ev['pie_ended']
    status = 'fixed' if refused and clean else 'reproduced'
    return {'ids': ids, 'status': status, 'evidence': ev}


def content_probes(url, log_text):
    """P10-P12 against an editor that has the fixture under /Game/N2CProbe, run-python on and -RenderOffScreen. P12
    starts and ends PIE, so it runs last."""
    results = {}
    for name, ids, run in (('P10', ['gap-1', 'mcp-tools-graph-functions-1', 'mcp-tools-graph-functions-2', 'p0-7'],
                            lambda: probe_p10(url)),
                           ('P11', ['gap-1', 'python-bridge-1', 'qw-14'], lambda: probe_p11(url)),
                           ('P12', ['gap-1', 'next-10'], lambda: probe_p12(url, log_text))):
        try:
            results[name] = run()
        except Exception as e:  # the editor died or stopped answering: a mismatch someone has to look at
            results[name] = {'ids': ids, 'status': 'skipped', 'evidence': {
                'reason': 'probe raised %s: %s' % (type(e).__name__, e), 'log': log_problems(log_text())[-20:]}}
    return results


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
    ap.add_argument('--skip-content', action='store_true',
                    help='skip the fixture probes P10-P12 (BeginPlay deletion, apostrophe JSON, PIE mutation)')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    port = free_port(args.port)
    saved_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved')
    results = {}
    fixture_dir = None if args.skip_content else install_fixture(args.project)
    # -RenderOffScreen: P10-P12 open a Blueprint editor, and on UE 5.6 under -nullrhi its window otherwise reaches
    # FGenericWindow::GetRestoredDimensions, which is Fatal
    session = EditorSession(args.engine, args.project, port, os.path.join(log_dir, 'editor_%s.log' % args.label),
                            extra_args=['-RenderOffScreen'])
    try:
        if not session.start():
            results['session'] = {'status': 'skipped', 'evidence': {'reason': 'editor did not answer /mcp/health'}}
        else:
            results.update(session_probes(port, session.proc.pid, saved_dir))
            if not args.skip_content:
                results.update(content_probes('http://127.0.0.1:%d/mcp' % port, session.log_text))
            # Quit from inside: UnrealEditor-Cmd ignores the WM_CLOSE stop() sends first. Nothing is saved, so the
            # fixture's in-memory changes (P12) are dropped
            try:
                _helpers().run_python('http://127.0.0.1:%d/mcp' % port,
                                      'import unreal\nunreal.SystemLibrary.quit_editor()\nresult = {}')
            except Exception:  # the editor may close the connection while it exits
                pass
            deadline = time.time() + 60
            while time.time() < deadline and session.proc.poll() is None:
                time.sleep(0.5)
    finally:
        session.stop()
        if not args.skip_content:
            remove_fixture(args.project, fixture_dir)
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
