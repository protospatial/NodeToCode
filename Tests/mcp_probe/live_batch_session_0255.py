"""Card 0255 live check: Batch Translate as a session of UN2CTranslationSubsystem, against a real provider.

Stdlib only; run with `python -I`:

    python -I live_batch_session_0255.py --engine <UE_5.8> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8 \
        --provider anthropic --model claude-haiku-5-5

One headless editor (-N2CMcp, run-python allowed) with the provider and model set for the session (the key comes from
the repo's .env into the editor's environment only, never printed). Five in-memory probe Blueprints (probe_blueprint
SETUP_PY, never saved) are tagged: two for a batch that runs to the end, three for one cancelled while its second item
is in flight. Each batch starts with UN2CTranslationSubsystem.StartBatchTranslation, the path the window's Batch
Translate takes.
- start refusal: a batch of a Blueprint that does not exist does not start, says NO_VALID_ITEMS and leaves the
  subsystem idle
- full batch: the session ends Succeeded with two items translated, the subsystem idle, the run folder written
- cancel: as soon as the orchestrator reports item 1 in progress, CancelSession; in the same game-thread step the
  session is Cancelled, the subsystem idle, 1 succeeded and 2 skipped (bCancelled), and the log shows no third request
Report: logs/live_batch_session_0255_<label>_<provider>.json.
"""
import argparse
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ASSET_DIR, STORE_ID, call_tool, stored_key_present, tool_text  # noqa: E402
from probe_blueprint import SETUP_PY  # noqa: E402

SETTINGS_SECTION = '[/Script/NodeToCode.N2CSettings]'
PROVIDERS = {'anthropic': ('Anthropic', 'AnthropicModelId', 'ANTHROPIC_API_KEY'),
             'gemini': ('Gemini', 'GeminiModelId', 'GEMINI_API_KEY'),
             'deepseek': ('DeepSeek', 'DeepSeekModelId', 'DEEPSEEK_API_KEY')}

TAG_PY = r"""
import json, unreal
res = json.loads(unreal.N2CPythonBridge.tag_focused_graph("%(tag)s", "N2CGate", "card 0255 live check"))
result = res.get("data") or {"error": res.get("error")}
"""

# Shared helpers, defined once per call (run-python calls do not share globals; builtins hold the session id)
HELPERS = r"""
import builtins, os, unreal
sub = unreal.get_editor_subsystem(unreal.N2CTranslationSubsystem)
orch = unreal.N2CBatchTranslationBlueprintLibrary.get_batch_orchestrator()

def valid(guid):
    # The zero Guid is invalid (export_text gives its 32 hex digits)
    return guid is not None and guid.export_text().strip("0-{}") != ""

def info_of(guid):
    r = sub.get_session_info(guid)
    ok, info = (r[0], r[1]) if isinstance(r, tuple) and len(r) == 2 else (r is not None, r)
    if not ok or info is None:
        return None
    return {"state": info.state.name, "items": info.item_count, "error": str(info.error_message),
            "run_folder": str(info.run_folder), "run_folder_exists": os.path.isdir(str(info.run_folder)) if str(info.run_folder) else False}

def result_of(guid):
    r = sub.get_batch_session_result(guid)
    ok, res = (r[0], r[1]) if isinstance(r, tuple) and len(r) == 2 else (r is not None, r)
    if not ok or res is None:
        return None
    return {"total": res.total_count, "success": res.success_count, "failure": res.failure_count,
            "skipped": res.skipped_count, "cancelled": res.cancelled}
"""

REFUSE_PY = HELPERS + r"""
missing = unreal.N2CTagInfo()
missing.set_editor_property("graph_name", "EventGraph")
missing.set_editor_property("graph_guid", "6F9619FF-8B86-D011-B42D-00C04FC964FF")
missing.set_editor_property("blueprint_path", "/Game/N2CTestMissing/BP_N2C0255Missing.BP_N2C0255Missing")
r = sub.start_batch_translation([missing])
guid, error = (r[0], r[1]) if isinstance(r, tuple) else (r, "")
result = {"started": valid(guid), "error": str(error), "busy": sub.is_busy()}
"""

