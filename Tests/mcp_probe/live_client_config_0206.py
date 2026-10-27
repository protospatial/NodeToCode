"""N2C.Mcp.WriteClientConfig, live (card 0206).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I live_client_config_0206.py --engine G:/UE/UE_5.8 \
        --project "D:/n2c-lanes/lane-b/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" [--claude]

One headless -N2CMcp editor on a non-default port, with Allow MCP Python Execution for the session. Through run-python
it runs `N2C.Mcp.WriteClientConfig all notoken dir=<scratch folder with a space>`, where the folder already holds a
.mcp.json and a .codex/config.toml with another server, then checks:
  - every JSON file parses, keeps the other server and points nodetocode at the session's bound port;
  - config.toml parses (tomllib), keeps [mcp_servers.other] and has [mcp_servers.nodetocode] url;
  - a second run leaves every file byte for byte, and the log printed a Claude Desktop snippet naming launch_bridge;
  - with `token`, the files hold the NODETOCODE_MCP_TOKEN references and never a 64-hex token;
  - with --claude: the headless Claude Code CLI, given the generated .mcp.json (notoken), calls
    get-available-translation-targets.
Report: logs/live_client_config_0206.json.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_bearer_token_0183 import call, session_with  # noqa: E402

FILES = {
    'claude': ('.mcp.json', 'mcpServers', 'url'),
    'cursor': ('.cursor/mcp.json', 'mcpServers', 'url'),
    'vscode': ('.vscode/mcp.json', 'servers', 'url'),
    'gemini': ('.gemini/settings.json', 'mcpServers', 'httpUrl'),
}


def run_command(url, sid, args):
    code = ('import unreal\nunreal.SystemLibrary.execute_console_command(None, %r)\nresult = {"ok": True}'
            % ('N2C.Mcp.WriteClientConfig ' + args))
    status, _, body = call(url, None, sid, 'run-python', {'code': code})
    return status == 200 and body is not None and not (body.get('result') or {}).get('isError')


def snapshot(folder):
    out = {}
    for rel in [f[0] for f in FILES.values()] + ['.codex/config.toml']:
        path = os.path.join(folder, rel)
        out[rel] = open(path, 'rb').read() if os.path.exists(path) else None
    return out


def check_files(folder, port):
    import tomllib
    url = 'http://127.0.0.1:%d/mcp' % port
    r = {}
    for name, (rel, key, url_key) in FILES.items():
        try:
            with open(os.path.join(folder, rel), 'r', encoding='utf-8') as f:
                data = json.load(f)
            ours = data[key]['nodetocode']
            r[name] = {'url': ours.get(url_key) == url,
                       'other_kept': name != 'claude' or data[key].get('other', {}).get('command') == 'other-mcp'}
        except Exception as e:  # noqa: BLE001
            r[name] = {'error': repr(e)}
    try:
        with open(os.path.join(folder, '.codex/config.toml'), 'rb') as f:
            toml = tomllib.load(f)
        r['codex'] = {'url': toml['mcp_servers']['nodetocode'].get('url') == url,
                      'other_kept': toml['mcp_servers'].get('other', {}).get('command') == 'other-mcp',
                      'top_level_kept': toml.get('model') == 'gpt-5.5'}
    except Exception as e:  # noqa: BLE001
        r['codex'] = {'error': repr(e)}
    r['pass'] = all(v is True for entry in r.values() for v in entry.values())
    return r


def run_claude(config_path, log_dir):
    exe = shutil.which('claude')
    if not exe:
        return {'skipped': 'claude not on PATH'}
    prompt = ('Call the nodetocode MCP tool get-available-translation-targets once with no arguments, then reply with '
              'only the number of targets it lists.')
    proc = subprocess.run([exe, '-p', prompt, '--mcp-config', config_path, '--strict-mcp-config', '--allowedTools',
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
    ap.add_argument('--port', type=int, default=27260)
    ap.add_argument('--claude', action='store_true')
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)

    scratch = tempfile.mkdtemp(prefix='n2c_0206_')
    folder = os.path.join(scratch, 'My Project')
    os.makedirs(os.path.join(folder, '.codex'))
    with open(os.path.join(folder, '.mcp.json'), 'w', encoding='utf-8') as f:
        json.dump({'mcpServers': {'other': {'command': 'other-mcp'}}}, f)
    with open(os.path.join(folder, '.codex/config.toml'), 'w', encoding='utf-8') as f:
        f.write('model = "gpt-5.5"\n\n[mcp_servers.other]\ncommand = "other-mcp"\n')

    port = free_port(args.port)
    log = os.path.join(args.log_dir, 'live_client_config_0206.log')
    session = EditorSession(args.engine, args.project, port, log, allow_python=True)
    r = {'port': port}
    try:
        r['started'] = session.start()
        if r['started']:
            url = 'http://127.0.0.1:%d/mcp' % port
            _, sid = session_with(url, None)
            r['first_run'] = run_command(url, sid, 'all notoken dir=' + folder.replace('\\', '/'))
            r['files'] = check_files(folder, port)
            before = snapshot(folder)
            r['second_run'] = run_command(url, sid, 'all notoken dir=' + folder.replace('\\', '/'))
            r['idempotent'] = snapshot(folder) == before
            if args.claude:
                r['claude'] = run_claude(os.path.join(folder, '.mcp.json'), args.log_dir)
            r['token_run'] = run_command(url, sid, 'all token dir=' + folder.replace('\\', '/'))
            texts = {k: (v or b'').decode('utf-8') for k, v in snapshot(folder).items()}
            r['token_refs'] = ('${NODETOCODE_MCP_TOKEN}' in texts['.mcp.json']
                               and '${env:NODETOCODE_MCP_TOKEN}' in texts['.vscode/mcp.json']
                               and '$NODETOCODE_MCP_TOKEN' in texts['.gemini/settings.json']
                               and 'bearer_token_env_var = "NODETOCODE_MCP_TOKEN"' in texts['.codex/config.toml'])
            r['no_raw_token'] = not any(re.search(r'\b[0-9a-f]{64}\b', t) for t in texts.values())
    finally:
        session.stop()
    text = session.log_text()
    r['snippet_logged'] = 'claude_desktop_config.json' in text and 'launch_bridge' in text
    r['restore_ok'] = session.restore_ok
    shutil.rmtree(scratch, ignore_errors=True)
    r['pass'] = bool(r.get('started') and r.get('first_run') and r.get('files', {}).get('pass') and r.get('second_run')
                     and r.get('idempotent') and r.get('token_run') and r.get('token_refs') and r.get('no_raw_token')
                     and r['snippet_logged'] and r['restore_ok']
                     and (not args.claude or r.get('claude', {}).get('pass') is True))
    with open(os.path.join(args.log_dir, 'live_client_config_0206.json'), 'w', encoding='utf-8') as f:
        json.dump(r, f, indent=2)
    print(json.dumps(r, indent=2))
    print('PASS' if r['pass'] else 'FAIL')
    return 0 if r['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
