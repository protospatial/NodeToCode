"""MCP 2026-07-28 (stateless) beside the legacy session era on one /mcp endpoint, live (card 0210).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I live_modern_0210.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-b/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" [--claude]

One headless -N2CMcp editor on a non-default port:
  modern   server/discover (resultType, supportedVersions [2026-07-28], serverInfo), tools/list (ttlMs, cacheScope),
           tools/call of get-available-translation-targets (resultType), all without Mcp-Session-Id; -32020 for a wrong
           Mcp-Method, -32022 for an unsupported version, 404 -32601 for ping
  legacy   initialize, notifications/initialized and tools/list in a session, as before (no resultType)
  listen   (UE 5.8) subscriptions/listen answers text/event-stream with the acknowledgement first; the client then
           closes the socket and the editor logs that it noticed within a few keep-alive beats; on 5.6/5.7 404 -32601
  bridge   the stdio bridge (--url) carries a modern server/discover and tools/list
  claude   (--claude) the headless Claude Code CLI, which probes with server/discover, connects and calls a tool; the
           editor log shows its server/discover and no initialize from it
Report: logs/live_modern_0210.json.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402

BRIDGE = os.path.normpath(os.path.join(HERE, '..', '..', 'Content', 'Python', 'mcp_bridge', 'nodetocode_bridge.py'))
VERSION = '2026-07-28'
META = {'io.modelcontextprotocol/protocolVersion': VERSION,
        'io.modelcontextprotocol/clientInfo': {'name': 'n2c-0210', 'version': '1'},
        'io.modelcontextprotocol/clientCapabilities': {}}
_opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def post(url, message, headers):
    """(status, headers, parsed body or None)"""
    h = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}
    h.update(headers)
    req = urllib.request.Request(url, data=json.dumps(message).encode('utf-8'), headers=h, method='POST')
    try:
        with _opener.open(req, timeout=60) as r:
            status, hdrs, raw = r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        status, hdrs, raw = e.code, e.headers, e.read()
    text = raw.decode('utf-8', 'replace')
    if text.lstrip().startswith(('event:', 'data:')):
        text = '\n'.join(line[5:].strip() for line in text.splitlines() if line.startswith('data:'))
    try:
        return status, hdrs, json.loads(text) if text.strip() else None
    except ValueError:
        return status, hdrs, None


def modern(url, rid, method, mcp_name=None, version=VERSION, method_header=None, **params):
    message = {'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': dict(params, _meta=dict(META, **{
        'io.modelcontextprotocol/protocolVersion': version}))}
    headers = {'MCP-Protocol-Version': version, 'Mcp-Method': method_header or method}
    if mcp_name:
        headers['Mcp-Name'] = mcp_name
    return post(url, message, headers)


def run_modern(url):
    r = {}
    status, hdrs, body = modern(url, 'd1', 'server/discover')
    result = (body or {}).get('result') or {}
    r['discover'] = (status == 200 and result.get('resultType') == 'complete' and result.get('supportedVersions') == [VERSION]
                     and 'io.modelcontextprotocol/serverInfo' in (result.get('_meta') or {}) and not hdrs.get('Mcp-Session-Id'))
    status, _, body = modern(url, 2, 'tools/list')
    result = (body or {}).get('result') or {}
    r['tools_list'] = status == 200 and result.get('resultType') == 'complete' and result.get('cacheScope') == 'private' \
        and isinstance(result.get('ttlMs'), int) and len(result.get('tools') or []) > 0
    r['tool_count'] = len(result.get('tools') or [])
    status, _, body = modern(url, 3, 'tools/call', mcp_name='get-available-translation-targets',
                             **{'name': 'get-available-translation-targets', 'arguments': {}})
    result = (body or {}).get('result') or {}
    r['tools_call'] = status == 200 and result.get('resultType') == 'complete' and not result.get('isError')
    status, _, body = modern(url, 4, 'tools/list', method_header='tools/call')
    r['header_mismatch'] = status == 400 and ((body or {}).get('error') or {}).get('code') == -32020
    status, _, body = modern(url, 5, 'tools/list', version='2099-01-01')
    error = (body or {}).get('error') or {}
    r['unsupported_version'] = status == 400 and error.get('code') == -32022 and (error.get('data') or {}).get('supported') == [VERSION]
    status, _, body = modern(url, 6, 'ping')
    r['ping_404'] = status == 404 and ((body or {}).get('error') or {}).get('code') == -32601
    r['pass'] = all(r[k] for k in ('discover', 'tools_list', 'tools_call', 'header_mismatch', 'unsupported_version', 'ping_404'))
    return r


def run_legacy(url):
    init = {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
            'params': {'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'n2c-0210-legacy', 'version': '1'}}}
    status, hdrs, body = post(url, init, {})
    sid = hdrs.get('Mcp-Session-Id') if hdrs else None
    r = {'initialize': status == 200 and bool(sid) and ((body or {}).get('result') or {}).get('protocolVersion') == '2025-11-25'}
    if sid:
        session = {'Mcp-Session-Id': sid, 'MCP-Protocol-Version': '2025-11-25'}
        post(url, {'jsonrpc': '2.0', 'method': 'notifications/initialized'}, session)
        status, _, body = post(url, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}, session)
        result = (body or {}).get('result') or {}
        r['tools_list'] = status == 200 and len(result.get('tools') or []) > 0 and 'resultType' not in result
        r['tool_count'] = len(result.get('tools') or [])
    r['pass'] = r['initialize'] and r.get('tools_list') is True
    return r


def run_listen(port, engine_minor):
    """Raw socket: POST subscriptions/listen, read until the acknowledgement, then close the socket"""
    message = {'jsonrpc': '2.0', 'id': 'sub-1', 'method': 'subscriptions/listen',
               'params': {'_meta': META, 'notifications': {'toolsListChanged': True}}}
    body = json.dumps(message).encode('utf-8')
    head = ('POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/json\r\n'
            'Accept: application/json, text/event-stream\r\nMCP-Protocol-Version: %s\r\nMcp-Method: subscriptions/listen\r\n'
            'Content-Length: %d\r\n\r\n' % (port, VERSION, len(body))).encode('ascii')
    sock = socket.create_connection(('127.0.0.1', port), timeout=20)
    sock.sendall(head + body)
    data = b''
    deadline = time.time() + 20
    while time.time() < deadline and b'subscriptions/acknowledged' not in data and b'"error"' not in data:
        chunk = sock.recv(4096)
        if not chunk:
            break
        data += chunk
    text = data.decode('utf-8', 'replace')
    r = {'status_line': text.split('\r\n', 1)[0]}
    if engine_minor >= 8:
        r['acknowledged'] = 'text/event-stream' in text and 'subscriptions/acknowledged' in text and '"toolsListChanged":true' in text
    else:
        r['not_available'] = ' 404 ' in r['status_line'] and '-32601' in text
    sock.close()
    r['closed_at'] = time.time()
    return r


def run_bridge(url):
    proc = subprocess.Popen([sys.executable, '-I', BRIDGE, '--url', url], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    out = []
    try:
        for message in ({'jsonrpc': '2.0', 'id': 1, 'method': 'server/discover', 'params': {'_meta': META}},
                        {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list', 'params': {'_meta': META}}):
            proc.stdin.write((json.dumps(message) + '\n').encode('utf-8'))
            proc.stdin.flush()
            out.append(json.loads(proc.stdout.readline()))
    finally:
        proc.stdin.close()
        proc.wait(30)
    r = {'discover': ((out[0].get('result') or {}).get('supportedVersions') == [VERSION]) if out else False,
         'tools_list': ((out[1].get('result') or {}).get('resultType') == 'complete') if len(out) > 1 else False}
    r['pass'] = r['discover'] and r['tools_list']
    return r


def run_claude(url, log_dir):
    import shutil
    exe = shutil.which('claude')
    if not exe:
        return {'skipped': 'claude not on PATH'}
    path = os.path.join(log_dir, 'live_modern_0210_mcp.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': url}}}, f)
    prompt = ('Call the nodetocode MCP tool get-available-translation-targets once with no arguments, then reply with '
              'only the number of targets it lists.')
    proc = subprocess.run([exe, '-p', prompt, '--mcp-config', path, '--strict-mcp-config', '--allowedTools',
                           'mcp__nodetocode__*', '--output-format', 'json', '--model', 'claude-haiku-5-5'],
                          capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=600)
    result = {'exit': proc.returncode}
    try:
        parsed = json.loads(proc.stdout or '')
        result['result'] = str(parsed.get('result', ''))[:200]
        result['is_error'] = parsed.get('is_error')
    except ValueError:
        result['raw'] = (proc.stdout or '')[-400:]
    result['pass'] = proc.returncode == 0 and not result.get('is_error') and any(ch.isdigit() for ch in result.get('result', ''))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--port', type=int, default=27270)
    ap.add_argument('--claude', action='store_true')
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)
    minor = int(os.path.basename(os.path.normpath(args.engine)).split('.')[-1]) if '.' in os.path.basename(os.path.normpath(args.engine)) else 8

    port = free_port(args.port)
    url = 'http://127.0.0.1:%d/mcp' % port
    session = EditorSession(args.engine, args.project, port, os.path.join(args.log_dir, 'live_modern_0210.log'))
    r = {'engine': args.engine, 'port': port}
    try:
        r['started'] = session.start()
        if r['started']:
            r['modern'] = run_modern(url)
            r['legacy'] = run_legacy(url)
            r['listen'] = run_listen(port, minor)
            time.sleep(8)  # a few keep-alive beats for the editor to notice the closed stream
            r['bridge'] = run_bridge(url)
            if args.claude:
                r['claude'] = run_claude(url, args.log_dir)
    finally:
        session.stop()
    log = session.log_text()
    if minor >= 8 and 'listen' in r:
        r['listen']['close_noticed'] = 'The client closed a subscriptions/listen stream' in log
        r['listen']['pass'] = r['listen'].get('acknowledged') is True and r['listen']['close_noticed']
    elif 'listen' in r:
        r['listen']['pass'] = r['listen'].get('not_available') is True
    if args.claude and 'claude' in r:
        r['claude']['discover_seen'] = 'MCP server/discover from claude-code' in log
        r['claude']['pass'] = r['claude'].get('pass') is True and r['claude']['discover_seen']
    r['restore_ok'] = session.restore_ok
    parts = [r.get(k, {}).get('pass') for k in ('modern', 'legacy', 'listen', 'bridge')] + \
        ([r.get('claude', {}).get('pass')] if args.claude else [])
    r['pass'] = bool(r.get('started')) and all(p is True for p in parts) and r['restore_ok']
    with open(os.path.join(args.log_dir, 'live_modern_0210.json'), 'w', encoding='utf-8') as f:
        json.dump(r, f, indent=2)
    print(json.dumps(r, indent=2))
    print('PASS' if r['pass'] else 'FAIL')
    return 0 if r['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
