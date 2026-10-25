"""NodeToCode mounted in Epic's ToolsetRegistry through UE 5.8's MCPClientToolset (card 0141).

Stdlib only; run with `python -I`, on UE 5.8 only (the plugins do not exist before 5.8):

    python -I live_mcpclienttoolset.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject"

For the length of the run it enables Epic's ModelContextProtocol ("Unreal MCP") and MCPClientToolset plugins in the
host .uproject, adds one MCP Toolset Servers entry for NodeToCode (Transport Streamable HTTP, Auth None) and turns on
Epic's MCP server on its own port, all in the host's Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini. The
.uproject and every config file are put back byte for byte when the editor stops (`restored` in the report). Then:

- replay: the request sequence MCPClientToolset sends (FMCPClientToolset::StartStreamableHTTPHandshake and
  SendStreamableHTTPRequest), straight to NodeToCode's /mcp: initialize at protocolVersion 2024-11-05 with
  `Accept: application/json, text/event-stream` and no MCP-Protocol-Version header, ids from 0, the Mcp-Session-Id
  kept, notifications/initialized, tools/list with no params and no cursor, then tools/call
  get-available-translation-targets and translate-focused-blueprint with no progressToken. Its tools/list must hold
  every tool a normal 2025-11-25 session lists.
- registry: the same tools reached through Epic's registry, over Epic's MCP server (no Epic account, no AI
  assistant): the editor log says MCPClientToolset registered NodeToCode, list_toolsets lists it, describe_toolset
  gives every tool as NodeToCode.<tool>, and call_tool runs get-available-translation-targets and
  translate-focused-blueprint.
- error shape: translate-focused-blueprint before any Blueprint is focused is a CallToolResult with isError true
  from NodeToCode; through the registry the same call is recorded as what Epic returns (MCPClientToolset drops
  isError).

The translations need the provider key: only that variable from the repo's .env is added to the editor's
environment, never printed, and the logs and report are checked for it. `--no-translate` skips both translations
(no key needed). `--activity-timeout N` starts the editor with [HTTP] HttpActivityTimeout=N (the engine default is
30 s) to show what MCPClientToolset does when a tool call takes longer than that; NodeToCode's own provider requests
set their own timeout, so only the registry hop is affected. `--eager` turns Epic's tool search off, so Epic's
tools/list carries every registry tool as its own MCP tool (the report records the NodeToCode names it lists).
Report: logs/live_mcpclienttoolset_<label>.json, editor log next to it.
"""
import argparse
import http.client
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_claude_cli_translate import code_chars_of  # noqa: E402
from live_env_translation import ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, stored_key_present  # noqa: E402
from mcp_session import open_session  # noqa: E402

ACCEPT = 'application/json, text/event-stream'
USER_INI_SECTION_SERVERS = '[/Script/MCPClientToolset.MCPToolsetSettings]'
USER_INI_SECTION_EPIC = '[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]'
TOOLSET = 'NodeToCode'
DESCRIPTION = 'Blueprint translation, graph editing and Python tools for the open Unreal editor'
EXTRA_PLUGINS = ('ModelContextProtocol', 'MCPClientToolset')

# What the editor reads back from the MCP Toolset Servers settings (run-python result)
SETTINGS_PY = r'''
import unreal
cls = unreal.load_class(None, "/Script/MCPClientToolset.MCPToolsetSettings")
s = unreal.get_default_object(cls)
sub_cls = unreal.load_class(None, "/Script/MCPClientToolset.MCPClientToolsetSubsystem")
def gp(o, *names):
    for n in names:
        try:
            return o.get_editor_property(n)
        except Exception:
            pass
    return "?"
servers = gp(s, "MCPServers", "mcp_servers", "mcpservers")
result = {"subsystem": bool(sub_cls and unreal.get_editor_subsystem(sub_cls)),
          "attrs": [a for a in dir(s) if "server" in a.lower()],
          "servers": [{"name": gp(e, "Name", "name"), "server_url": gp(e, "ServerUrl", "server_url"),
                       "enabled": gp(e, "bEnabled", "enabled"), "transport": str(gp(e, "Transport", "transport")),
                       "auth": str(gp(e, "Auth", "auth"))} for e in servers] if servers != "?" else "?"}
'''


def server_entry(url):
    """The MCP Toolset Servers entry, as one config line value (FMCPServerConfig text form)"""
    return ('(Name="%s",Description="%s",ServerUrl="%s",ApiKey="",bEnabled=True,Transport=StreamableHTTP,Auth=None,'
            'OAuthClientId="",OAuthScope="")' % (TOOLSET, DESCRIPTION, url))


