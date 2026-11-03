"""Discovery polish against a running editor (card 0214): serverInfo, completion, resource annotations, the Python stub
resource and a resource update notification on a 2026-07-28 subscriptions/listen stream.

Stdlib only; run with `python -I`, on UE 5.8 for the update notification (5.6/5.7 cannot keep a stream open; the
other checks run on every engine):

    python -I live_discovery_0214.py --engine <UE_5.8> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor (Allow MCP Python Execution on, for one run-python that compiles a fixture Blueprint) on a
host that links Tests/NodeToCodeTestContent. Checks: initialize's serverInfo has title, description, websiteUrl and a
data: URI PNG icon, and capabilities have completions; completion/complete offers the four BP_N2CDep_ Blueprints for
nodetocode://blueprint/{+path} and python for generate-code's language; resources/list annotates each resource with
audience and priority (no readOnly) and lists nodetocode://python/unreal-stub, which reads as Python or as the note
on how to generate it. On 5.8: server/discover advertises resources.subscribe and carries the icon; a
subscriptions/listen stream subscribed to BP_N2CDep_A's resource gets notifications/resources/updated with that uri
and its subscription id within a few seconds of compiling it. Report: logs/live_discovery_0214_<label>.json.
"""
import argparse
import base64
import http.client
import json
import os
import queue
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_env_translation import call_tool, post  # noqa: E402

FIXTURE = 'NodeToCodeTestContent/Dependencies/BP_N2CDep_'
A_URI = 'nodetocode://blueprint/NodeToCodeTestContent/Dependencies/BP_N2CDep_A'
MODERN_META = {'io.modelcontextprotocol/protocolVersion': '2026-07-28',
               'io.modelcontextprotocol/clientInfo': {'name': 'live-discovery-0214', 'version': '1'},
               'io.modelcontextprotocol/clientCapabilities': {}}
COMPILE_PY = '''
import unreal
bp = unreal.load_asset("/NodeToCodeTestContent/Dependencies/BP_N2CDep_A")
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
print("compiled", bp is not None)
'''


