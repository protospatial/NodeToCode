"""Card 0149 live check: a batch of three graphs cancelled while the second is being translated, and one translation
with a tiny HTTP Activity Timeout against a local endpoint that accepts the request and never answers.

Stdlib only; run with `python -I`:

    python -I live_llm_handles_0149.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

One headless editor (-N2CMcp, run-python allowed). The batch runs on Anthropic claude-haiku-5-5 (the key comes from
the repo's .env into the editor's environment only, never printed): three in-memory probe Blueprints (probe_blueprint
SETUP_PY, never saved) are tagged and batch-translated through UN2CBatchTranslationBlueprintLibrary; as soon as the
orchestrator reports item 1 (the second graph) in progress, run-python calls CancelBatchTranslation and reads, in the
same game-thread step, whether the batch has completed and its counts. Pass: the batch completed inside the cancel,
1 succeeded and 2 were skipped, and the log shows the in-flight translation was cancelled and no third request was
sent.

The timeout check starts a TCP listener in this process that accepts connections and never replies, points Ollama's
endpoint at it, sets HTTP Activity Timeout to 3 s (on the settings object, for the session only) and calls
translate-focused-blueprint with provider ollama. Pass: the call fails in under 30 s with error_kind Timeout.
Report: logs/live_llm_handles_0149_<label>.json.
"""
import argparse
import json
import os
import re
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ASSET_DIR, STORE_ID, run_tool_call, stored_key_present, tool_text, call_tool  # noqa: E402
from probe_blueprint import SETUP_PY  # noqa: E402

SETTINGS_SECTION = '[/Script/NodeToCode.N2CSettings]'
MODEL = 'claude-haiku-5-5'
TAG = 'N2CCard0149'

TAG_PY = r"""
import json, unreal
res = json.loads(unreal.N2CPythonBridge.tag_focused_graph("%(tag)s", "N2CGate", "card 0149 live check"))
result = res.get("data") or {"error": res.get("error")}
"""

BATCH_START_PY = r"""
import unreal
lib = unreal.N2CBatchTranslationBlueprintLibrary
lib.translate_graphs_with_tag("%(tag)s", "")
result = {"in_progress": lib.is_batch_translation_in_progress()}
"""

# Cancels as soon as item 1 is in flight, and reads the outcome in the same game-thread step
CANCEL_WHEN_SECOND_PY = r"""
import time, unreal
lib = unreal.N2CBatchTranslationBlueprintLibrary
orch = lib.get_batch_orchestrator()
index = orch.get_current_item_index()
result = {"index": index, "in_progress": lib.is_batch_translation_in_progress()}
if lib.is_batch_translation_in_progress() and index == 1:
    t0 = time.perf_counter()
    lib.cancel_batch_translation()
    result["cancel_seconds"] = round(time.perf_counter() - t0, 4)
    result["in_progress_after_cancel"] = lib.is_batch_translation_in_progress()
    r = orch.get_current_result()
    result["counts"] = {"total": r.total_count, "success": r.success_count, "failure": r.failure_count, "skipped": r.skipped_count}
    result["output"] = str(r.batch_output_path)
    result["module_status"] = str(unreal.N2CLLMModule.get_bp().get_system_status())
    result["cancelled"] = True
"""

STALL_SETTINGS_PY = r"""
import unreal
s = unreal.get_default_object(unreal.N2CSettings)
cfg = s.get_editor_property("ollama_config")
previous = {"endpoint": str(cfg.get_editor_property("ollama_endpoint")), "timeout": s.get_editor_property("http_activity_timeout_seconds")}
cfg.set_editor_property("ollama_endpoint", "%(endpoint)s")
s.set_editor_property("ollama_config", cfg)
s.set_editor_property("http_activity_timeout_seconds", %(timeout)s)
result = {"previous": previous, "now": s.get_editor_property("http_activity_timeout_seconds")}
"""


class StallServer:
    """Accepts TCP connections, reads what arrives and never answers"""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        self.connections = []
        self.bytes_received = 0
        self.stop = False
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while not self.stop:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            self.connections.append(conn)
            threading.Thread(target=self._read, args=(conn,), daemon=True).start()

    def _read(self, conn):
        while not self.stop:
            try:
                data = conn.recv(65536)
            except OSError:
                return
            if not data:
                return
            self.bytes_received += len(data)

    def close(self):
        self.stop = True
        for c in self.connections:
            try:
                c.close()
            except OSError:
                pass
        self.sock.close()


