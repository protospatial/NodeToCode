"""The generated MCP tool reference against a live server (card 0179).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I live_tool_reference_0179.py --engine G:/UE/UE_5.8 --project <host>.uproject

Starts a headless editor with -N2CMcp twice through editor_session.py, once with Allow MCP Python Execution on and
once with it off, and sends initialize and tools/list (following nextCursor) to /mcp. Passes when, with it on,
tools/list names exactly the tools of Context/mcp-server/tools.generated.json and each listed definition equals the
reference's title, description, inputSchema, outputSchema, annotations and _meta; and with it off, exactly the tools the reference marks
defaultEnabled. Prints one JSON line; exits 0 on a pass.
"""
import argparse
import http.client
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
REFERENCE = os.path.join(REPO_ROOT, 'Context', 'mcp-server', 'tools.generated.json')
HEADERS = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
           'MCP-Protocol-Version': '2025-11-25'}


def post(port, payload, session_id=None):
    headers = dict(HEADERS)
    if session_id:
        headers['Mcp-Session-Id'] = session_id
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'), headers=headers)
        response = conn.getresponse()
        body = response.read().decode('utf-8', 'replace')
        return {k.lower(): v for k, v in response.getheaders()}, body
    finally:
        conn.close()


def parse(body):
    """A JSON body, or the last data: line of an SSE body"""
    text = body.strip()
    if not text.startswith('{'):
        lines = [line[5:].strip() for line in text.splitlines() if line.startswith('data:')]
        text = lines[-1] if lines else ''
    return json.loads(text) if text else None


def live_tools(port):
    headers, body = post(port, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
        'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'live_tool_reference_0179', 'version': '1'}}})
    session_id = headers.get('mcp-session-id')
    post(port, {'jsonrpc': '2.0', 'method': 'notifications/initialized'}, session_id)
    tools, cursor, request_id = [], None, 2
    while True:
        params = {'cursor': cursor} if cursor else {}
        _headers, body = post(port, {'jsonrpc': '2.0', 'id': request_id, 'method': 'tools/list', 'params': params}, session_id)
        result = (parse(body) or {}).get('result', {})
        tools.extend(result.get('tools', []))
        cursor = result.get('nextCursor')
        request_id += 1
        if not cursor:
            return tools


def run(args, allow_python, port):
    log = os.path.join(args.log_dir, 'live_tool_reference_0179_%s.log' % ('python_on' if allow_python else 'python_off'))
    session = EditorSession(args.engine, args.project, port, log, allow_python=allow_python)
    try:
        if not session.start():
            raise SystemExit('the editor or its MCP server did not start (log: %s)' % log)
        return live_tools(port), session
    finally:
        session.stop()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--port', type=int, default=27179)
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)

    with open(REFERENCE, encoding='utf-8') as f:
        reference = json.load(f)
    by_name = {tool['name']: tool for tool in reference['tools']}
    default_names = sorted(name for name, tool in by_name.items() if tool['defaultEnabled'])

    report, ok = {'reference_tools': len(by_name), 'reference_default': len(default_names)}, True
    for allow_python in (True, False):
        tools, session = run(args, allow_python, free_port(args.port))
        names = sorted(tool['name'] for tool in tools)
        expected = sorted(by_name) if allow_python else default_names
        key = 'python_on' if allow_python else 'python_off'
        mismatched = [tool['name'] for tool in tools if tool['name'] in by_name and any(
            tool.get(field) != by_name[tool['name']].get(field) for field in ('title', 'description', 'inputSchema', 'outputSchema', '_meta'))
            or tool.get('annotations', {}) != by_name.get(tool['name'], {}).get('annotations')]
        report[key] = {'listed': len(names), 'expected': len(expected), 'missing': sorted(set(expected) - set(names)),
                       'extra': sorted(set(names) - set(expected)), 'schema_mismatch': mismatched,
                       'restore_ok': session.restore_ok}
        ok = ok and names == expected and not mismatched and session.restore_ok
    report['pass'] = ok
    print(json.dumps(report))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
