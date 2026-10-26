"""NodeToCode's tools that Epic's Unreal MCP also has, left to Unreal MCP on UE 5.8 (card 0174).

Stdlib only; run with `python -I`:

    python -I live_epic_overlap_0174.py --engine G:/UE/UE_5.8 --project <5.8 host .uproject> --label 5.8
    python -I live_epic_overlap_0174.py --engine G:/UE/UE_5.6 --project <5.6 host .uproject> --label 5.6

Each scenario starts a headless editor, reads NodeToCode's tools/list over a 2025-11-25 session, reads the startup log
line that says how many tools were left to Unreal MCP and why, and stops the editor. Plugins are enabled in the host
.uproject and settings written to the host's Saved/Config/WindowsEditor/NodeToCode.ini only for that editor; both
files are put back byte for byte when it stops (`restored`). The 34 overlapping tools are read from
Source/Public/MCP/Tools/N2CMcpEpicOverlap.h.

UE 5.8 scenarios:
- off: the .uproject as it is (no Epic plugins), defaults: every tool, the 34 included; "none left" in the log.
- on: ModelContextProtocol and EditorToolset enabled, defaults: tools/list is `off` minus exactly the 34, calling
  one is -32602 naming the Epic tool, and the log says 34 were left to Unreal MCP.
- mcp_only: ModelContextProtocol enabled without EditorToolset: every tool (nothing serves the counterparts).
- custom: both plugins, an explicit saved Tool Profile = Custom with all twelve toolsets: every tool.
- always: both plugins, Overlapping Tools = Always serve them: every tool.
- bridge: both plugins plus MCPClientToolset with a NodeToCode MCP Toolset Servers entry (as card 0141): the editor
  log says MCPClientToolset registered the reduced count.
UE 5.6 / 5.7 scenarios (the Epic plugins do not exist there):
- off: defaults; leave: Overlapping Tools = Leave them to Unreal MCP. Both list every tool; "no Unreal MCP" in the log.

`--scenarios a,b` runs a subset (`off` always runs first, as the reference). Report:
logs/live_epic_overlap_0174_<label>.json, one editor log per scenario next to it.
"""
import argparse
import http.client
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from mcp_session import ACCEPT, open_session  # noqa: E402

SETTINGS_SECTION = '[/Script/NodeToCode.N2CSettings]'
SERVERS_SECTION = '[/Script/MCPClientToolset.MCPToolsetSettings]'
TOOLSETS = ['BlueprintDiscoveryTools', 'BlueprintGraphTools', 'BlueprintFunctionTools', 'BlueprintVariableTools',
            'BlueprintTagTools', 'BlueprintCompileTools', 'BlueprintClassTools', 'BlueprintComponentTools',
            'ContentBrowserTools', 'FileSystemTools', 'PythonTools', 'TranslationTools']

# name: (plugins to enable, settings {key: value}, custom toolsets, expect the 34 left out)
SCENARIOS_58 = {
    'off': ((), {}, None, False),
    'on': (('ModelContextProtocol', 'EditorToolset'), {}, None, True),
    'mcp_only': (('ModelContextProtocol',), {}, None, False),
    'custom': (('ModelContextProtocol', 'EditorToolset'), {'McpToolProfile': 'Custom'}, TOOLSETS, False),
    'always': (('ModelContextProtocol', 'EditorToolset'), {'McpOverlappingTools': 'AlwaysServe'}, None, False),
    'bridge': (('ModelContextProtocol', 'EditorToolset', 'MCPClientToolset'), {}, None, True),
}
SCENARIOS_OLD = {
    'off': ((), {}, None, False),
    'leave': ((), {'McpOverlappingTools': 'LeaveToUnrealMcp'}, None, False),
}