def run_py(port, code, rid):
    status, response = call_tool(port, 'run-python', {'code': code}, rid)
    text = tool_text(response)
    try:
        return json.loads(text or '{}').get('data')
    except ValueError:
        return {'status': status, 'text': text[:600]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8')
    ap.add_argument('--port', type=int, default=27191)
    args = ap.parse_args()

    keys = load_provider_keys(only=['ANTHROPIC_API_KEY'])
    if 'ANTHROPIC_API_KEY' not in keys:
        print(json.dumps({'ok': False, 'reason': 'ANTHROPIC_API_KEY not in .env'}))
        return 2
    if stored_key_present(STORE_ID['anthropic']):
        print(json.dumps({'ok': False, 'reason': 'an Anthropic key is saved in the per-user secrets file'}))
        return 2

    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    log_path = os.path.join(logs, 'live_llm_handles_0149_%s_editor.log' % args.label)
    report_path = os.path.join(logs, 'live_llm_handles_0149_%s.json' % args.label)
    project_dir = os.path.dirname(os.path.abspath(args.project))
    port = free_port(args.port)
    report = {'label': args.label, 'date': time.strftime('%Y-%m-%d'), 'model': MODEL}
    stall = StallServer()

    session = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                            extra_args=['-LogCmds=LogNodeToCode VeryVerbose', *BLUEPRINT_EDITOR_ARGS])
    with session as s:
        s._patch('NodeToCode.ini', SETTINGS_SECTION, 'Provider', 'Anthropic')
        s._patch('NodeToCode.ini', SETTINGS_SECTION, 'AnthropicModelId', MODEL)
        for state_file in ('BlueprintGraphState.json', 'BlueprintGraphState.json.bak'):
            s.guard_file(os.path.join(project_dir, 'Saved', 'NodeToCode', state_file))
        report['started'] = s.start()
        if report['started']:
            # Three probe Blueprints, each focused when made, each tagged
            report['setup'] = []
            for i, letter in enumerate('ABC'):
                name = 'BP_N2C0149%s_%s' % (letter, time.strftime('%H%M%S'))
                setup = run_py(port, SETUP_PY % {'dir': ASSET_DIR, 'name': name}, 10 + i)
                tagged = run_py(port, TAG_PY % {'tag': TAG}, 20 + i)
                report['setup'].append({'name': name, 'setup_ok': bool(setup and setup.get('print_string_live')),
                                        'tag': tagged})
            print('setup', json.dumps(report['setup'])[:800], flush=True)

            # The batch, cancelled while item 1 is in flight
            report['batch_start'] = run_py(port, BATCH_START_PY % {'tag': TAG}, 30)
            polls = []
            outcome = None
            deadline = time.time() + 120
            rid = 40
            while time.time() < deadline:
                rid += 1
                r = run_py(port, CANCEL_WHEN_SECOND_PY, rid)
                polls.append(r)
                if not r or r.get('cancelled') or not r.get('in_progress'):
                    outcome = r
                    break
                time.sleep(0.15)
            report['batch_polls'] = len(polls)
            report['batch_outcome'] = outcome
            time.sleep(5.0)  # anything the cancelled request or a third item would log
            text = session.log_text() or ''
            report['batch_log'] = {
                'sending_requests': len(re.findall(r'Sending request to EN2CLLMProvider::Anthropic', text)),
                'cancelled_in_flight': len(re.findall(r'Cancelled the in-flight translation of', text)),
                'request_cancelled': len(re.findall(r'LLM request cancelled', text)),
                'batch_complete_line': re.findall(r'Batch translation complete\. [^\r\n]*', text)[-1:],
            }
            counts = (outcome or {}).get('counts') or {}
            report['batch_ok'] = bool(outcome and outcome.get('cancelled')
                                      and outcome.get('in_progress_after_cancel') is False
                                      and counts == {'total': 3, 'success': 1, 'failure': 0, 'skipped': 2}
                                      and report['batch_log']['sending_requests'] == 2
                                      and report['batch_log']['cancelled_in_flight'] == 1)
            print('batch', json.dumps({'outcome': outcome, 'log': report['batch_log'], 'ok': report['batch_ok']}), flush=True)

            # One translation against the stalling endpoint with a 3 s activity timeout
            report['stall_settings'] = run_py(port, STALL_SETTINGS_PY % {'endpoint': 'http://127.0.0.1:%d' % stall.port,
                                                                         'timeout': '3.0'}, 90)
            started = time.time()
            events = run_tool_call(port, 'translate-focused-blueprint',
                                   {'provider': 'ollama', 'model': 'n2c-stall', 'language': 'cpp'}, 91,
                                   meta={'progressToken': 'card0149-stall'}, timeout=120)
            seconds = round(time.time() - started, 1)
            final = [d for e, d in events if e == 'response']
            message = json.loads(final[-1]) if final else {}
            text = tool_text(message)
            try:
                error = json.loads(text)
            except ValueError:
                error = {'raw': text[:600]}
            report['stall'] = {'seconds': seconds, 'is_error': bool((message.get('result') or {}).get('isError')),
                               'error_kind': error.get('error_kind'), 'retryable': error.get('retryable'),
                               'message': str(error.get('message', ''))[:300],
                               'stub_connections': len(stall.connections), 'stub_bytes': stall.bytes_received}
            report['stall_ok'] = bool(report['stall']['is_error'] and error.get('error_kind') == 'Timeout'
                                      and seconds < 30 and stall.connections)
            print('stall', json.dumps(report['stall']), flush=True)
            prev = (report['stall_settings'] or {}).get('previous') or {}
            run_py(port, STALL_SETTINGS_PY % {'endpoint': prev.get('endpoint', 'http://localhost:11434'),
                                              'timeout': prev.get('timeout', 300.0)}, 92)
            # The tags live in Saved/NodeToCode/BlueprintGraphState.json, which the session restores

    stall.close()
    report['config_restored'] = session.restore_ok
    report['leaks'] = [list(x) for x in find_leaks(keys, [log_path, os.path.join(project_dir, 'Saved', 'Logs'),
                                                          os.path.join(project_dir, 'Saved', 'NodeToCode')])]
    report['ok'] = bool(report.get('batch_ok') and report.get('stall_ok') and not report['leaks'])
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print(json.dumps({'ok': report['ok'], 'batch_ok': report.get('batch_ok'), 'stall_ok': report.get('stall_ok'),
                      'leaks': report['leaks'], 'config_restored': report['config_restored'], 'report': report_path}))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