def post(port, payload, headers, timeout):
    """One POST to http://127.0.0.1:port/mcp. Returns status, headers (lower-case names), body text, seconds to the
    response headers and seconds to the end of the body."""
    t0 = time.time()
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'), headers=headers)
        r = conn.getresponse()
        t_headers = time.time() - t0
        body = r.read().decode('utf-8', 'replace')
        return r.status, {k.lower(): v for k, v in r.getheaders()}, body, round(t_headers, 2), round(time.time() - t0, 2)
    finally:
        conn.close()


def parse_like_mcpclienttoolset(headers, body, rid):
    """The JSON-RPC message MCPClientToolset takes from a response: the whole body when Content-Type contains
    application/json, else the SSE 'message' event whose id is rid (it keeps only the last data: line of an event)"""
    if 'application/json' in headers.get('content-type', ''):
        try:
            return json.loads(body) if body else None
        except ValueError:
            return None
    for block in re.split(r'\r\n\r\n|\n\n', body):
        event, data = 'message', ''
        for line in block.splitlines():
            if line.startswith('event:'):
                event = line[6:].strip()
            elif line.startswith('data:'):
                data = line[5:].strip()
        if event == 'message' and data:
            try:
                msg = json.loads(data)
            except ValueError:
                continue
            if msg.get('id') == rid:
                return msg
    return None