def raw(port, payload, headers=None, timeout=60):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=timeout)
    try:
        conn.request('POST', '/mcp', body=json.dumps(payload).encode('utf-8'),
                     headers=dict({'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}, **(headers or {})))
        r = conn.getresponse()
        return r.status, r.read().decode('utf-8', 'replace')
    finally:
        conn.close()


def modern_headers(method):
    return {'MCP-Protocol-Version': '2026-07-28', 'Mcp-Method': method}


def listen(port, subscription_id, uris, events, stop):
    """Opens subscriptions/listen and puts every SSE message (parsed JSON) on events until stop is set"""
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=60)
    try:
        params = {'_meta': MODERN_META, 'notifications': {'resourceSubscriptions': uris}}
        conn.request('POST', '/mcp', body=json.dumps({'jsonrpc': '2.0', 'id': subscription_id, 'method': 'subscriptions/listen',
                                                      'params': params}).encode('utf-8'),
                     headers=dict({'Content-Type': 'application/json', 'Accept': 'text/event-stream'}, **modern_headers('subscriptions/listen')))
        r = conn.getresponse()
        events.put(('status', r.status, r.getheader('Content-Type')))
        data = []
        while not stop.is_set():
            line = r.fp.readline()
            if not line:
                break
            line = line.decode('utf-8', 'replace').rstrip('\r\n')
            if line.startswith('data:'):
                data.append(line[5:].strip())
            elif line == '' and data:
                try:
                    events.put(('message', json.loads('\n'.join(data))))
                except ValueError:
                    pass
                data = []
    except Exception as e:  # recorded; the main thread decides
        events.put(('error', str(e)))
    finally:
        conn.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27192)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_discovery_0214_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_discovery_0214_%s.json' % args.label)
    port = free_port(args.port)
    report = {'label': args.label, 'port': port}
    checks = {}
    streams = '5.8' in args.engine or '5_8' in args.project

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            status, body = raw(port, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'live-discovery-0214', 'version': '1'}}})
            init = (json.loads(body).get('result') or {}) if status == 200 else {}
            info = init.get('serverInfo') or {}
            icon = (info.get('icons') or [{}])[0]
            png = b''
            if (icon.get('src') or '').startswith('data:image/png;base64,'):
                png = base64.b64decode(icon['src'].split(',', 1)[1])
            report['serverInfo'] = {k: v for k, v in info.items() if k != 'icons'}
            report['icon_bytes'] = len(png)
            checks['initialize: serverInfo title, description, websiteUrl'] = info.get('title') == 'NodeToCode' and \
                bool(info.get('description')) and (info.get('websiteUrl') or '').startswith('https://')
            checks['initialize: a data: URI PNG icon'] = png[:4] == b'\x89PNG'
            checks['initialize: completions advertised'] = isinstance((init.get('capabilities') or {}).get('completions'), dict)

            status, body = post(port, {'jsonrpc': '2.0', 'id': 2, 'method': 'completion/complete', 'params': {
                'ref': {'type': 'ref/resource', 'uri': 'nodetocode://blueprint/{+path}'},
                'argument': {'name': 'path', 'value': FIXTURE.lower()}}})
            values = ((json.loads(body).get('result') or {}).get('completion') or {}).get('values') if status == 200 else None
            report['completion_paths'] = values
            checks['completion: the four BP_N2CDep_ Blueprints'] = values == [FIXTURE + n for n in ('A', 'B', 'C', 'Child')]
            status, body = post(port, {'jsonrpc': '2.0', 'id': 3, 'method': 'completion/complete', 'params': {
                'ref': {'type': 'ref/prompt', 'name': 'generate-code'}, 'argument': {'name': 'language', 'value': 'py'}}})
            values = ((json.loads(body).get('result') or {}).get('completion') or {}).get('values') if status == 200 else None
            report['completion_language'] = values
            checks['completion: generate-code language py -> python'] = values == ['python']

            status, body = post(port, {'jsonrpc': '2.0', 'id': 4, 'method': 'resources/list', 'params': {}})
            resources = (json.loads(body).get('result') or {}).get('resources') or [] if status == 200 else []
            report['resources'] = [{'uri': r.get('uri'), 'annotations': r.get('annotations')} for r in resources]
            checks['resources: audience and priority on each, no readOnly'] = bool(resources) and all(
                'priority' in (r.get('annotations') or {}) and 'assistant' in ((r.get('annotations') or {}).get('audience') or [])
                and 'readOnly' not in (r.get('annotations') or {}) for r in resources)
            checks['resources: the Python stub is listed'] = any(r.get('uri') == 'nodetocode://python/unreal-stub' for r in resources)
            status, body = post(port, {'jsonrpc': '2.0', 'id': 5, 'method': 'resources/read', 'params': {'uri': 'nodetocode://python/unreal-stub'}}, timeout=300)
            contents = ((json.loads(body).get('result') or {}).get('contents') or [{}])[0] if status == 200 else {}
            text = contents.get('text') or ''
            report['stub'] = {'mimeType': contents.get('mimeType'), 'chars': len(text), 'head': text[:120]}
            checks['resources: the stub reads as Python or as how to generate it'] = contents.get('mimeType') == 'text/x-python' and \
                ('class ' in text or 'Developer Mode' in text)

            if streams:
                status, body = raw(port, {'jsonrpc': '2.0', 'id': 6, 'method': 'server/discover', 'params': {'_meta': MODERN_META}},
                                   modern_headers('server/discover'))
                discover = (json.loads(body).get('result') or {}) if status == 200 else {}
                minfo = ((discover.get('_meta') or {}).get('io.modelcontextprotocol/serverInfo')) or {}
                checks['discover: resources.subscribe and the icon'] = \
                    ((discover.get('capabilities') or {}).get('resources') or {}).get('subscribe') is True and bool(minfo.get('icons'))

                events, stop = queue.Queue(), threading.Event()
                thread = threading.Thread(target=listen, args=(port, 'sub-live', [A_URI], events, stop), daemon=True)
                thread.start()
                ack, update, started = None, None, None
                deadline = time.time() + 30
                while time.time() < deadline and update is None:
                    try:
                        kind, *rest = events.get(timeout=1)
                    except queue.Empty:
                        continue
                    if kind == 'message':
                        message = rest[0]
                        if message.get('method') == 'notifications/subscriptions/acknowledged' and ack is None:
                            ack = message
                            started = time.time()
                            report['compile'] = call_tool(port, 'run-python', {'code': COMPILE_PY}, 7)[1].get('result', {}).get('isError')
                        elif message.get('method') == 'notifications/resources/updated':
                            update = message
                            report['update_seconds'] = round(time.time() - started, 2)
                    elif kind == 'error':
                        report['listen_error'] = rest[0]
                        break
                stop.set()
                report['ack'] = ack
                report['update'] = update
                accepted = ((ack or {}).get('params') or {}).get('notifications') or {}
                checks['listen: the Blueprint resource is acknowledged'] = accepted.get('resourceSubscriptions') == [A_URI]
                params = (update or {}).get('params') or {}
                checks['listen: notifications/resources/updated after the compile'] = params.get('uri') == A_URI and \
                    ((params.get('_meta') or {}).get('io.modelcontextprotocol/subscriptionId')) == 'sub-live'
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
