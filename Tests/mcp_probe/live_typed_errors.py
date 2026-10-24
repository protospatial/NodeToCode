"""Live provider errors reach the MCP result, the log and batch items as typed errors (card 0091).

Stdlib only; run with `python -I`:

    python -I live_typed_errors.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

Forces real provider errors cheaply, with no successful (billed) translation:
- Anthropic, Gemini and DeepSeek get a made-up key (never a real one) in the editor's environment, so the provider
  answers 401 or 400 "API key not valid";
- OpenAI gets the real key from the repo's .env (editor environment only) and a model id that does not exist, so it
  answers 404 model_not_found.
For each case it runs translate-focused-blueprint over MCP on an in-memory probe Blueprint and checks the CallToolResult
is an error whose text is NodeToCode error JSON with the expected error_kind and http_status and the provider's own
message, and that the editor log has the provider's status and message and no "Failed to parse LLM response from
provider". Before those it runs a one-graph batch translation (Anthropic, made-up key) and checks BatchSummary.json
records errorKind, httpStatus and the provider message instead of "Translation failed". Key values are never printed;
the editor log, Saved/Logs, the plugin log, new translation folders and the report are scanned for the real key.
Report: logs/live_typed_errors_<label>.json.
"""
import argparse
import glob
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ASSET_DIR, SETUP_PY, STORE_ID, run_tool_call, stored_key_present, tool_text  # noqa: E402
from live_provider_matrix import CONNECT_PY, run_py  # noqa: E402

# Made-up values, not keys: the provider rejects them
FAKE_KEY = 'n2c-made-up-test-key-0091-not-a-real-key'

# Current registry models only (owner instruction 2026-10-23); the OpenAI case uses an id that does not exist on purpose
CASES = [
    {'id': 'anthropic/bad-key', 'provider': 'anthropic', 'model': 'claude-haiku-5-5',
     'env': {'ANTHROPIC_API_KEY': FAKE_KEY}, 'kind': 'Auth', 'status': 401},
    {'id': 'gemini/bad-key', 'provider': 'gemini', 'model': 'gemini-3.5-flash-lite',
     'env': {'GEMINI_API_KEY': FAKE_KEY}, 'kind': 'Auth', 'status': 400},
    {'id': 'deepseek/bad-key', 'provider': 'deepseek', 'model': 'deepseek-flash',
     'env': {'DEEPSEEK_API_KEY': FAKE_KEY}, 'kind': 'Auth', 'status': 401},
    {'id': 'openai/unknown-model', 'provider': 'openai', 'model': 'gpt-6-n2c-0091-does-not-exist',
     'env': 'real', 'kind': 'InvalidRequest', 'status': 404},
]

# Runs in the editor: points the settings at a provider for the batch and returns the previous value
PROVIDER_PY = r'''
import unreal
settings = unreal.get_default_object(unreal.N2CSettings)
previous = settings.get_editor_property("provider").name
target = getattr(unreal.N2CLLMProvider, %(member)r)
settings.set_editor_property("provider", target)
result = {"previous": previous, "now": settings.get_editor_property("provider").name}
'''

# Runs in the editor: tags the focused graph and starts a one-item batch
BATCH_START_PY = r'''
import unreal
info, ok, err = unreal.N2CTagBlueprintLibrary.tag_focused_blueprint_graph("N2CTypedErrors0091", "Default", "card 0091 probe")
started = None
if ok:
    started = unreal.N2CBatchTranslationBlueprintLibrary.start_batch_translation([info])
result = {"tagged": ok, "tag_error": err, "graph_guid": info.get_editor_property("graph_guid"),
          "started": str(started)}
'''

BATCH_POLL_PY = r'''
import unreal
result = {"in_progress": unreal.N2CBatchTranslationBlueprintLibrary.is_batch_translation_in_progress()}
'''

UNTAG_PY = r'''
import unreal
result = {"removed": unreal.N2CTagBlueprintLibrary.remove_tag(%(guid)r, "N2CTypedErrors0091", "Default")}
'''


def check_case(port, c, rid, timeout):
    rec = {'id': c['id'], 'provider': c['provider'], 'model': c['model'], 'expect': {'kind': c['kind'], 'status': c['status']}}
    events = run_tool_call(port, 'translate-focused-blueprint',
                           {'provider': c['provider'], 'model': c['model'], 'language': 'cpp'}, rid,
                           meta={'progressToken': 'typed-errors-%d' % rid}, timeout=timeout)
    final = [d for e, d in events if e == 'response']
    if not final:
        rec['ok'] = False
        rec['reason'] = 'no response'
        return rec
    message = json.loads(final[-1])
    result = message.get('result') or {}
    rec['is_error'] = bool(result.get('isError'))
    text = tool_text(message)
    rec['text'] = text[:1200]
    try:
        error = json.loads(text)
    except ValueError:
        error = {}
    rec['error_json'] = isinstance(error, dict) and 'error' in error
    rec['error_kind'] = error.get('error_kind')
    rec['http_status'] = error.get('http_status')
    rec['retryable'] = error.get('retryable')
    rec['provider_message'] = error.get('message')
    rec['ok'] = bool(rec['is_error'] and rec['error_json'] and rec['error_kind'] == c['kind']
                     and rec['http_status'] == c['status'] and rec['provider_message']
                     and 'Failed to parse' not in text)
    return rec


