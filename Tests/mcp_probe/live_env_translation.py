"""One live translation through a cloud provider, with the key supplied only through the editor's environment.

Stdlib only; run with `python -I`:

    python -I live_env_translation.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" \
        --provider gemini --model gemini-3.5-flash-lite

It reads the provider's key from the repo's .env (env_keys.py), starts a headless editor with only that key added to
its environment (no key is saved: the per-user secrets file must have none for the provider, or the check would not
prove the environment path), makes /Game/N2CTest/BP_N2CEnvKeyProbe_<time> in memory (an Actor with a Print String node,
never saved), opens it, runs translate-focused-blueprint over MCP and reads the task stream until the response arrives. It passes
when the response parses as a translation with code, Project Settings shows an empty API Key field whose API Key
Source names the variable, the editor log names the variable, and no key value is in the editor log, the run's report
or any translation files. Key values are never printed. Report: logs/live_env_translation.json.
"""
import argparse
import http.client
import json
import os
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402

PROVIDER_VARIABLE = {'anthropic': 'ANTHROPIC_API_KEY', 'openai': 'OPENAI_API_KEY', 'gemini': 'GEMINI_API_KEY',
                     'deepseek': 'DEEPSEEK_API_KEY'}
STORE_ID = {'anthropic': 'Anthropic', 'openai': 'OpenAI', 'gemini': 'Gemini', 'deepseek': 'DeepSeek'}
ASSET_DIR = '/Game/N2CTest'
ASSET_NAME = 'BP_N2CEnvKeyProbe'

# Runs in the editor: makes a new probe Blueprint in memory (a fresh name each run, never saved: a Blueprint made in
# this session is focused as soon as it is opened), opens its EventGraph, adds a Print String node and compiles it
SETUP_PY = r'''
import json, unreal
path = "%(dir)s/%(name)s"
bridge = unreal.N2CPythonBridge
steps = {}
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
steps["created"] = unreal.AssetToolsHelpers.get_asset_tools().create_asset("%(name)s", "%(dir)s", unreal.Blueprint, factory) is not None
steps["opened"] = json.loads(bridge.open_blueprint(path, "EventGraph")).get("success")
found = json.loads(bridge.search_blueprint_nodes("Print String", True, 5, "", True))
def walk(v):
    if isinstance(v, dict):
        yield v
        for x in v.values():
            yield from walk(x)
    elif isinstance(v, list):
        for x in v:
            yield from walk(x)
def key(d):
    return str(d.get("name", d.get("displayName", ""))).replace(" ", "").lower()
action = next((d for d in walk(found) if key(d).startswith("printstring")
               and (d.get("actionIdentifier") or (d.get("spawnMetadata") or {}).get("actionIdentifier"))), None)
if action:
    ident = action.get("actionIdentifier") or action["spawnMetadata"]["actionIdentifier"]
    added = json.loads(bridge.add_node_to_graph("Print String", ident, 300.0, 0.0))
    steps["added_print_string"] = added.get("success")
    if not added.get("success"):
        steps["add_error"] = added.get("error")
else:
    steps["search_head"] = json.dumps(found)[:1200]
steps["compiled"] = json.loads(bridge.compile_focused_blueprint()).get("success")
result = steps
'''


# Runs in the editor: what Project Settings shows for the provider's key (the field and its API Key Source line)
SOURCE_PY = r'''
import unreal
settings = unreal.get_default_object(unreal.N2CSettings)
prefix = "%(store_id)s".lower()
result = {"api_key_field_empty": settings.get_editor_property(prefix + "_api_key_ui") == "",
          "api_key_source": settings.get_editor_property(prefix + "_api_key_source")}
'''


def post(port, payload, timeout=120):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'),
                     headers={'Content-Type': 'application/json'})
        r = conn.getresponse()
        return r.status, r.read().decode('utf-8', 'replace')
    finally:
        conn.close()


def call_tool(port, name, arguments, rid, meta=None):
    params = {'name': name, 'arguments': arguments}
    if meta:
        params['_meta'] = meta
    status, body = post(port, {'jsonrpc': '2.0', 'id': rid, 'method': 'tools/call', 'params': params})
    try:
        return status, json.loads(body)
    except ValueError:
        return status, {'raw': body[:2000]}


def tool_text(response):
    try:
        return ''.join(c.get('text', '') for c in response['result']['content'])
    except (KeyError, TypeError):
        return ''