def overlapping_tools():
    """{tool: counterpart} from N2CMcpEpicOverlap::Tools"""
    text = open(os.path.join(REPO, 'Source', 'Public', 'MCP', 'Tools', 'N2CMcpEpicOverlap.h'), encoding='utf-8').read()
    body = text[text.index('inline constexpr FOverlap Tools[]'):]
    body = body[:body.index('};')]
    return dict(re.findall(r'\{\s*TEXT\("([a-z0-9-]+)"\)\s*,\s*TEXT\("([^"]+)"\)\s*\}', body))


def post(port, session, payload, timeout=60):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'), headers={
            'Content-Type': 'application/json', 'Accept': ACCEPT, 'Mcp-Session-Id': session,
            'MCP-Protocol-Version': '2025-11-25'})
        r = conn.getresponse()
        body = r.read().decode('utf-8', 'replace')
        if 'text/event-stream' in (r.getheader('Content-Type') or ''):
            body = [l[5:].strip() for l in body.splitlines() if l.startswith('data:')][-1]
        return json.loads(body)
    finally:
        conn.close()


def patch_uproject(session, project, plugins):
    session.guard_file(project)
    raw = open(project, 'rb').read()
    data = json.loads(raw.decode('utf-8-sig'))
    entries = data.setdefault('Plugins', [])
    names = {p.get('Name') for p in entries}
    for name in plugins:
        if name not in names:
            entries.append({'Name': name, 'Enabled': True})
    with open(project, 'w', encoding='utf-8', newline='\r\n') as f:
        json.dump(data, f, indent='\t')


def add_array(session, path, section, key, values):
    """Writes key as an array into section of the host's Saved/Config/<Platform>/NodeToCode.ini, as the editor writes
    that layer: one plain key=value line per element; restored at stop(). The engine reads the Saved layer without the
    array commands of Config/Default*.ini (!key=ClearArray, +key=value), so a +key line there is another key and the
    array loads empty (card 0479, NodeToCode.Settings.ConfigLayers)."""
    session.patch_file(path, section, key, values[0])
    text = open(path, 'rb').read().decode('utf-8-sig')
    lines = text.splitlines()
    at = next(i for i, l in enumerate(lines) if l.strip() == '%s=%s' % (key, values[0])) + 1
    lines[at:at] = ['%s=%s' % (key, v) for v in values[1:]]
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')


