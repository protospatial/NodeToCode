"""NodeToCodeTools translation, live through Epic's Unreal MCP on UE 5.8 with a real provider (card 0176).

Stdlib only; run with `python -I`:

    python -I live_toolset_0176.py --engine G:/UE/UE_5.8 --project <5.8 host .uproject> --label 5.8

For the length of the run the host .uproject enables Epic's ToolsetRegistry and ModelContextProtocol plugins (put back
byte for byte when the editor stops), Epic's MCP server starts on its own port, and the editor gets ANTHROPIC_API_KEY
from the repo's .env in its environment only (env_keys). A scratch Actor Blueprint with two functions is made under
/Game/N2CTest/Toolset0176_<time> and both are tagged. Then, over Epic's MCP server only (call_tool):

1. translate_graph(first function, cpp, anthropic, --model) is sent with _meta.progressToken on its own thread while the
   main thread polls get_batch_status(""): the batch is seen running, the call ends with code from the requested model,
   and Epic's server sent progress heartbeats on the call's stream.
2. translate_graph(second function) is started the same way, get_batch_status("") sees it running, cancel_batch("")
   stops it: the call ends within seconds with "Cancelled by cancel_batch" and the batch is cancelled.
3. translate_graphs_by_tag(tag) returns a batch id at once; get_batch_status(id) is polled to succeeded with both
   functions completed.

No key value may appear in the editor log, Saved/NodeToCode, Saved/Logs or this run's logs. The scratch folder, the tag
and the BatchTranslation_ folders the run made are removed.

Report: logs/live_toolset_0176_<label>.json, editor log next to it.
"""
import argparse
import glob
import json
import os
import re
import shutil
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_graph_target_0152 import Probe  # noqa: E402
from live_mcpclienttoolset import EpicClient, parse_like_mcpclienttoolset, post, result_text, wait_for  # noqa: E402
from live_toolset_0175 import patch_uproject, python_problems  # noqa: E402

NAME = 'BP_N2CToolset0176'
FUNCTIONS = ('N2CToolsetFnA', 'N2CToolsetFnB')

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None}
if bp:
    result["functions"] = [unreal.BlueprintEditorLibrary.add_function_graph(bp, n) is not None for n in %(fns)r]
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
for t in unreal.N2CTagBlueprintLibrary.get_graphs_with_tag("%(tag)s", ""):
    unreal.N2CTagBlueprintLibrary.remove_tag(t.graph_guid, t.tag, t.category)
unreal.N2CTagBlueprintLibrary.save_tags()
bp = unreal.load_asset(folder + "/%(name)s")
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''


def batch_folders(saved_dir):
    return set(glob.glob(os.path.join(saved_dir, 'NodeToCode', 'Translations', 'BatchTranslation_*')))


def value_of(text):
    """call_tool's text is {"returnValue": ...} (or the value itself)"""
    try:
        obj = json.loads(text)
    except ValueError:
        return text
    if isinstance(obj, dict) and set(obj) == {'returnValue'}:
        return obj['returnValue']
    return obj


class Toolset:
    def __init__(self, ec, toolset):
        self.ec = ec
        self.toolset = toolset

    def call(self, tool, arguments, timeout=600):
        status, msg, secs = self.ec.call('call_tool', {'toolset_name': self.toolset, 'tool_name': tool,
                                                       'arguments': arguments}, timeout)
        result = msg.get('result') or {}
        text = result_text(msg)
        return {'status': status, 'isError': bool(result.get('isError') or msg.get('error')), 'text': text,
                'value': value_of(text), 'seconds': round(secs, 2)}

    def call_with_progress(self, tool, arguments, out, timeout=600):
        """tools/call with _meta.progressToken, read as a stream: out gets the result and the heartbeats seen"""
        ec = self.ec
        rid = ec.next_id
        ec.next_id += 1
        payload = {'jsonrpc': '2.0', 'id': rid, 'method': 'tools/call',
                   'params': {'name': 'call_tool', '_meta': {'progressToken': 'n2c-0176-%d' % rid},
                              'arguments': {'toolset_name': self.toolset, 'tool_name': tool, 'arguments': arguments}}}
        headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
                   'Mcp-Session-Id': ec.session, 'MCP-Protocol-Version': '2025-11-25'}
        t0 = time.time()
        try:
            status, h, body, _, _ = post(ec.port, payload, headers, timeout)
        except Exception as e:  # noqa: BLE001
            out.update({'exception': repr(e), 'seconds': round(time.time() - t0, 2)})
            return
        msg = parse_like_mcpclienttoolset(h, body, rid) or {}
        result = msg.get('result') or {}
        text = result_text(msg)
        out.update({'status': status, 'content_type': h.get('content-type', ''),
                    'heartbeats': len(re.findall(r'notifications/progress', body)),
                    'isError': bool(result.get('isError') or msg.get('error')), 'text': text,
                    'value': value_of(text), 'error': msg.get('error'), 'seconds': round(time.time() - t0, 2)})