START_PY = HELPERS + r"""
graphs = unreal.N2CTagBlueprintLibrary.get_graphs_with_tag("%(tag)s", "")
r = sub.start_batch_translation(graphs)
guid, error = (r[0], r[1]) if isinstance(r, tuple) else (r, "")
builtins._n2c0255_batch = guid
result = {"graphs": len(graphs), "started": valid(guid), "error": str(error), "busy": sub.is_busy(),
          "info": info_of(guid) if valid(guid) else None}
"""

POLL_PY = HELPERS + r"""
guid = builtins._n2c0255_batch
result = {"info": info_of(guid), "busy": sub.is_busy(), "index": orch.get_current_item_index(),
          "batch_result": result_of(guid)}
"""

# Cancels as soon as item 1 is in flight, and reads the outcome in the same game-thread step
CANCEL_WHEN_SECOND_PY = HELPERS + r"""
import time
guid = builtins._n2c0255_batch
index = orch.get_current_item_index()
info = info_of(guid)
result = {"index": index, "info": info}
if info and info["state"] == "SENDING" and index == 1:
    t0 = time.perf_counter()
    result["cancel_returned"] = sub.cancel_session(guid)
    result["cancel_seconds"] = round(time.perf_counter() - t0, 4)
    result["info_after"] = info_of(guid)
    result["busy_after"] = sub.is_busy()
    result["batch_result"] = result_of(guid)
    result["cancelled"] = True
"""


