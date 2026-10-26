"""The client snippets and tool modes of wiki/developers/Connecting-an-MCP-Client.md and Choosing-a-Tool-Mode.md,
live with Claude Code against a headless editor (card 0178).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET when other agents run probes):

    python -I live_client_pages_0178.py --engine G:/UE/UE_5.8 \
        --project "<Hosts>/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8 [--scenarios http,...]

Every Claude Code run uses a new, empty CLAUDE_CONFIG_DIR (deleted afterwards; the user's own config is never read or
changed) and signs in with ANTHROPIC_API_KEY from the repo's .env, given to the CLI only. Each scenario starts its own
editor with the settings the page names, then:

- http: the page's JSON config ({"type": "http", "url": ...}) through `claude -p --mcp-config --strict-mcp-config`.
  Passes when the init lists nodetocode as connected with 63 tools (the default: Default toolsets, List every tool,
  Allow MCP Python Execution off) and a get-available-translation-targets call succeeds.
- mcp_add: `claude mcp add --transport http nodetocode <url>` then `claude mcp list`, which must say Connected.
- bridge: the stdio bridge as Claude Desktop starts it (command python, args nodetocode_bridge.py --project <host>),
  through --mcp-config. Passes with nodetocode connected and 63 tools.
- launcher: the same with launch_bridge.bat as the command (Windows only).
- python_allowed: Allow MCP Python Execution on: 66 tools, run-python among them.
- tool_search: Tool Exposure Tool search: exactly call_tool, describe_toolset and list_toolsets, and list_toolsets
  succeeds.
- python_only: Tool Profile Python scripting only, Python execution off: the 4 script library tools.
- custom: Tool Profile Custom toolsets with BlueprintDiscoveryTools and TranslationTools (written to the project's
  Config/DefaultNodeToCode.ini, as live_epic_overlap_0174.py does): 13 tools.

Report: logs/live_client_pages_0178_<label>.json; each CLI run's stream next to it.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_claude_plugin_0172 import claude_exe, cli_env, run, run_cli, server_status  # noqa: E402

BRIDGE_DIR = os.path.join(REPO, 'Content', 'Python', 'mcp_bridge')
SETTINGS_SECTION = '[/Script/NodeToCode.N2CSettings]'
PREFIX = 'mcp__nodetocode__'
META_TOOLS = ['call_tool', 'describe_toolset', 'list_toolsets']
PYTHON_LIBRARY = ['get-python-script', 'get-script-functions', 'list-python-scripts', 'search-python-scripts']

# name: (settings in Saved/Config NodeToCode.ini, custom toolsets, allow python, route, expected count, tool to call)
SCENARIOS = {
    'http': ({}, None, False, 'http', 63, 'get-available-translation-targets'),
    'mcp_add': ({}, None, False, 'mcp_add', None, None),
    'bridge': ({}, None, False, 'bridge', 63, 'get-available-translation-targets'),
    'launcher': ({}, None, False, 'launcher', 63, None),
    'python_allowed': ({}, None, True, 'http', 66, None),
    'tool_search': ({'McpToolExposure': 'ToolSearch'}, None, False, 'http', 3, 'list_toolsets'),
    'python_only': ({'McpToolProfile': 'PythonScripting'}, None, False, 'http', 4, None),
    'custom': ({'McpToolProfile': 'Custom'}, ['BlueprintDiscoveryTools', 'TranslationTools'], False, 'http', 13, None),
}


def add_array(session, path, section, key, values):
    """key as an array (!key=ClearArray, then +key=value lines) in section of path; restored at stop()"""
    session.patch_file(path, section, '!' + key, 'ClearArray')
    lines = open(path, 'rb').read().decode('utf-8-sig').splitlines()
    at = next(i for i, l in enumerate(lines) if l.strip() == '!%s=ClearArray' % key) + 1
    lines[at:at] = ['+%s=%s' % (key, v) for v in values]
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')


def server_entry(route, url, project):
    if route == 'http':
        return {'type': 'http', 'url': url}
    if route == 'bridge':
        return {'command': sys.executable, 'args': [os.path.join(BRIDGE_DIR, 'nodetocode_bridge.py'), '--project',
                                                    project]}
    return {'command': os.path.join(BRIDGE_DIR, 'launch_bridge.bat'), 'args': ['--project', project]}


def run_scenario(args, name, keys, work, log_dir):
    settings, custom, allow_python, route, expected, call = SCENARIOS[name]
    port = free_port(args.port)
    url = 'http://127.0.0.1:%d/mcp' % port
    base = os.path.join(log_dir, 'live_client_pages_0178_%s_%s' % (args.label, name))
    out = {'settings': settings, 'custom_toolsets': custom, 'allow_python': allow_python, 'route': route, 'url': url}
    s = EditorSession(args.engine, args.project, port, base + '_editor.log', allow_python=allow_python)
    config_dir = os.path.join(work, name, 'claude-config')
    cwd = os.path.join(work, name, 'project')
    os.makedirs(config_dir)
    os.makedirs(cwd)
    env = cli_env(config_dir, keys)
    try:
        ini = os.path.join(s._saved_config_dir(), 'NodeToCode.ini')
        for key, value in settings.items():
            s.patch_file(ini, SETTINGS_SECTION, key, value)
        if custom:
            add_array(s, s.project_config_path(), SETTINGS_SECTION, 'McpCustomToolsets', custom)
        if not s.start():
            out['error'] = 'editor did not start'
            return out, {'editor_started': False}
        checks = {'editor_started': True}
        if route == 'mcp_add':
            code, text = run([claude_exe(), 'mcp', 'add', '--transport', 'http', 'nodetocode', url], env, cwd=cwd)
            out['add'] = {'exit': code, 'text': text.strip()[:400]}
            code, text = run([claude_exe(), 'mcp', 'list'], env, cwd=cwd)
            out['list'] = {'exit': code, 'text': text.strip()[:800]}
            line = next((l for l in text.splitlines() if l.startswith('nodetocode:')), '')
            checks['listed_connected'] = url in line and 'Connected' in line
            return out, checks
        config = os.path.join(work, name, 'mcp.json')
        with open(config, 'w', encoding='utf-8') as f:
            json.dump({'mcpServers': {'nodetocode': server_entry(route, url, args.project)}}, f, indent=2)
        out['config'] = json.load(open(config, encoding='utf-8'))
        prompt = ('Call the nodetocode MCP tool %s with no arguments and reply with its result in one line.' % call
                  if call else 'Reply with the single word OK.')
        r = run_cli(prompt, cwd, env, 'mcp__nodetocode__*', base + '_cli.jsonl', args.cli_model, args.timeout,
                    ['--mcp-config', config, '--strict-mcp-config'])
        tools = sorted(t[len(PREFIX):] for t in ((r['init'] or {}).get('tools') or []) if t.startswith(PREFIX))
        out.update({'exit': r['exit'], 'seconds': r['seconds'], 'status': server_status(r['init'], 'nodetocode'),
                    'tool_count': len(tools), 'answer': r['answer'][:400],
                    'calls': [{'tool': c['tool'], 'error': c['error'], 'text': c['text'][:300]} for c in r['calls']]})
        if name in ('tool_search', 'python_only', 'custom'):
            out['tools'] = tools
        checks['connected'] = out['status'] == 'connected'
        checks['tool_count'] = len(tools) == expected
        if name == 'tool_search':
            checks['meta_tools'] = tools == META_TOOLS
        if name == 'python_allowed':
            checks['run_python_listed'] = 'run-python' in tools
        if name == 'python_only':
            checks['script_library'] = tools == sorted(PYTHON_LIBRARY)
        if call:
            checks['call_ok'] = any(c['tool'] == PREFIX + call and not c['error'] for c in r['calls'])
        return out, checks
    finally:
        s.stop()
        out['restore_ok'] = s.restore_ok
        out['leaks'] = find_leaks(keys, [base + '_editor.log', base + '_cli.jsonl'])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--scenarios', default=','.join(SCENARIOS))
    ap.add_argument('--cli-model', default='claude-haiku-5-5')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--port', type=int, default=27178)
    args = ap.parse_args()
    names = [n for n in args.scenarios.split(',') if n]
    unknown = set(names) - set(SCENARIOS)
    if unknown:
        raise SystemExit('unknown scenarios: %s' % ', '.join(sorted(unknown)))
    if os.name != 'nt' and 'launcher' in names:
        names.remove('launcher')
    keys = load_provider_keys(only=['ANTHROPIC_API_KEY'])
    if not keys:
        raise SystemExit('ANTHROPIC_API_KEY not in the repo .env (the CLI signs in with it)')
    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    report = {'label': args.label, 'cli_model': args.cli_model, 'scenarios': {}, 'checks': {}}
    code, version = run([claude_exe(), '--version'], cli_env(tempfile.gettempdir(), {}))
    report['claude_version'] = version.strip()
    work = tempfile.mkdtemp(prefix='n2c_clients_0178_')
    try:
        for name in names:
            out, checks = run_scenario(args, name, keys, work, log_dir)
            checks['restore_ok'] = out.get('restore_ok') is True
            checks['no_key_leak'] = not out.get('leaks')
            report['scenarios'][name] = out
            report['checks'][name] = checks
            print('%s: %s %s' % (name, 'PASS' if all(checks.values()) else 'FAIL',
                                 ' '.join('%s=%s' % kv for kv in checks.items())), flush=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    ok = all(all(c.values()) for c in report['checks'].values())
    report['result'] = 'PASS' if ok else 'FAIL'
    path = os.path.join(log_dir, 'live_client_pages_0178_%s.json' % args.label)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print('%s, report %s' % (report['result'], path))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
