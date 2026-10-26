"""export-n2cjson-batch and translate-batch by asset path with no Blueprint editor open, live through claude -p, and a
batch cancelled midway (card 0169).

Stdlib only; run with `python -I` (set N2C_PORT_OFFSET first when other agents run probes):

    python -I live_batch_0169.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

One headless editor (-N2CMcp, run-python allowed, -RenderOffScreen), with ANTHROPIC_API_KEY from the repo's .env in
the editor's environment only (never printed). Three scratch Actor Blueprints under a fresh /Game/N2CTest/Batch0169_<time>
folder (probe_blueprint SETUP_PY: BeginPlay wired to a Print String), saved, every editor closed.

1. claude -p, with only mcp__nodetocode__* allowed, exports the three Blueprints with export-n2cjson-batch (whole
   Blueprints, outDir Live0169_<time>). Checks: one call, not an error, a file per graph with nodes (at least the three
   event graphs), every file parses as N2C JSON 2.0.0, no editor opened, result marked untrusted.
2. claude -p translates the three event graphs with translate-batch (anthropic, claude-haiku-5-5, cpp). Checks: three
   completed, three distinct output folders with a .cpp each, and the translation viewer's loader
   (UN2CTagBlueprintLibrary::LoadTranslation, through run-python) reads each graph's translation back.
3. This script starts a second translate-batch on the same three graphs with a progress token (streamed on 5.8, held
   on 5.6/5.7), and sends notifications/cancelled as soon as the second graph starts (on 5.6/5.7: after the first
   request has had time to finish). Checks: the call ends without a response, BatchSummary.json of that batch shows
   the remaining graphs skipped, no third request is sent (log), and a new call works afterwards.
4. Cleanup: the scratch folder, the export folder and the batch folders this run made are deleted; a leak scan of
   the editor log, the host's Saved/NodeToCode and this run's logs finds no key.
Report: logs/live_batch_0169_<label>.json.
"""
import argparse
import glob
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ToolCallStream, post  # noqa: E402
from live_graph_target_0152 import Probe, run_cli  # noqa: E402
from probe_blueprint import SETUP_PY  # noqa: E402

NAMES = ('BP_N2CBatch0169A', 'BP_N2CBatch0169B', 'BP_N2CBatch0169C')
MODEL = 'claude-haiku-5-5'

SAVE_AND_CLOSE_PY = r'''
import unreal
folder = "%(folder)s"
subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
saved = []
for name in %(names)r:
    bp = unreal.load_asset(folder + "/" + name)
    if bp:
        subsystem.close_all_editors_for_asset(bp)
        saved.append(unreal.EditorAssetLibrary.save_loaded_asset(bp, False))
result = {"saved": saved}
'''