def run_py(port, code, rid):
    status, response = call_tool(port, 'run-python', {'code': code}, rid)
    text = tool_text(response)
    try:
        answer = json.loads(text or '{}')
        if answer.get('data') is None and answer.get('error'):
            print('run-python %d failed: %s' % (rid, str(answer.get('error'))[:400]), flush=True)
        return answer.get('data')
    except ValueError:
        return {'status': status, 'text': text[:600]}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8')
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDERS))
    ap.add_argument('--model', default='claude-haiku-5-5')
    ap.add_argument('--port', type=int, default=27255)
    args = ap.parse_args()

    provider_name, model_key, env_key = PROVIDERS[args.provider]
    keys = load_provider_keys(only=[env_key])
    if env_key not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % env_key}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % provider_name}))
        return 2

    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    stem = 'live_batch_session_0255_%s_%s' % (args.label, args.provider)
    log_path = os.path.join(logs, stem + '_editor.log')
    report_path = os.path.join(logs, stem + '.json')
    project_dir = os.path.dirname(os.path.abspath(args.project))
    port = free_port(args.port)
    stamp = time.strftime('%H%M%S')
    tag_ok, tag_cancel = 'N2C0255Ok%s' % stamp, 'N2C0255Cancel%s' % stamp
    report = {'label': args.label, 'date': time.strftime('%Y-%m-%d'), 'provider': provider_name, 'model': args.model}

    session = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                            extra_args=['-LogCmds=LogNodeToCode VeryVerbose', *BLUEPRINT_EDITOR_ARGS])
    with session as s:
        s._patch('EditorPerProjectUserSettings.ini', '[/Script/NodeToCode.N2CUserSettings]', 'Provider', provider_name)
        s._patch('NodeToCode.ini', SETTINGS_SECTION, model_key, args.model)
        for state_file in ('BlueprintGraphState.json', 'BlueprintGraphState.json.bak'):
            s.guard_file(os.path.join(project_dir, 'Saved', 'NodeToCode', state_file))
        report['started'] = s.start()
        if report['started']:
            rid = 10
            report['setup'] = []
            for tag, letters in ((tag_ok, 'AB'), (tag_cancel, 'CDE')):
                for letter in letters:
                    rid += 1
                    name = 'BP_N2C0255%s_%s' % (letter, stamp)
                    setup = run_py(port, SETUP_PY % {'dir': ASSET_DIR, 'name': name}, rid)
                    rid += 1
                    tagged = run_py(port, TAG_PY % {'tag': tag}, rid)
                    report['setup'].append({'name': name, 'tag': tag, 'setup_ok': bool(setup and setup.get('print_string_live')),
                                            'tagged': bool(tagged and not tagged.get('error'))})
            print('setup', json.dumps(report['setup']), flush=True)

            # A batch that cannot start
            refuse = run_py(port, REFUSE_PY, 50)
            report['refuse'] = refuse
            report['refuse_ok'] = bool(refuse and refuse.get('started') is False
                                       and str(refuse.get('error', '')).startswith('NO_VALID_ITEMS') and refuse.get('busy') is False)
            print('refuse', json.dumps(refuse), flush=True)

            # A batch that runs to the end
            start = run_py(port, START_PY % {'tag': tag_ok}, 60)
            report['full_start'] = start
            outcome, rid, deadline = None, 60, time.time() + 240
            while start and start.get('started') and time.time() < deadline:
                rid += 1
                r = run_py(port, POLL_PY, rid)
                if not r or not r.get('info') or r['info']['state'] in ('SUCCEEDED', 'FAILED', 'CANCELLED'):
                    outcome = r
                    break
                time.sleep(1.0)
            report['full_outcome'] = outcome
            info = (outcome or {}).get('info') or {}
            report['full_ok'] = bool(info.get('state') == 'SUCCEEDED' and info.get('items') == 2 and info.get('run_folder_exists')
                                     and (outcome or {}).get('busy') is False
                                     and ((outcome or {}).get('batch_result') or {}).get('success') == 2)
            print('full', json.dumps({'outcome': outcome, 'ok': report['full_ok']}), flush=True)
            sends_before_cancel = len(re.findall(r'Sending request to EN2CLLMProvider::', session.log_text() or ''))

            # A batch cancelled while its second item is in flight
            start = run_py(port, START_PY % {'tag': tag_cancel}, 200)
            report['cancel_start'] = start
            outcome, rid, deadline = None, 200, time.time() + 180
            while start and start.get('started') and time.time() < deadline:
                rid += 1
                r = run_py(port, CANCEL_WHEN_SECOND_PY, rid)
                if not r or r.get('cancelled') or (r.get('info') or {}).get('state') != 'SENDING':
                    outcome = r
                    break
                time.sleep(0.15)
            report['cancel_outcome'] = outcome
            time.sleep(5.0)  # anything the cancelled request or a third item would log
            text = session.log_text() or ''
            report['cancel_log'] = {
                'requests_in_cancelled_batch': len(re.findall(r'Sending request to EN2CLLMProvider::', text)) - sends_before_cancel,
                'cancelled_in_flight': len(re.findall(r'Cancelled the in-flight translation of', text)),
                'batch_complete_lines': re.findall(r'Batch translation complete\. [^\r\n]*', text)[-2:],
            }
            o = outcome or {}
            report['cancel_ok'] = bool(o.get('cancelled') and o.get('cancel_returned') and o.get('busy_after') is False
                                       and (o.get('info_after') or {}).get('state') == 'CANCELLED'
                                       and o.get('batch_result') == {'total': 3, 'success': 1, 'failure': 0, 'skipped': 2, 'cancelled': True}
                                       and o.get('cancel_seconds', 9) < 1.0
                                       and report['cancel_log']['requests_in_cancelled_batch'] == 2
                                       and report['cancel_log']['cancelled_in_flight'] >= 1)
            print('cancel', json.dumps({'outcome': outcome, 'log': report['cancel_log'], 'ok': report['cancel_ok']}), flush=True)

    report['config_restored'] = session.restore_ok
    report['leaks'] = [list(x) for x in find_leaks(keys, [log_path, os.path.join(project_dir, 'Saved', 'Logs'),
                                                          os.path.join(project_dir, 'Saved', 'NodeToCode')])]
    report['ok'] = bool(report.get('refuse_ok') and report.get('full_ok') and report.get('cancel_ok') and not report['leaks'])
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print(json.dumps({'ok': report['ok'], 'refuse_ok': report.get('refuse_ok'), 'full_ok': report.get('full_ok'),
                      'cancel_ok': report.get('cancel_ok'), 'leaks': report['leaks'],
                      'config_restored': report['config_restored'], 'report': report_path}))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