def poll_status(ts, batch_id, until, timeout, seen):
    """get_batch_status(batch_id) every half second until until(value) or timeout; every value goes into seen"""
    end = time.time() + timeout
    while time.time() < end:
        r = ts.call('get_batch_status', {'batch_id': batch_id}, 60)
        v = r['value'] if isinstance(r['value'], dict) else {}
        seen.append({k: v.get(k) for k in ('batch_id', 'state', 'progress', 'message')} if v else {'raw': r['text'][:300]})
        if v and until(v):
            return v
        time.sleep(0.5)
    return None


def run(args, report, checks, log_path, keys):
    port = free_port(args.port)
    epic_port = free_port(args.epic_port)
    if epic_port == port:
        epic_port = free_port(args.epic_port + 1)
    stamp = time.strftime('%H%M%S')
    folder = '/Game/N2CTest/Toolset0176_%s' % stamp
    path = '%s/%s.%s' % (folder, NAME, NAME)
    tag = 'n2c_0176_%s' % stamp
    report.update({'port': port, 'epic_port': epic_port, 'folder': folder, 'tag': tag, 'model': args.model})
    extra = ['-ModelContextProtocolPort=%d' % epic_port, '-ModelContextProtocolStartServer']
    s = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_args=extra, extra_env=keys)
    try:
        patch_uproject(s, args.project)
        report['started'] = s.start()
        checks['editor started'] = report['started']
        if not report['started']:
            return s
        m = wait_for(r'Registering Toolset (\S*NodeToCodeTools)', s, 120)
        toolset = m.group(1) if m else 'nodetocode.toolsets.n2c_toolset.NodeToCodeTools'
        report['toolset_name'] = toolset
        wait_for(r'Starting MCP server on port \d+|MCP server.*\b%d\b' % epic_port, s, 60)

        p = Probe(port)
        data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME, 'fns': list(FUNCTIONS)})
        report['setup'] = data or text
        checks['setup: Blueprint and two functions made'] = data.get('created') is True and \
            data.get('functions') == [True, True]

        ec = EpicClient(epic_port)
        status, init_msg, _ = ec.request('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                                         'clientInfo': {'name': 'n2c-0176', 'version': '1.0'}})
        checks['epic initialize'] = status == 200 and 'result' in init_msg
        ts = Toolset(ec, toolset)
        for fn in FUNCTIONS:
            r = ts.call('tag_graph', {'blueprint_path': path, 'graph_name': fn, 'tag': tag, 'category': 'N2C0176',
                                      'description': ''})
            checks['tag_graph %s' % fn] = not r['isError']

        common = {'target_language': 'cpp', 'provider': 'anthropic', 'model': args.model}

        # 1. translate_graph, polled to completion
        out = {}
        worker = threading.Thread(target=ts.call_with_progress, args=(
            'translate_graph', dict(common, blueprint_path=path, graph_name=FUNCTIONS[0]), out))
        worker.start()
        seen = report['translate_status_polls'] = []
        poll_status(ts, '', lambda v: v.get('state') != 'running', args.timeout, seen)
        worker.join(args.timeout)
        report['translate'] = {k: (v if k != 'text' else v[:1500]) for k, v in out.items() if k != 'value'}
        value = out.get('value')
        try:
            value = json.loads(value) if isinstance(value, str) else value
        except ValueError:
            value = None
        value = value if isinstance(value, dict) else {}
        report['translate_value_keys'] = list(value)
        graphs = value.get('graphs') or []
        code = ((graphs[0] if graphs else {}).get('code') or {}).get('graphImplementation', '')
        checks['translate_graph: the batch was seen running'] = any(x.get('state') == 'running' for x in seen)
        checks['translate_graph: ends with code, no error'] = out.get('status') == 200 and not out.get('isError') \
            and bool(code.strip())
        checks['translate_graph: the requested provider and model'] = value.get('provider') == 'anthropic' and \
            value.get('model') == args.model
        checks['translate_graph: marked untrusted first'] = list(value)[:1] == ['untrusted_content']
        checks['translate_graph: files saved'] = bool(value.get('files')) and all(
            os.path.isfile(f) for f in value.get('files', []))
        checks["translate_graph: Epic's server sent progress heartbeats"] = out.get('heartbeats', 0) > 0
        final = seen[-1] if seen else {}
        checks['get_batch_status: succeeded at progress 1'] = final.get('state') == 'succeeded' and \
            final.get('progress') == 1.0

        # 2. A second translate_graph, cancelled
        out2 = {}
        worker = threading.Thread(target=ts.call_with_progress, args=(
            'translate_graph', dict(common, blueprint_path=path, graph_name=FUNCTIONS[1]), out2))
        worker.start()
        seen2 = report['cancel_status_polls'] = []
        running = poll_status(ts, '', lambda v: v.get('state') == 'running' and
                              'Translating' in (v.get('message') or ''), 60, seen2)
        checks['cancel: the second translation was running'] = running is not None
        t_cancel = time.time()
        r = report['cancel_batch'] = ts.call('cancel_batch', {'batch_id': ''})
        worker.join(60)
        report['cancelled_call'] = {k: v for k, v in out2.items() if k != 'value'}
        report['cancel_to_end_seconds'] = round(time.time() - t_cancel, 2)
        checks['cancel: cancel_batch succeeds'] = not r['isError']
        checks['cancel: the call ends with "Cancelled by cancel_batch" within 10 s'] = \
            out2.get('isError') is True and 'Cancelled by cancel_batch' in (out2.get('text') or '') and \
            report['cancel_to_end_seconds'] < 10
        st = ts.call('get_batch_status', {'batch_id': ''})['value']
        checks['cancel: the batch is cancelled'] = isinstance(st, dict) and st.get('state') == 'cancelled'

        # 3. translate_graphs_by_tag, polled to completion
        r = report['by_tag_start'] = ts.call('translate_graphs_by_tag', dict(common, tag=tag, category=''))
        batch_id = r['value'] if isinstance(r['value'], str) else ''
        checks['translate_graphs_by_tag: returns a batch id at once'] = bool(batch_id) and not r['isError'] and \
            r['seconds'] < 10
        seen3 = report['batch_status_polls'] = []
        done = poll_status(ts, batch_id, lambda v: v.get('state') != 'running', args.timeout, seen3) or {}
        report['batch_final'] = {k: done.get(k) for k in ('state', 'progress', 'message', 'provider', 'model',
                                                          'output_path', 'items', 'error')}
        items = done.get('items') or []
        checks['batch: seen running with progress below 1'] = any(
            x.get('state') == 'running' and (x.get('progress') or 0) < 1 for x in seen3)
        checks['batch: succeeded with both functions completed'] = done.get('state') == 'succeeded' and \
            sorted(i.get('graph_name') for i in items) == sorted(FUNCTIONS) and \
            all(i.get('status') == 'completed' for i in items)
        checks['batch: marked untrusted first'] = list(done)[:1] == ['untrusted_content']

        data, text = p.python(CLEANUP_PY % {'folder': folder, 'name': NAME, 'tag': tag})
        report['cleanup'] = data or text
        checks['cleanup: scratch folder deleted'] = data.get('exists_after') is False
    finally:
        s.stop()
        report['restored'] = s.restore_ok
        checks['host .uproject and config restored byte for byte'] = s.restore_ok is True
        problems = python_problems(s.log_text())
        report['python_log_lines'] = problems
        checks['no Python error or warning in the log'] = not any(
            re.search(r'LogPython: (Error|Warning)', line) for line in problems)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--port', type=int, default=27230, help='first NodeToCode port to try')
    ap.add_argument('--epic-port', type=int, default=8000, help="first port to try for Epic's MCP server")
    ap.add_argument('--timeout', type=int, default=300)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    stem = 'live_toolset_0176_%s' % args.label
    log_path = os.path.join(log_dir, stem + '_editor.log')
    report_path = os.path.join(log_dir, stem + '.json')
    saved_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Saved')

    keys = load_provider_keys(only=['ANTHROPIC_API_KEY'])
    report = {'label': args.label, 'key_present': 'ANTHROPIC_API_KEY' in keys}
    checks = {'ANTHROPIC_API_KEY in .env': 'ANTHROPIC_API_KEY' in keys}
    before = batch_folders(saved_dir)
    run(args, report, checks, log_path, keys)

    made = sorted(batch_folders(saved_dir) - before)
    leaks = find_leaks(keys, [log_path, os.path.join(saved_dir, 'NodeToCode'), os.path.join(saved_dir, 'Logs')])
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    checks["no key in the editor log, Saved/NodeToCode or Saved/Logs"] = not leaks
    for folder in made:
        shutil.rmtree(folder, ignore_errors=True)
    report['removed_batch_folders'] = made
    report['checks'] = checks
    report['pass'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    leaks_after = find_leaks(keys, [report_path])
    if leaks_after:
        os.remove(report_path)
        print('FAIL: a key reached the report; report removed')
        return 1
    for k, v in checks.items():
        print('%s  %s' % ('PASS' if v else 'FAIL', k))
    print('%s: %s' % ('PASS' if report['pass'] else 'FAIL', report_path))
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