class ReplayClient:
    """MCPClientToolset's Streamable HTTP client, request for request"""

    def __init__(self, port):
        self.port = port
        self.next_id = 0
        self.session = ''
        self.trace = []

    def headers(self):
        h = {'Content-Type': 'application/json', 'Accept': ACCEPT}
        if self.session:
            h['Mcp-Session-Id'] = self.session
        return h

    def request(self, method, params=None, timeout=600):
        rid = self.next_id
        self.next_id += 1
        payload = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            payload['params'] = params
        status, headers, body, t_headers, t_done = post(self.port, payload, self.headers(), timeout)
        if not self.session and 200 <= status < 300:
            self.session = headers.get('mcp-session-id', '')
        msg = parse_like_mcpclienttoolset(headers, body, rid) if 200 <= status < 300 else None
        self.trace.append({'method': method, 'id': rid, 'status': status, 'content_type': headers.get('content-type'),
                           'seconds_to_headers': t_headers, 'seconds': t_done, 'parsed': msg is not None})
        return msg

    def notify_initialized(self):
        status, headers, body, _, _ = post(self.port, {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
                                           self.headers(), 30)
        self.trace.append({'method': 'notifications/initialized', 'status': status, 'body_len': len(body)})
        return status


class EpicClient:
    """A plain 2025-11-25 client for Epic's MCP server"""

    def __init__(self, port):
        self.port = port
        self.session = ''
        self.next_id = 1

    def request(self, method, params=None, timeout=600):
        rid = self.next_id
        self.next_id += 1
        h = {'Content-Type': 'application/json', 'Accept': 'application/json'}
        if self.session:
            h['Mcp-Session-Id'] = self.session
            h['MCP-Protocol-Version'] = '2025-11-25'
        payload = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            payload['params'] = params
        status, headers, body, t_headers, t_done = post(self.port, payload, h, timeout)
        if method == 'initialize':
            self.session = headers.get('mcp-session-id', '')
            post(self.port, {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
                 dict(h, **{'Mcp-Session-Id': self.session, 'MCP-Protocol-Version': '2025-11-25'}), 30)
        try:
            msg = json.loads(body)
        except ValueError:
            msg = {'raw': body[:2000]}
        return status, msg, t_done

    def call(self, tool, arguments, timeout=600):
        return self.request('tools/call', {'name': tool, 'arguments': arguments}, timeout)


def result_text(msg):
    try:
        return ''.join(c.get('text', '') for c in msg['result']['content'] if c.get('type') == 'text')
    except (KeyError, TypeError):
        return ''


def unwrap_registry_text(text):
    """MCPClientToolset hands the registry {"text": "<the server's joined text content>"}; Epic's call_tool returns
    that as its text. Returns the inner text (or the text itself when it is not wrapped)."""
    try:
        obj = json.loads(text)
    except ValueError:
        return text
    if isinstance(obj, dict) and set(obj) == {'text'}:
        return obj['text']
    return text


def tool_names(msg):
    try:
        return sorted(t['name'] for t in msg['result']['tools'])
    except (KeyError, TypeError):
        return []


def patch_uproject(session, project):
    """Enable EXTRA_PLUGINS in the host .uproject; the session restores the file byte for byte at stop()"""
    session.guard_file(project)
    raw = open(project, 'rb').read()
    data = json.loads(raw.decode('utf-8-sig'))
    plugins = data.setdefault('Plugins', [])
    names = {p.get('Name') for p in plugins}
    for name in EXTRA_PLUGINS:
        if name not in names:
            plugins.append({'Name': name, 'Enabled': True})
    with open(project, 'w', encoding='utf-8', newline='\r\n') as f:
        json.dump(data, f, indent='\t')


def wait_for(pattern, session, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        m = re.search(pattern, session.log_text())
        if m:
            return m
        time.sleep(1.0)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8')
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--port', type=int, default=27210, help='first NodeToCode port to try')
    ap.add_argument('--epic-port', type=int, default=27260, help="first port to try for Epic's MCP server")
    ap.add_argument('--no-translate', action='store_true')
    ap.add_argument('--activity-timeout', type=float, default=None)
    ap.add_argument('--eager', action='store_true', help="turn Epic's tool search off")
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    suffix = args.label + ('_t%g' % args.activity_timeout if args.activity_timeout else '') + ('_eager' if args.eager else '')
    log_path = os.path.join(log_dir, 'live_mcpclienttoolset_%s_editor.log' % suffix)
    report_path = os.path.join(log_dir, 'live_mcpclienttoolset_%s.json' % suffix)

    keys = {}
    if not args.no_translate:
        variable = PROVIDER_VARIABLE[args.provider]
        keys = load_provider_keys(only=[variable])
        if variable not in keys:
            print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
            return 2
        if stored_key_present(STORE_ID[args.provider]):
            print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % STORE_ID[args.provider]}))
            return 2

    port = free_port(args.port)
    epic_port = free_port(max(args.epic_port, port + 2))
    url = 'http://127.0.0.1:%d/mcp' % port
    report = {'label': args.label, 'port': port, 'epic_port': epic_port, 'server_url': url,
              'activity_timeout': args.activity_timeout, 'eager': args.eager,
              'translate': None if args.no_translate else {'provider': args.provider, 'model': args.model}}
    checks = {}
    extra = list(BLUEPRINT_EDITOR_ARGS)
    if args.activity_timeout:
        extra.append('-ini:Engine:[HTTP]:HttpActivityTimeout=%g' % args.activity_timeout)
    translate_args = {'provider': args.provider, 'model': args.model, 'language': args.language}

    s = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys, extra_args=extra)
    try:
        patch_uproject(s, args.project)
        user_ini = os.path.join(s._saved_config_dir(), 'EditorPerProjectUserSettings.ini')
        # 'MCPServers=', not '+MCPServers=': on UE 5.8 a '+MCPServers=' line in this Saved/Config file left the list
        # empty (observed, card 0141), and MCPClientToolset then logs nothing at all
        s.patch_file(user_ini, USER_INI_SECTION_SERVERS, 'MCPServers', server_entry(url))
        s.patch_file(user_ini, USER_INI_SECTION_EPIC, 'bAutoStartServer', 'True')
        s.patch_file(user_ini, USER_INI_SECTION_EPIC, 'ServerPortNumber', str(epic_port))
        s.patch_file(user_ini, USER_INI_SECTION_EPIC, 'bEnableToolSearch', 'False' if args.eager else 'True')
        report['user_ini_lines'] = {'servers': 'MCPServers=' + server_entry(url)}

        report['started'] = s.start()
        if not report['started']:
            raise RuntimeError('editor did not start')

        # Startup: MCPClientToolset makes one connection attempt when its editor subsystem initializes
        m = wait_for(r"MCPClientToolset: (registered '%s'|failed to connect[^\r\n]*)" % TOOLSET, s, 60)
        report['mcpclienttoolset_startup'] = m.group(0) if m else None
        checks['registered_at_startup'] = bool(m and m.group(1).startswith('registered'))
        m = re.search(r"MCPClientToolset '%s': registered (\d+) tool" % TOOLSET, s.log_text())
        report['mcpclienttoolset_tool_count'] = int(m.group(1)) if m else None
        log = s.log_text()
        order = {}
        for key, pat in (('n2c_listening', r'MCP HTTP server started on port'),
                         ('mcpclienttoolset_handshake', r"MCPClientToolset '%s': starting Streamable HTTP handshake" % TOOLSET),
                         ('n2c_negotiated', r'Protocol version negotiated: 2024-11-05'),
                         ('mcpclienttoolset_registered', r"MCPClientToolset: registered '%s'" % TOOLSET),
                         ('epic_server', r'Starting MCP server on port')):
            mm = re.search(pat, log)
            order[key] = mm.start() if mm else None
        report['log_order'] = sorted([k for k in order if order[k] is not None], key=lambda k: order[k])
        report['log_missing'] = [k for k in order if order[k] is None]

        # What the editor read from the MCP Toolset Servers entry
        n2c_session = {'Content-Type': 'application/json', 'Accept': ACCEPT,
                       'Mcp-Session-Id': open_session(port, client_name='n2c-0141'), 'MCP-Protocol-Version': '2025-11-25'}
        status, _, body, _, _ = post(port, {'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call', 'params': {
            'name': 'run-python', 'arguments': {'code': SETTINGS_PY}}}, n2c_session, 120)
        report['settings_read_back'] = result_text(json.loads(body))[:1500] if status == 200 else status
        status, _, body, _, _ = post(port, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}, n2c_session, 60)
        reference_tools = tool_names(json.loads(body))
        report['reference_tool_count'] = len(reference_tools)

        # --- replay: MCPClientToolset's sequence straight to NodeToCode ---
        rc = ReplayClient(port)
        init = rc.request('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                         'clientInfo': {'name': 'UE-ToolsetRegistry', 'version': '1.0'}})
        report['replay_negotiated'] = ((init or {}).get('result') or {}).get('protocolVersion')
        checks['replay_initialize'] = bool(init and 'result' in init and rc.session)
        checks['replay_initialized_202'] = rc.notify_initialized() == 202
        listed = rc.request('tools/list')
        replay_tools = tool_names(listed)
        report['replay_tool_count'] = len(replay_tools)
        report['replay_missing_tools'] = sorted(set(reference_tools) - set(replay_tools))
        checks['replay_lists_every_tool'] = bool(replay_tools) and not report['replay_missing_tools']
        checks['replay_no_next_cursor'] = 'nextCursor' not in ((listed or {}).get('result') or {})
        targets = rc.request('tools/call', {'name': 'get-available-translation-targets', 'arguments': {}})
        report['replay_targets_head'] = result_text(targets)[:300]
        checks['replay_targets'] = bool(result_text(targets)) and not (targets.get('result') or {}).get('isError')
        unfocused = rc.request('tools/call', {'name': 'translate-focused-blueprint', 'arguments': translate_args})
        report['replay_unfocused'] = {'isError': (unfocused or {}).get('result', {}).get('isError'),
                                      'text_head': result_text(unfocused)[:300]}
        checks['replay_unfocused_is_error'] = report['replay_unfocused']['isError'] is True

        # --- registry: the same tools through Epic's MCP server ---
        ec = EpicClient(epic_port)
        status, init_msg, _ = ec.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                                         'clientInfo': {'name': 'n2c-0141', 'version': '1.0'}})
        checks['epic_initialize'] = status == 200 and 'result' in init_msg
        status, listed_epic, _ = ec.request('tools/list')
        epic_names = tool_names(listed_epic)
        report['epic_tools_list_count'] = len(epic_names)
        report['epic_tools_list_n2c_names_head'] = [n for n in epic_names if n.startswith(TOOLSET + '.')][:8]
        report['epic_tools_list_head'] = epic_names[:12]
        if args.eager:
            n2c_native = [n for n in epic_names if n.startswith(TOOLSET + '.')]
            report['epic_eager_n2c_tool_count'] = len(n2c_native)
            bad = [n for n in n2c_native if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', n)]
            report['epic_eager_names_outside_claude_pattern'] = len(bad)
        def registry_call(tool, arguments):
            # Tool search (Epic's default): call_tool; eager: the tool is a native MCP tool named NodeToCode.<tool>
            if args.eager:
                return ec.call('%s.%s' % (TOOLSET, tool), arguments)
            return ec.call('call_tool', {'toolset_name': TOOLSET, 'tool_name': tool, 'arguments': arguments})

        if args.eager:
            registry_tools = sorted(n for n in epic_names if n.startswith(TOOLSET + '.'))
            checks['registry_lists_toolset'] = bool(registry_tools)
        else:
            status, catalog, _ = ec.call('list_toolsets', {})
            catalog_text = result_text(catalog)
            report['list_toolsets_n2c_line'] = next((l for l in catalog_text.splitlines() if l.startswith('- %s' % TOOLSET)), None)
            checks['registry_lists_toolset'] = report['list_toolsets_n2c_line'] is not None
            status, described, _ = ec.call('describe_toolset', {'toolset_name': TOOLSET})
            try:
                schema = json.loads(result_text(described))
                registry_tools = sorted(t['name'] for t in schema.get('tools', []))
            except (ValueError, KeyError, TypeError):
                registry_tools = []
        report['registry_tool_count'] = len(registry_tools)
        report['registry_tool_names_head'] = registry_tools[:5]
        checks['registry_names_prefixed'] = bool(registry_tools) and all(n.startswith(TOOLSET + '.') for n in registry_tools)
        report['registry_missing_tools'] = sorted(set(reference_tools) - {n[len(TOOLSET) + 1:] for n in registry_tools})
        checks['registry_lists_every_tool'] = bool(registry_tools) and not report['registry_missing_tools']
        status, rtargets, secs = registry_call('get-available-translation-targets', {})
        inner = unwrap_registry_text(result_text(rtargets))
        report['registry_targets'] = {'status': status, 'isError': (rtargets.get('result') or {}).get('isError'),
                                      'seconds': secs, 'wrapped_head': result_text(rtargets)[:200], 'inner_head': inner[:300]}
        checks['registry_targets'] = status == 200 and bool(inner) and inner == result_text(targets)
        status, runf, secs = registry_call('translate-focused-blueprint', translate_args)
        report['registry_unfocused'] = {'status': status, 'isError': (runf.get('result') or {}).get('isError'),
                                        'text_head': result_text(runf)[:300]}
        # Recorded, not asserted: whether Epic's side still says the call failed
        report['registry_keeps_isError'] = report['registry_unfocused']['isError'] is True

        if not args.no_translate:
            name = 'BP_N2CRegistryProbe_%s' % time.strftime('%H%M%S')
            status, _, body, _, _ = post(port, {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {
                'name': 'run-python', 'arguments': {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}}}, n2c_session, 300)
            report['setup'] = result_text(json.loads(body))[:600] if status == 200 else status

            translated = rc.request('tools/call', {'name': 'translate-focused-blueprint', 'arguments': translate_args})
            chars, graphs = code_chars_of(result_text(translated))
            report['replay_translate'] = dict(rc.trace[-1], isError=(translated or {}).get('result', {}).get('isError'),
                                              code_chars=chars, graphs=graphs)
            checks['replay_translate_has_code'] = chars > 0
            checks['replay_translate_one_json_response'] = 'application/json' in (rc.trace[-1].get('content_type') or '')

            status, rtrans, secs = registry_call('translate-focused-blueprint', translate_args)
            inner = unwrap_registry_text(result_text(rtrans))
            chars, graphs = code_chars_of(inner)
            report['registry_translate'] = {'status': status, 'seconds': secs,
                                            'isError': (rtrans.get('result') or {}).get('isError'),
                                            'code_chars': chars, 'graphs': graphs, 'text_head': result_text(rtrans)[:300]}
            if args.activity_timeout:
                report['registry_translate_timed_out'] = (rtrans.get('result') or {}).get('isError') is True and \
                    'no response received' in result_text(rtrans)
            else:
                checks['registry_translate_has_code'] = chars > 0
        report['replay_trace'] = rc.trace
        checks['editor_alive_at_end'] = s.health()
    except Exception as e:  # recorded in the report; the session still stops and restores
        report['error'] = '%s: %s' % (type(e).__name__, e)
        checks['no_exception'] = False
    finally:
        s.stop()

    report['restored'] = s.restore_ok
    checks['files_restored'] = bool(s.restore_ok)
    log = s.log_text()
    report['editor_answered'] = re.findall(r'Started task [0-9A-F]{32} for tool translate-focused-blueprint '
                                           r'\(progress token [^)]*\), answering ([^\r\n]*)', log)
    report['mcpclienttoolset_log'] = [l.strip()[:300] for l in log.splitlines() if 'LogMCPClientToolset' in l][:20]
    report['checks'] = checks
    leaks = find_leaks(keys, [log_path]) if keys else []
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(checks) and all(checks.values()) and not leaks
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    if keys and find_leaks(keys, [report_path]):
        report = {'ok': False, 'reason': 'a key value reached the report; it was not printed'}
        with open(report_path, 'w', encoding='utf-8') as f:
            json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