LOAD_TRANSLATIONS_PY = r'''
import unreal
loaded = {}
for guid in %(guids)r:
    out = unreal.N2CTagBlueprintLibrary.load_translation(guid)
    translation = out[1] if isinstance(out, tuple) else out
    if isinstance(out, tuple) and not out[0]:
        translation = None
    code = translation.get_editor_property("code") if translation else None
    impl = code.get_editor_property("graph_implementation") if code else ""
    loaded[guid] = len(impl or "")
result = {"loaded": loaded}
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
subsystem = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
for name in %(names)r:
    bp = unreal.load_asset(folder + "/" + name)
    if bp:
        subsystem.close_all_editors_for_asset(bp)
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

EXPORT_PROMPT = (
    'Using only the nodetocode MCP tools (not run-python, and do not open any editor), export every graph of these '
    'three Blueprints as N2C JSON files in ONE export-n2cjson-batch call, with targets [%(targets)s] (blueprintPath '
    'only, no graph) and outDir "%(outdir)s". Do nothing else. Reply with only a JSON object {"fileCount": <fileCount '
    'from the result>, "graphs": [<graphName of each file>]}.')

TRANSLATE_PROMPT = (
    'Using only the nodetocode MCP tools (not run-python, and do not open any editor), translate the EventGraph of these '
    'three Blueprints to C++ in ONE translate-batch call: targets [%(targets)s] (each with graphName "EventGraph"), '
    'provider "anthropic", model "%(model)s", language "cpp". Do nothing else. Reply with only a JSON object '
    '{"succeeded": <succeeded from the result>, "folders": [<outputDirectory of each item>]}.')


def open_editor_count(probe):
    status, text, err = probe.tool('get-open-blueprint-editors', {})
    try:
        return -1 if err else int(json.loads(text).get('count'))
    except (ValueError, TypeError, AttributeError):
        return -1


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def batch_folders(saved_dir):
    return set(glob.glob(os.path.join(saved_dir, 'NodeToCode', 'Translations', 'BatchTranslation_*')))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27169)
    ap.add_argument('--cli-model', default='sonnet')
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    stem = 'live_batch_0169_%s' % args.label
    log_path = os.path.join(log_dir, stem + '_editor.log')
    report_path = os.path.join(log_dir, stem + '.json')
    mcp_config_path = os.path.join(log_dir, stem + '_mcp.json')
    saved_dir = os.path.join(os.path.dirname(args.project), 'Saved')

    keys = load_provider_keys(only=['ANTHROPIC_API_KEY'])
    port = free_port(args.port)
    stamp = time.strftime('%H%M%S')
    folder = '/Game/N2CTest/Batch0169_%s' % stamp
    outdir = 'Live0169_%s' % stamp
    paths = ['%s/%s.%s' % (folder, n, n) for n in NAMES]
    report = {'label': args.label, 'port': port, 'folder': folder, 'key_present': 'ANTHROPIC_API_KEY' in keys}
    checks = {'ANTHROPIC_API_KEY in .env': 'ANTHROPIC_API_KEY' in keys}
    before = batch_folders(saved_dir)

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True,
                       extra_args=BLUEPRINT_EDITOR_ARGS, extra_env=keys) as s:
        report['started'] = s.start()
        checks['editor_started'] = bool(report['started'])
        if report['started']:
            p = Probe(port)
            setup = {}
            for name in NAMES:
                data, text = p.python(SETUP_PY % {'dir': folder, 'name': name})
                setup[name] = {k: data.get(k) for k in ('created', 'connected', 'compiled', 'print_string_live')} if data else text
            data, text = p.python(SAVE_AND_CLOSE_PY % {'folder': folder, 'names': list(NAMES)})
            report['setup'] = {'blueprints': setup, 'save': data or text}
            checks['setup: three wired Blueprints'] = all(isinstance(v, dict) and v.get('print_string_live') for v in setup.values())
            checks['setup: no Blueprint editor open'] = open_editor_count(p) == 0
            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            targets = ', '.join('{"blueprintPath": "%s"}' % path for path in paths)

            # 1. Export through claude -p
            run = run_cli(EXPORT_PROMPT % {'targets': targets, 'outdir': outdir}, mcp_config_path, args.cli_model,
                          args.timeout, os.path.join(log_dir, stem + '_export_cli.jsonl'))
            exports = [c for c in run['calls'] if c['tool'] == 'export-n2cjson-batch']
            report['export_cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                                    'answer': run['answer'], 'seconds': run['seconds']}
            body = strip_mark(exports[-1]['body']) if exports else {}
            files = body.get('files') or []
            report['export'] = {k: body.get(k) for k in ('outDir', 'fileCount', 'failureCount', 'totalBytes')}
            report['export']['graphs'] = [(f.get('blueprintPath'), f.get('graphName'), f.get('nodeCount')) for f in files]
            parsed = []
            for f in files:
                try:
                    with open(f.get('path') or '', encoding='utf-8') as fh:
                        doc = json.load(fh)
                    parsed.append(doc.get('version') == '2.0.0' and bool(doc.get('graphs')))
                except (OSError, ValueError):
                    parsed.append(False)
            checks['export: one export-n2cjson-batch call, not an error'] = len(exports) == 1 and not exports[0]['error']
            checks['export: by the scratch blueprintPaths'] = bool(exports) and \
                sorted(t.get('blueprintPath') for t in exports[0]['input'].get('targets') or []) == sorted(paths)
            checks['export: the three event graphs and no failure'] = body.get('failureCount') == 0 and \
                sum(1 for f in files if f.get('graphName') == 'EventGraph') == 3
            checks['export: every file parses as N2C JSON 2.0.0'] = bool(parsed) and all(parsed)
            checks['export: files under Saved/NodeToCode/Exports/' + outdir] = bool(files) and all(
                os.path.normcase(os.path.join('NodeToCode', 'Exports', outdir)) in os.path.normcase(f.get('path') or '') for f in files)
            checks['export: result marked untrusted first'] = bool(exports) and exports[0]['text'].lstrip().startswith('{"untrusted_content"')
            checks['export: answer matches'] = isinstance(run['answer'], dict) and run['answer'].get('fileCount') == len(files)
            checks['export: no editor opened'] = open_editor_count(p) == 0 and not any(
                c['tool'] in ('open-blueprint-asset', 'open-blueprint-function') for c in run['calls'])

            # 2. Translate through claude -p
            run = run_cli(TRANSLATE_PROMPT % {'targets': targets, 'model': MODEL}, mcp_config_path, args.cli_model,
                          args.timeout, os.path.join(log_dir, stem + '_translate_cli.jsonl'))
            translates = [c for c in run['calls'] if c['tool'] == 'translate-batch']
            report['translate_cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                                       'answer': run['answer'], 'seconds': run['seconds']}
            body = strip_mark(translates[-1]['body']) if translates else {}
            items = body.get('items') or []
            report['translate'] = {k: body.get(k) for k in ('provider', 'model', 'language', 'total', 'succeeded', 'failed',
                                                            'skipped', 'durationSeconds', 'usage', 'codeInline', 'batchOutputPath')}
            report['translate']['items'] = [{k: i.get(k) for k in ('blueprintPath', 'graphName', 'status', 'outputDirectory', 'files', 'error')}
                                            for i in items]
            folders = [i.get('outputDirectory') for i in items if i.get('status') == 'completed']
            checks['translate: one translate-batch call, not an error'] = len(translates) == 1 and not translates[0]['error']
            checks['translate: anthropic %s cpp' % MODEL] = body.get('provider') == 'anthropic' and body.get('model') == MODEL \
                and body.get('language') == 'cpp'
            checks['translate: three completed in three folders'] = body.get('succeeded') == 3 and len(set(folders)) == 3
            checks['translate: a .cpp in each folder'] = len(folders) == 3 and all(
                glob.glob(os.path.join(d, '*.cpp')) for d in folders)
            checks['translate: code inline and marked untrusted'] = body.get('codeInline') is True and all(
                (i.get('graphs') or [{}])[0].get('code', {}).get('graphImplementation') for i in items) and \
                bool(translates) and translates[0]['text'].lstrip().startswith('{"untrusted_content"')
            guids = [i.get('graphGuid') for i in items if i.get('status') == 'completed']
            data, text = p.python(LOAD_TRANSLATIONS_PY % {'guids': guids})
            report['loaded'] = data or text
            loaded = (data or {}).get('loaded') or {}
            checks['translate: the viewer loader reads each translation back'] = len(guids) == 3 and all(
                (loaded.get(g) or 0) > 0 for g in guids)
            checks['translate: no editor opened'] = open_editor_count(p) == 0

            # 3. A second batch, cancelled once its second graph is under way
            stream = ToolCallStream(port, 'translate-batch', {
                'targets': [{'blueprintPath': path, 'graphName': 'EventGraph'} for path in paths],
                'provider': 'anthropic', 'model': MODEL, 'language': 'cpp'}, 9169, meta={'progressToken': 'n2c-0169-cancel'})
            deadline = time.time() + 240
            second = False
            while time.time() < deadline and stream.closed_at is None:
                second = any('(2 of 3)' in d for _, e, d in stream.events if e == 'progress')
                if second:
                    break
                time.sleep(0.2)
            if not second and stream.closed_at is None and not stream.events:
                # Held response (UE 5.6/5.7): no progress arrives; give the first request time to finish
                time.sleep(12)
            sent_at = round(time.time() - stream.t0, 2)
            status, _ = post(port, {'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                                    'params': {'requestId': 9169, 'reason': 'live 0169 cancel'}})
            stream.wait(30)
            report['cancel'] = {'second_started_seen': second, 'cancel_sent_at': sent_at, 'cancel_status': status,
                                'closed_at': stream.closed_at, 'content_type': stream.content_type,
                                'events': [(t, e, d[:160]) for t, e, d in stream.events]}
            time.sleep(3)
            new = sorted(batch_folders(saved_dir) - before, key=os.path.getmtime)
            summary = {}
            if new:
                try:
                    with open(os.path.join(new[-1], 'BatchSummary.json'), encoding='utf-8') as f:
                        summary = json.load(f)
                except (OSError, ValueError):
                    summary = {}
            statuses = [i.get('status') for i in summary.get('items') or []]
            report['cancel']['summary_statuses'] = statuses
            report['cancel']['summary_folder'] = new[-1] if new else None
            log = s.log_text()
            sends = log.count('Processing item 3/3')
            checks['cancel: notifications/cancelled accepted (202)'] = status == 202
            checks['cancel: the call ended without a response'] = stream.closed_at is not None and not stream.responses()
            checks['cancel: one done, the rest skipped'] = len(statuses) == 3 and statuses[0] == 'EN2CBatchItemStatus::Completed' \
                and statuses[1:] == ['EN2CBatchItemStatus::Skipped'] * 2
            checks['cancel: the in-flight request was cancelled'] = 'Cancelled the in-flight translation of EventGraph' in log
            # Only the first (claude -p) batch reached its third item; the cancelled one never did
            checks['cancel: no third request after the cancel'] = sends == 1
            report['cancel']['item3_sends'] = sends
            status, text, err = p.tool('export-n2cjson-batch', {'targets': [{'blueprintPath': paths[0], 'graphName': 'EventGraph'}],
                                                                'outDir': outdir})
            checks['cancel: the server still answers'] = not err

            # 4. Cleanup
            data, text = p.python(CLEANUP_PY % {'folder': folder, 'names': list(NAMES)})
            report['cleanup'] = data or text
            checks['scratch folder deleted'] = (data or {}).get('exists_after') is False
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    export_root = os.path.join(saved_dir, 'NodeToCode', 'Exports', outdir)
    made = sorted(batch_folders(saved_dir) - before)
    leaks = find_leaks(keys, [log_path, os.path.join(saved_dir, 'NodeToCode'), os.path.join(saved_dir, 'Logs')] +
                       glob.glob(os.path.join(log_dir, stem + '*')))
    report['leaks'] = [{'path': path, 'variable': name} for path, name in leaks]
    checks['no key in the editor log, Saved/NodeToCode or this run\'s logs'] = not leaks
    shutil.rmtree(export_root, ignore_errors=True)
    for path in made:
        shutil.rmtree(path, ignore_errors=True)
    report['removed'] = {'exports': export_root, 'batches': made}
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