def read_task_stream(port, task_id, timeout):
    """The task stream's events as [(event, data)] until it closes, the response arrives or `timeout` seconds of wall
    clock pass. The bound is on the whole read, not per socket read: the task sends a progress event every half
    second while it waits for the provider, so a socket timeout alone never fires (a Gemini request that hung for 14
    minutes kept a run waiting). A read that hits the bound ends with ('timeout', '<seconds>')."""
    if len(task_id) == 32:
        task_id = '-'.join((task_id[:8], task_id[8:12], task_id[12:16], task_id[16:20], task_id[20:]))
    events = []
    conn = http.client.HTTPConnection('127.0.0.1', port + 1, timeout=timeout)

    def read():
        event, data = None, []
        try:
            conn.request('GET', '/mcp/events/%s' % task_id)
            r = conn.getresponse()
            for raw in r:
                line = raw.decode('utf-8', 'replace').rstrip('\r\n')
                if line.startswith('event:'):
                    event = line[6:].strip()
                elif line.startswith('data:'):
                    data.append(line[6:] if line[5:6] == ' ' else line[5:])
                elif not line and (event or data):
                    # A blank line ends an event; its data lines are joined with newlines
                    events.append((event or 'message', '\n'.join(data)))
                    if event == 'response':
                        break
                    event, data = None, []
        except (OSError, http.client.HTTPException):
            pass  # closed below on timeout, or the server went away; the caller sees no response event

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    reader.join(timeout)
    timed_out = reader.is_alive()
    if timed_out and conn.sock is not None:
        try:
            conn.sock.shutdown(socket.SHUT_RDWR)  # unblocks the reader
        except OSError:
            pass
    conn.close()
    reader.join(5)
    result = list(events)
    if timed_out:
        result.append(('timeout', str(timeout)))
    return result


def stored_key_present(store_id):
    """Whether the per-user secrets file holds a key for store_id (the value is not read out)"""
    base = os.environ.get('LOCALAPPDATA') if os.name == 'nt' else os.path.expanduser('~/Library/Application Support/Epic')
    path = os.path.join(base or '', 'NodeToCode', 'secrets.json')
    try:
        data = json.load(open(path, encoding='utf-8-sig'))
    except (OSError, ValueError):
        return False
    return bool(str(data.get('%s_API_Key' % store_id, '')).strip())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--provider', default='gemini', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='gemini-3.5-flash-lite')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--port', type=int, default=27170)
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--log', default=os.path.join(HERE, 'logs', 'live_env_translation_editor.log'))
    ap.add_argument('--report', default=os.path.join(HERE, 'logs', 'live_env_translation.json'))
    args = ap.parse_args()

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file, so the environment '
                          'variable would not be used' % STORE_ID[args.provider]}))
        return 2

    os.makedirs(os.path.dirname(args.log), exist_ok=True)
    port = free_port(args.port)
    report = {'provider': args.provider, 'model': args.model, 'variable': variable, 'port': port}
    translations_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved', 'NodeToCode', 'Translations')
    before = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()

    with EditorSession(args.engine, args.project, port, args.log, allow_python=True, extra_env=keys) as s:
        report['started'] = s.start()
        if report['started']:
            name = '%s_%s' % (ASSET_NAME, time.strftime('%H%M%S'))
            status, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = {'status': status, 'text': tool_text(setup)[:2000] or setup}
            status, source = call_tool(port, 'run-python', {'code': SOURCE_PY % {'store_id': STORE_ID[args.provider]}}, 4)
            report['settings'] = json.loads(tool_text(source) or '{}').get('data')
            status, providers = call_tool(port, 'get-available-llm-providers', {}, 5)
            report['providers_listed'] = sorted(p.get('id', '') for p in json.loads(tool_text(providers) or '{}').get('providers', []))
            started_at = time.time()
            status, started = call_tool(port, 'translate-focused-blueprint',
                                        {'provider': args.provider, 'model': args.model, 'language': args.language}, 2,
                                        meta={'progressToken': 'live-env-translation'})
            report['task_call'] = {'status': status}
            result = started.get('result') or {}
            task_id = result.get('taskId')
            if task_id:
                events = read_task_stream(port, task_id, args.timeout)
                report['seconds'] = round(time.time() - started_at, 1)
                report['events'] = {e: sum(1 for x, _ in events if x == e) for e in sorted({x for x, _ in events})}
                final = [d for e, d in events if e == 'response']
                if final:
                    try:
                        message = json.loads(final[-1])
                    except ValueError:
                        message = {}
                    text = tool_text(message)
                    report['response_shape'] = {'message': sorted(message), 'result': sorted(message.get('result') or {}),
                                                'text_head': text[:400], 'raw_head': final[-1][:400]}
                    report['is_error'] = bool((message.get('result') or {}).get('isError')) or 'error' in message
                    try:
                        translation = json.loads(text)
                    except ValueError:
                        translation = None
                    report['response_parsed'] = isinstance(translation, dict)
                    if isinstance(translation, dict):
                        graphs = translation.get('graphs') or []
                        report['graphs'] = len(graphs)
                        report['code_chars'] = sum(len((g.get('code') or {}).get('graphDeclaration', '') or '')
                                                   + len((g.get('code') or {}).get('graphImplementation', '') or '')
                                                   for g in graphs if isinstance(g, dict))
                        report['response_keys'] = sorted(translation)
                    else:
                        report['response_head'] = text[:600]
            else:
                report['task_start'] = started
    log_text = s.log_text()
    report['log_names_variable'] = ('the %s environment variable is used' % variable) in log_text
    after = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    new_dirs = [os.path.join(translations_dir, d) for d in sorted(after - before)]
    report['translation_dirs'] = new_dirs

    # The report is checked too, so write it first; nothing in it was read from a key
    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [args.log, args.report] + new_dirs)
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    settings = report.get('settings') or {}
    report['ok'] = bool(report.get('response_parsed') and not report.get('is_error') and report.get('code_chars')
                        and report['log_names_variable'] and not leaks and settings.get('api_key_field_empty')
                        and variable in (settings.get('api_key_source') or ''))
    with open(args.report, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