def wait_for(session, pattern, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        m = re.search(pattern, session.log_text())
        if m:
            return m
        time.sleep(1.0)
    return None


def run_scenario(args, name, spec, log_dir, overlap):
    plugins, settings, custom, _ = spec
    port = free_port(args.port)
    log_path = os.path.join(log_dir, 'live_epic_overlap_0174_%s_%s_editor.log' % (args.label, name))
    out = {'plugins': list(plugins), 'settings': dict(settings), 'custom_toolsets': custom}
    s = EditorSession(args.engine, args.project, port, log_path, allow_python=True)
    try:
        if plugins:
            patch_uproject(s, args.project, plugins)
        ini = os.path.join(s._saved_config_dir(), 'NodeToCode.ini')
        for key, value in settings.items():
            s.patch_file(ini, SETTINGS_SECTION, key, value)
        if custom:
            add_array(s, ini, SETTINGS_SECTION, 'McpCustomToolsets', custom)
        if 'MCPClientToolset' in plugins:
            user_ini = os.path.join(s._saved_config_dir(), 'EditorPerProjectUserSettings.ini')
            s.patch_file(user_ini, SERVERS_SECTION, 'MCPServers',
                         '(Name="NodeToCode",Description="NodeToCode",ServerUrl="http://127.0.0.1:%d/mcp",ApiKey="",'
                         'bEnabled=True,Transport=StreamableHTTP,Auth=None,OAuthClientId="",OAuthScope="")' % port)
        out['started'] = s.start()
        if not out['started']:
            raise RuntimeError('editor did not start')
        session = open_session(port, client_name='n2c-0174')
        listed = post(port, session, {'jsonrpc': '2.0', 'id': 1, 'method': 'tools/list'})
        out['tools'] = sorted(t['name'] for t in listed['result']['tools'])
        out['count'] = len(out['tools'])
        m = re.search(r'MCP tools: (\d+) tools left to Unreal MCP[^\r\n]*|MCP tools: none left to Unreal MCP[^\r\n]*', s.log_text())
        out['log_line'] = m.group(0)[:600] if m else None
        out['log_lines_count'] = len(re.findall(r'MCP tools: (?:\d+ tools|none) left to Unreal MCP', s.log_text()))
        left = [t for t in overlap if t not in out['tools']]
        out['left_out'] = sorted(left)
        if left:
            reply = post(port, session, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
                                         'params': {'name': 'copy-asset', 'arguments': {}}})
            out['call_left_out'] = reply.get('error')
        if 'MCPClientToolset' in plugins:
            m = wait_for(s, r"MCPClientToolset '[^']+': registered (\d+) tool", 60)
            out['bridge_tool_count'] = int(m.group(1)) if m else None
        out['alive'] = s.health()
    except Exception as e:  # recorded; the session still stops and restores
        out['error'] = '%s: %s' % (type(e).__name__, e)
    finally:
        s.stop()
    out['restored'] = s.restore_ok
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True, help='5.6, 5.7 or 5.8')
    ap.add_argument('--port', type=int, default=27330)
    ap.add_argument('--scenarios', default='')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    overlap = overlapping_tools()
    table = SCENARIOS_58 if args.label.startswith('5.8') else SCENARIOS_OLD
    names = [n for n in args.scenarios.split(',') if n] or list(table)
    names = ['off'] + [n for n in names if n != 'off']
    report = {'label': args.label, 'overlapping_tools': len(overlap), 'scenarios': {}}
    checks = {}
    for name in names:
        out = run_scenario(args, name, table[name], log_dir, overlap)
        report['scenarios'][name] = out
        print('%s: %s tools, left out %d, restored %s%s' % (name, out.get('count'), len(out.get('left_out', [])),
              out.get('restored'), (', error ' + out['error']) if 'error' in out else ''), flush=True)

    ref = report['scenarios']['off'].get('tools') or []
    checks['overlap_has_34'] = len(overlap) == 34
    checks['off_lists_every_overlapping_tool'] = bool(ref) and all(t in ref for t in overlap)
    for name in names:
        out = report['scenarios'][name]
        expect_left = table[name][3]
        tools = out.get('tools') or []
        checks['%s_no_error' % name] = 'error' not in out
        checks['%s_restored' % name] = out.get('restored') is True
        checks['%s_log_once' % name] = out.get('log_lines_count') == 1
        if expect_left:
            checks['%s_is_off_minus_34' % name] = bool(tools) and sorted(tools) == sorted(set(ref) - set(overlap))
            checks['%s_log_says_34' % name] = bool(out.get('log_line')) and out['log_line'].startswith('MCP tools: 34 tools left to Unreal MCP')
            err = out.get('call_left_out') or {}
            checks['%s_call_names_epic_tool' % name] = err.get('code') == -32602 and 'AssetTools.duplicate' in err.get('message', '')
        else:
            checks['%s_same_as_off' % name] = bool(tools) and tools == ref
            checks['%s_log_says_none' % name] = bool(out.get('log_line')) and 'none left to Unreal MCP' in out['log_line']
        if 'bridge_tool_count' in out:
            checks['%s_bridge_count' % name] = out['bridge_tool_count'] == len(ref) - len(overlap)
        if name in ('off', 'leave') and not args.label.startswith('5.8'):
            checks['%s_reason_no_unreal_mcp' % name] = 'no Unreal MCP' in (out.get('log_line') or '')
    report['reference_count'] = len(ref)
    report['checks'] = checks
    report['ok'] = all(checks.values())
    for out in report['scenarios'].values():
        out.pop('tools', None)
    path = os.path.join(log_dir, 'live_epic_overlap_0174_%s.json' % args.label)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
