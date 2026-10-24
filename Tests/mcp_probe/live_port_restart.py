"""Change the MCP Server Port in a running editor and check the server moves without an editor restart (card 0112).

Stdlib only; run with `python -I`:

    python -I live_port_restart.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject"

It starts a headless -N2CMcp editor on a free port P with run-python allowed, checks the discovery file
(<Project>/Saved/NodeToCode/mcp-server.json and the per-user index entry) names P and the editor's pid, then sets
McpServerPort to another free port Q through run-python with PostEditChangeProperty (what the settings page does).
It passes when /mcp/health answers on Q, the old routes on P are gone (404), the discovery file and the Server Status
setting name Q, the editor log shows the restart and no error line, and the discovery files are gone after the editor
exits. EditorSession puts the host's config files back afterwards. Report: logs/live_port_restart.json.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from e05_regressions import run_python  # noqa: E402

SET_PORT_PY = '''
import unreal
s = unreal.get_default_object(unreal.N2CSettings)
s.set_editor_property('mcp_server_port', %d, unreal.PropertyAccessChangeNotifyMode.ALWAYS)
result = {'port': s.get_editor_property('mcp_server_port')}
'''

STATUS_PY = '''
import unreal
s = unreal.get_default_object(unreal.N2CSettings)
result = {'status': s.get_editor_property('mcp_server_status')}
'''


QUIT_PY = '''
import unreal
unreal.SystemLibrary.quit_editor()
result = {'quit': True}
'''


def health(port, timeout=3):
    try:
        with urllib.request.urlopen('http://127.0.0.1:%d/mcp/health' % port, timeout=timeout) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code
    except OSError:
        return 0


def read_json(path):
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--port', type=int, default=27190)
    ap.add_argument('--log-dir', default=os.path.join(HERE, 'logs'))
    args = ap.parse_args()
    os.makedirs(args.log_dir, exist_ok=True)

    port = free_port(args.port)
    new_port = free_port(port + 10)
    discovery = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved', 'NodeToCode', 'mcp-server.json')
    index_dir = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'NodeToCode', 'servers')
    log_path = os.path.join(args.log_dir, 'live_port_restart_editor.log')
    checks = []

    def check(ok, name, detail=''):
        checks.append({'name': name, 'ok': bool(ok), 'detail': str(detail)})
        print('%s %s  %s' % ('PASS' if ok else 'FAIL', name, detail), flush=True)

    s = EditorSession(args.engine, args.project, port, log_path, allow_python=True)
    s.start()
    pid = s.proc.pid
    index_file = os.path.join(index_dir, '%d.json' % pid)
    try:
        url = 'http://127.0.0.1:%d/mcp' % port
        d = read_json(discovery) or {}
        check(d.get('port') == port and d.get('pid') == pid, 'discovery file names port %d and pid %d' % (port, pid),
              json.dumps({k: d.get(k) for k in ('port', 'pid', 'projectFile', 'engineVersion', 'protocolVersions')}))
        check((read_json(index_file) or {}).get('port') == port, 'per-user index entry %s' % index_file)

        data, raw = run_python(url, SET_PORT_PY % new_port)
        check(data.get('port') == new_port, 'set McpServerPort to %d through run-python' % new_port, raw[:200])

        deadline = time.time() + 30
        while time.time() < deadline and health(new_port) != 200:
            time.sleep(0.5)
        check(health(new_port) == 200, '/mcp/health answers on the new port %d' % new_port)
        check(health(port) == 404, 'old routes on port %d are unbound (404)' % port, health(port))
        d = read_json(discovery) or {}
        check(d.get('port') == new_port, 'discovery file names the new port', d.get('port'))
        check((read_json(index_file) or {}).get('port') == new_port, 'per-user index entry names the new port')
        data, raw = run_python('http://127.0.0.1:%d/mcp' % new_port, STATUS_PY)
        check(str(new_port) in str(data.get('status')), 'Server Status names the new port', data.get('status'))

        # Quit from inside so ShutdownModule runs (UnrealEditor-Cmd ignores WM_CLOSE)
        try:
            run_python('http://127.0.0.1:%d/mcp' % new_port, QUIT_PY)
        except Exception:  # the editor may close the connection while it exits
            pass
        deadline = time.time() + 60
        while time.time() < deadline and s.proc.poll() is None:
            time.sleep(0.5)
        check(s.proc.poll() == 0, 'editor exits cleanly', s.proc.poll())
    finally:
        s.stop()
    text = s.log_text()
    check('restarting the MCP server' in text, 'log shows the restart')
    errors = [l.strip()[:200] for l in text.splitlines() if re.search(r'\bLog\w+: Error: ', l)]
    check(not errors, 'no error line in the editor log', errors[:5])
    check(not os.path.exists(index_file), 'per-user index entry removed on exit')
    d = read_json(discovery)
    check(d is None or d.get('pid') != pid, 'project discovery file removed on exit')
    check(getattr(s, 'restore_ok', True), 'host config files restored')

    report = os.path.join(args.log_dir, 'live_port_restart.json')
    with open(report, 'w', encoding='utf-8') as f:
        json.dump({'port': port, 'new_port': new_port, 'checks': checks}, f, indent=1)
    failed = [c for c in checks if not c['ok']]
    print('LIVE_PORT_RESTART %s (%d checks, %d failed); report %s' % ('PASS' if not failed else 'FAIL', len(checks),
                                                                       len(failed), report))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
