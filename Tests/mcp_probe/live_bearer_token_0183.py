"""The MCP endpoint's optional bearer token, live (card 0183).

Stdlib only; run with `python -I`, under the `user-secrets` lock (it reads and may create the real per-user token file
<UserSettingsDir>/NodeToCode/mcp-token, and puts it back as it was afterwards):

    python -I live_bearer_token_0183.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-b/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" [--claude]

Two headless -N2CMcp editor sessions on the host:
  off  Require MCP Bearer Token left unset in the user's layer (default off): initialize without Authorization is
       200, and so is one with a wrong token (unchanged behaviour).
  on   bRequireMcpBearerToken=True in the user's Saved/Config layer:
       - /mcp/health answers 200 without a token and its body does not hold the token;
       - POST /mcp without Authorization is 401 with WWW-Authenticate: Bearer and a JSON-RPC error with a null id;
         with a wrong token 401; with the token from the file 200, and tools/list works in that session;
       - the stdio bridge (nodetocode_bridge.py --url <editor>, no token option) initializes and lists the tools: it
         reads the token file itself;
       - read-file and read-path cannot read the token file (it is outside the project, and denied);
       - N2C.Mcp.RegenerateToken (through run-python) writes a new token: the old one is then 401, the new one 200,
         and the bridge, still running, keeps working (it rereads the file);
       - with --claude: the headless Claude Code CLI, configured with the doc snippet (type http, headers
         Authorization: Bearer ${NODETOCODE_MCP_TOKEN}) and the token in NODETOCODE_MCP_TOKEN, calls
         get-available-translation-targets.
No token value is printed or written to the report. Report: logs/live_bearer_token_0183.json.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402

BRIDGE = os.path.normpath(os.path.join(HERE, '..', '..', 'Content', 'Python', 'mcp_bridge', 'nodetocode_bridge.py'))
SECTION = '[/Script/NodeToCode.N2CUserSettings]'
ACCEPT = 'application/json, text/event-stream'
INIT = {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
        'params': {'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'n2c-0183', 'version': '1'}}}
_opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def token_file():
    if os.name == 'nt':
        base = os.environ.get('LOCALAPPDATA') or os.path.join(os.path.expanduser('~'), 'AppData', 'Local')
    elif sys.platform == 'darwin':
        base = os.path.join(os.path.expanduser('~'), 'Library', 'Application Support', 'Epic')
    else:
        base = os.path.join(os.path.expanduser('~'), '.config', 'Epic')
    return os.path.join(base, 'NodeToCode', 'mcp-token')


def read_token():
    try:
        with open(token_file(), 'r', encoding='utf-8') as f:
            return f.read().strip()
    except OSError:
        return None


def post(url, message, token=None, session=None):
    """(status, headers, parsed body or None)"""
    headers = {'Content-Type': 'application/json', 'Accept': ACCEPT}
    if token is not None:
        headers['Authorization'] = 'Bearer ' + token
    if session:
        headers['Mcp-Session-Id'] = session
    req = urllib.request.Request(url, data=json.dumps(message).encode('utf-8'), headers=headers, method='POST')
    try:
        with _opener.open(req, timeout=60) as r:
            status, hdrs, raw = r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        status, hdrs, raw = e.code, e.headers, e.read()
    text = raw.decode('utf-8', 'replace')
    if text.lstrip().startswith('event:') or text.lstrip().startswith('data:'):
        text = '\n'.join(line[5:].strip() for line in text.splitlines() if line.startswith('data:'))
    try:
        body = json.loads(text)
    except ValueError:
        body = None
    return status, hdrs, body


def session_with(url, token):
    """(initialize status, session id) for a session opened with token"""
    status, hdrs, _ = post(url, INIT, token)
    session = hdrs.get('Mcp-Session-Id') if hdrs else None
    if status == 200 and session:
        post(url, {'jsonrpc': '2.0', 'method': 'notifications/initialized'}, token, session)
    return status, session


def call(url, token, session, name, arguments, rid=5):
    return post(url, {'jsonrpc': '2.0', 'id': rid, 'method': 'tools/call', 'params': {'name': name, 'arguments': arguments}},
                token, session)


def tool_count(url, token, session):
    status, _, body = post(url, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}, token, session)
    return len(body['result']['tools']) if status == 200 and body and 'result' in body else -1


class BridgeProcess:
    """nodetocode_bridge.py --url <editor> as a stdio child, one request at a time"""

    def __init__(self, url, env):
        self.proc = subprocess.Popen([sys.executable, '-I', BRIDGE, '--url', url], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)

    def request(self, message):
        self.proc.stdin.write((json.dumps(message) + '\n').encode('utf-8'))
        self.proc.stdin.flush()
        if 'id' not in message:
            return None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                return None
            reply = json.loads(line)
            if isinstance(reply, dict) and reply.get('id') == message['id']:
                return reply

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.wait(30)
        except Exception:
            self.proc.kill()
        return self.proc.stderr.read().decode('utf-8', 'replace')


def bridge_lists_tools(bridge, rid):
    init = bridge.request(dict(INIT, id=rid))
    bridge.request({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
    listed = bridge.request({'jsonrpc': '2.0', 'id': rid + 1, 'method': 'tools/list'})
    ok = bool(init and 'result' in init and listed and 'result' in listed)
    return ok, len(listed['result']['tools']) if ok else -1


def bridge_tools_again(bridge, rid):
    listed = bridge.request({'jsonrpc': '2.0', 'id': rid, 'method': 'tools/list'})
    return bool(listed and 'result' in listed), (listed or {}).get('error')


def run_claude(url, token, log_dir):
    exe = shutil.which('claude')
    if not exe:
        return {'skipped': 'claude not on PATH'}
    config = {'mcpServers': {'nodetocode': {'type': 'http', 'url': url,
                                            'headers': {'Authorization': 'Bearer ${NODETOCODE_MCP_TOKEN}'}}}}
    path = os.path.join(log_dir, 'live_bearer_token_0183_mcp.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(config, f)
    env = dict(os.environ, NODETOCODE_MCP_TOKEN=token)
    prompt = ('Call the nodetocode MCP tool get-available-translation-targets once with no arguments, then reply with '
              'only the number of targets it lists.')
    proc = subprocess.run([exe, '-p', prompt, '--mcp-config', path, '--strict-mcp-config', '--allowedTools',
                           'mcp__nodetocode__*', '--output-format', 'json', '--model', 'claude-haiku-5-5'],
                          capture_output=True, text=True, encoding='utf-8', errors='replace', env=env, timeout=600)
    out = proc.stdout or ''
    result = {'exit': proc.returncode, 'leaked_token': token in out or token in (proc.stderr or '')}
    try:
        parsed = json.loads(out)
        result['result'] = str(parsed.get('result', ''))[:200]
        result['is_error'] = parsed.get('is_error')
    except ValueError:
        result['raw'] = out[-400:]
    result['pass'] = proc.returncode == 0 and not result.get('is_error') and not result['leaked_token'] and \
        any(ch.isdigit() for ch in result.get('result', ''))
    return result


def run_off(args, port):
    log = os.path.join(args.log_dir, 'live_bearer_token_0183_off.log')
    session = EditorSession(args.engine, args.project, port, log)
    user_ini = os.path.join(session._saved_config_dir(), 'EditorPerProjectUserSettings.ini')
    session.guard_file(user_ini)
    from editor_session import remove_ini_key
    remove_ini_key(user_ini, SECTION, 'bRequireMcpBearerToken')
    r = {'mode': 'off'}
    try:
        r['started'] = session.start()
        if r['started']:
            url = 'http://127.0.0.1:%d/mcp' % port
            r['no_token'] = session_with(url, None)[0]
            r['wrong_token'] = session_with(url, '0' * 64)[0]
    finally:
        session.stop()
    r['restore_ok'] = session.restore_ok
    r['pass'] = r.get('started') is True and r.get('no_token') == 200 and r.get('wrong_token') == 200 and session.restore_ok
    return r


def run_on(args, port):
    log = os.path.join(args.log_dir, 'live_bearer_token_0183_on.log')
    session = EditorSession(args.engine, args.project, port, log, allow_python=True)
    session.guard_file(token_file())
    session.patch_file(os.path.join(session._saved_config_dir(), 'EditorPerProjectUserSettings.ini'), SECTION,
                       'bRequireMcpBearerToken', 'True')
    r = {'mode': 'on'}
    bridge = None
    try:
        r['started'] = session.start()
        if not r['started']:
            return r
        url = 'http://127.0.0.1:%d/mcp' % port
        token = read_token()
        r['token_file_written'] = bool(token) and len(token) == 64
        with _opener.open('http://127.0.0.1:%d/mcp/health' % port, timeout=10) as h:
            health = h.read().decode('utf-8', 'replace')
            r['health'] = h.status
        r['health_hides_token'] = bool(token) and token not in health

        status, hdrs, body = post(url, INIT)
        r['no_token'] = status
        r['www_authenticate'] = hdrs.get('WWW-Authenticate') if hdrs else None
        r['no_token_body_id_null'] = bool(body) and 'error' in body and body.get('id') is None
        r['wrong_token'] = session_with(url, '0' * 64)[0]
        status, sid = session_with(url, token)
        r['right_token'] = status
        r['tools'] = tool_count(url, token, sid)
        r['tools_without_token_in_session'] = post(url, {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/list'}, None, sid)[0]

        # The file tools cannot reach it
        status, _, body = call(url, token, sid, 'read-file', {'filePath': token_file()})
        text = json.dumps(body)
        r['read_file_refused'] = status == 200 and bool(body) and body.get('result', {}).get('isError') is True and token not in text
        status, _, body = call(url, token, sid, 'read-path', {'relativePath': token_file()})
        text = json.dumps(body)
        r['read_path_refused'] = status == 200 and bool(body) and body.get('result', {}).get('isError') is True and token not in text

        # The stdio bridge with no token option
        env = {k: v for k, v in os.environ.items() if k != 'NODETOCODE_MCP_TOKEN'}
        bridge = BridgeProcess(url, env)
        r['bridge_ok'], r['bridge_tools'] = bridge_lists_tools(bridge, 10)

        if args.claude:
            r['claude'] = run_claude(url, token, args.log_dir)

        # Regenerate through the console command
        status, _, body = call(url, token, sid, 'run-python', {
            'code': 'import unreal\nunreal.SystemLibrary.execute_console_command(None, "N2C.Mcp.RegenerateToken")\nresult = {"ok": True}'})
        new = read_token()
        r['regenerated'] = bool(new) and new != token and len(new) == 64
        r['old_token_after'] = session_with(url, token)[0]
        r['new_token_after'] = session_with(url, new)[0] if new else None
        r['bridge_after_regenerate'], err = bridge_tools_again(bridge, 20)
        if err:
            r['bridge_error'] = str(err)[:200]
    finally:
        if bridge:
            stderr = bridge.close()
            r['bridge_stderr_leak'] = any(t and t in stderr for t in (read_token(), locals().get('token')))
        session.stop()
    r['restore_ok'] = session.restore_ok
    checks = ['started', 'token_file_written', 'health_hides_token', 'no_token_body_id_null', 'read_file_refused',
              'read_path_refused', 'bridge_ok', 'regenerated', 'bridge_after_regenerate', 'restore_ok']
    r['pass'] = all(r.get(c) is True for c in checks) and r.get('health') == 200 and r.get('no_token') == 401 \
        and r.get('www_authenticate') == 'Bearer' and r.get('wrong_token') == 401 and r.get('right_token') == 200 \
        and r.get('tools', 0) > 0 and r.get('tools_without_token_in_session') == 401 and r.get('old_token_after') == 401 \
        and r.get('new_token_after') == 200 and not r.get('bridge_stderr_leak') \
        and (not args.claude or r.get('claude', {}).get('pass') is True)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--port', type=int, default=27240)
    ap.add_argument('--modes', default='off,on')
    ap.add_argument('--claude', action='store_true', help='also connect the headless Claude Code CLI over HTTP')
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)
    results = []
    for mode in args.modes.split(','):
        results.append((run_off if mode == 'off' else run_on)(args, free_port(args.port)))
        print(json.dumps(results[-1]))
    report = {'engine': args.engine, 'results': results, 'pass': all(r['pass'] for r in results)}
    with open(os.path.join(args.log_dir, 'live_bearer_token_0183.json'), 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('PASS' if report['pass'] else 'FAIL')
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
