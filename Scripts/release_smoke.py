"""Load an installed NodeToCode package in a headless editor and check that the MCP server answers.

Stdlib only; run with `python -I`. Scripts/package-release.ps1 calls it on a Blueprint-only scratch host whose
Plugins/NodeToCode folder is the unzipped release package (with its prebuilt Binaries), so a pass means the shipped
binaries load without a rebuild:

    python -I release_smoke.py --engine G:/UE/UE_5.8 --project <host>.uproject --plugin-dir <host>/Plugins/NodeToCode

Starts the editor with -N2CMcp (Tests/mcp_probe/editor_session.py), then sends initialize and tools/list to
http://127.0.0.1:<port>/mcp. Prints one JSON line and exits 0 only when the server answered with a tool list and the
log shows NodeToCode mounted from --plugin-dir.
"""
import argparse
import http.client
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'Tests', 'mcp_probe'))
from editor_session import EditorSession, free_port  # noqa: E402

HEADERS = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}


def post(port, payload, session_id=None):
    headers = dict(HEADERS)
    if session_id:
        headers['Mcp-Session-Id'] = session_id
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'), headers=headers)
        r = conn.getresponse()
        body = r.read().decode('utf-8', 'replace')
        return r.status, {k.lower(): v for k, v in r.getheaders()}, body
    finally:
        conn.close()


def parse_jsonrpc(body):
    """A JSON body, or the last `data:` line of an SSE body."""
    text = body.strip()
    if not text.startswith('{'):
        lines = [l[5:].strip() for l in text.splitlines() if l.startswith('data:')]
        text = lines[-1] if lines else ''
    try:
        return json.loads(text)
    except ValueError:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--plugin-dir', required=True)
    ap.add_argument('--port', type=int, default=27250)
    ap.add_argument('--log', required=True)
    ap.add_argument('--timeout', type=int, default=400)
    args = ap.parse_args()

    result = {'started': False, 'server': None, 'tools': 0, 'mounted_from_package': False}
    port = free_port(args.port)
    with EditorSession(args.engine, args.project, port, args.log, allow_python=False) as s:
        result['started'] = s.start(timeout=args.timeout)
        if result['started']:
            status, headers, body = post(port, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                'protocolVersion': '2025-03-26', 'capabilities': {},
                'clientInfo': {'name': 'n2c-release-smoke', 'version': '1'}}})
            init = parse_jsonrpc(body) or {}
            info = (init.get('result') or {}).get('serverInfo') or {}
            result['server'] = '%s %s' % (info.get('name', '?'), info.get('version', '?')) if status == 200 else status
            sid = headers.get('mcp-session-id')
            post(port, {'jsonrpc': '2.0', 'method': 'notifications/initialized'}, sid)
            status, _, body = post(port, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}, sid)
            listed = parse_jsonrpc(body) or {}
            result['tools'] = len((listed.get('result') or {}).get('tools') or []) if status == 200 else 0
    log = s.log_text()
    want = os.path.normcase(os.path.normpath(os.path.abspath(args.plugin_dir)))
    for line in log.splitlines():
        if 'NodeToCode' in line and ('Mounting' in line or 'Mounted' in line or '.uplugin' in line):
            seen = line.replace('/', os.sep)
            if want in os.path.normcase(seen) or 'Project plugin NodeToCode' in line:
                result['mounted_from_package'] = True
                break
    result['log_lines_with_LogNodeToCode'] = sum(1 for l in log.splitlines() if 'LogNodeToCode' in l)
    ok = result['started'] and result['tools'] > 0 and result['mounted_from_package']
    result['pass'] = ok
    print(json.dumps(result))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