def latest_batch_summary(translations_dir, before):
    folders = sorted(set(glob.glob(os.path.join(translations_dir, 'BatchTranslation_*'))) - before)
    for folder in reversed(folders):
        path = os.path.join(folder, 'BatchSummary.json')
        if os.path.isfile(path):
            with open(path, encoding='utf-8-sig') as f:
                return folder, json.load(f)
    return None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8')
    ap.add_argument('--port', type=int, default=27191)
    ap.add_argument('--timeout', type=int, default=180)
    args = ap.parse_args()

    real = load_provider_keys(only=['OPENAI_API_KEY'])
    if 'OPENAI_API_KEY' not in real:
        print(json.dumps({'ok': False, 'reason': 'OPENAI_API_KEY not in .env'}))
        return 2
    saved = [p for p in STORE_ID if stored_key_present(STORE_ID[p])]
    if saved:
        print(json.dumps({'ok': False, 'reason': 'keys saved in the per-user secrets file for %s' % saved}))
        return 2
    env = dict(real)
    for c in CASES:
        if isinstance(c['env'], dict):
            env.update(c['env'])

    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    log_path = os.path.join(logs, 'live_typed_errors_%s_editor.log' % args.label)
    report_path = os.path.join(logs, 'live_typed_errors_%s.json' % args.label)
    project_dir = os.path.dirname(os.path.abspath(args.project))
    translations_dir = os.path.join(project_dir, 'Saved', 'NodeToCode', 'Translations')
    before_dirs = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    before_batches = set(glob.glob(os.path.join(translations_dir, 'BatchTranslation_*')))
    port = free_port(args.port)
    report = {'label': args.label, 'date': time.strftime('%Y-%m-%d'), 'port': port, 'cases': []}

    session = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=env,
                            extra_args=['-RenderOffScreen'])
    with session as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CTypedErrors_%s' % time.strftime('%H%M%S')
            report['setup'] = run_py(port, SETUP_PY % {'dir': ASSET_DIR, 'name': name}, 1)
            report['wiring'] = run_py(port, CONNECT_PY, 2)

            # Batch first: the LLM module initializes from the settings' provider on the first request
            provider = run_py(port, PROVIDER_PY % {'member': 'ANTHROPIC'}, 3)
            report['batch_provider'] = provider
            start = run_py(port, BATCH_START_PY, 4)
            report['batch_start'] = start
            deadline = time.time() + args.timeout
            rid = 5
            while time.time() < deadline:
                time.sleep(1.0)
                poll = run_py(port, BATCH_POLL_PY, rid)
                rid += 1
                if isinstance(poll, dict) and not poll.get('in_progress'):
                    break
            folder, summary = latest_batch_summary(translations_dir, before_batches)
            batch = {'folder': folder}
            items = (summary or {}).get('items') or []
            if items:
                item = items[0]
                batch.update({k: item.get(k) for k in ('status', 'error', 'errorKind', 'httpStatus', 'retryable', 'providerMessage')})
            batch['ok'] = bool(items and batch.get('errorKind') == 'Auth' and batch.get('httpStatus') == 401
                               and batch.get('providerMessage') and batch.get('error') != 'Translation failed')
            report['batch'] = batch
            if isinstance(start, dict) and start.get('graph_guid'):
                report['untag'] = run_py(port, UNTAG_PY % {'guid': start['graph_guid']}, rid)
            if isinstance(provider, dict) and provider.get('previous'):
                report['batch_provider_restored'] = run_py(port, PROVIDER_PY % {'member': provider['previous']}, rid + 1)
            print('batch', 'PASS' if batch['ok'] else 'FAIL', json.dumps(batch)[:400], flush=True)

            for i, c in enumerate(CASES):
                rec = check_case(port, c, 100 + i, args.timeout)
                report['cases'].append(rec)
                print('%-24s %s  kind=%s status=%s  %s' % (c['id'], 'PASS' if rec['ok'] else 'FAIL', rec.get('error_kind'),
                                                         rec.get('http_status'), (rec.get('provider_message') or '')[:120]),
                      flush=True)

    log_text = session.log_text()
    report['log'] = {
        'failed_to_parse_from_provider': log_text.count('Failed to parse LLM response from provider'),
        'translation_failed_lines': [l.strip()[:400] for l in log_text.splitlines() if 'Translation failed' in l][:12],
        'http_error_lines': [l.strip()[:300] for l in log_text.splitlines() if 'HttpHandler' in l and 'HTTP 4' in l][:8],
    }
    new_dirs = [os.path.join(translations_dir, d) for d in sorted((set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()) - before_dirs)]
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    scan = [log_path, report_path, os.path.join(project_dir, 'Saved', 'Logs'),
            os.path.join(project_dir, 'Saved', 'NodeToCode.log')] + new_dirs
    report['leaks'] = [{'path': p, 'variable': n} for p, n in find_leaks(real, scan)]
    report['ok'] = bool(report.get('started') and report.get('batch', {}).get('ok')
                        and report['cases'] and all(c['ok'] for c in report['cases'])
                        and report['log']['failed_to_parse_from_provider'] == 0 and not report['leaks'])
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'ok': report['ok'], 'leaks': report['leaks'], 'log': report['log']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
