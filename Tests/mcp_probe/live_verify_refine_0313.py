"""Card 0313 live check: Verify and Refine on a real provider, through Batch Translate's path.

Stdlib only; run with `python -I`:

    python -I live_verify_refine_0313.py --engine <UE_5.8> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8 \
        --provider anthropic --model claude-haiku-5-5 --language python

One headless editor (-N2CMcp, run-python allowed) with the provider, model and target language set for the session and
Verify and Refine on for that language (VerifyLanguages, MaxRefineTurns 2; the key comes from the repo's .env into the
editor's environment only, never printed). One in-memory probe Blueprint (probe_blueprint SETUP_PY, never saved) is
tagged and translated with UN2CTranslationSubsystem.StartBatchTranslation. With --language cpp the C++ compile check
(n2c.Verify.CppCompile) is switched on for the session, so the model's code is compiled in the scratch module and its
compiler errors, if any, start real refine turns.

Pass: the batch ends with the item translated; the item folder's Verify.json has a status (passed, failed-after-N or
skipped) and one attempt per turn; BatchSummary.json's verifyStatus is the same; the log has one "Sending request" per
turn (1 + refine turns) and one "Verify and refine: turn" line per attempt.
Report: logs/live_verify_refine_0313_<label>_<provider>_<language>.json.
"""
import argparse
import glob
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
LANGUAGES = {'python': 'Python', 'cpp': 'Cpp'}

TAG_PY = r"""
import json, unreal
res = json.loads(unreal.N2CPythonBridge.tag_focused_graph("%(tag)s", "N2CGate", "card 0313 live check"))
result = res.get("data") or {"error": res.get("error")}
"""

CVAR_PY = r"""
import unreal
unreal.SystemLibrary.execute_console_command(None, "n2c.Verify.CppCompile 1")
result = {"set": True}
"""

HELPERS = r"""
import builtins, os, unreal
sub = unreal.get_editor_subsystem(unreal.N2CTranslationSubsystem)

def valid(guid):
    return guid is not None and guid.export_text().strip("0-{}") != ""

def info_of(guid):
    r = sub.get_session_info(guid)
    ok, info = (r[0], r[1]) if isinstance(r, tuple) and len(r) == 2 else (r is not None, r)
    if not ok or info is None:
        return None
    return {"state": info.state.name, "error": str(info.error_message), "run_folder": str(info.run_folder)}

def result_of(guid):
    r = sub.get_batch_session_result(guid)
    ok, res = (r[0], r[1]) if isinstance(r, tuple) and len(r) == 2 else (r is not None, r)
    if not ok or res is None:
        return None
    return {"total": res.total_count, "success": res.success_count, "failure": res.failure_count, "skipped": res.skipped_count,
            "input_tokens": res.total_input_tokens, "output_tokens": res.total_output_tokens}
"""

START_PY = HELPERS + r"""
graphs = unreal.N2CTagBlueprintLibrary.get_graphs_with_tag("%(tag)s", "")
r = sub.start_batch_translation(graphs)
guid, error = (r[0], r[1]) if isinstance(r, tuple) else (r, "")
builtins._n2c0313_batch = guid
result = {"graphs": len(graphs), "started": valid(guid), "error": str(error)}
"""

# A graph of an existing Blueprint (--blueprint/--graph), started as a batch of one
START_GRAPH_PY = HELPERS + r"""
tag = unreal.N2CTagInfo()
tag.set_editor_property("graph_name", "%(graph)s")
tag.set_editor_property("graph_guid", "%(guid)s")
tag.set_editor_property("blueprint_path", "%(path)s")
r = sub.start_batch_translation([tag])
guid, error = (r[0], r[1]) if isinstance(r, tuple) else (r, "")
builtins._n2c0313_batch = guid
result = {"graphs": 1, "started": valid(guid), "error": str(error)}
"""

POLL_PY = HELPERS + r"""
guid = builtins._n2c0313_batch
result = {"info": info_of(guid), "busy": sub.is_busy(), "batch_result": result_of(guid)}
"""


def run_py(port, code, rid, timeout=None):
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
    ap.add_argument('--language', default='python', choices=sorted(LANGUAGES))
    ap.add_argument('--port', type=int, default=27313)
    ap.add_argument('--blueprint', default='', help='translate this graph of an existing Blueprint instead of a probe Blueprint')
    ap.add_argument('--graph', default='')
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
    stem = 'live_verify_refine_0313_%s_%s_%s%s' % (args.label, args.provider, args.language, '_' + args.graph if args.graph else '')
    log_path = os.path.join(logs, stem + '_editor.log')
    report_path = os.path.join(logs, stem + '.json')
    project_dir = os.path.dirname(os.path.abspath(args.project))
    port = free_port(args.port)
    stamp = time.strftime('%H%M%S')
    tag = 'N2C0313%s' % stamp
    report = {'label': args.label, 'date': time.strftime('%Y-%m-%d'), 'provider': provider_name, 'model': args.model,
              'language': LANGUAGES[args.language]}

    session = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                            extra_args=['-LogCmds=LogNodeToCode VeryVerbose', *BLUEPRINT_EDITOR_ARGS])
    with session as s:
        s._patch('EditorPerProjectUserSettings.ini', '[/Script/NodeToCode.N2CUserSettings]', 'Provider', provider_name)
        s._patch('NodeToCode.ini', SETTINGS_SECTION, model_key, args.model)
        s._patch('NodeToCode.ini', SETTINGS_SECTION, 'TargetLanguage', LANGUAGES[args.language])
        s._patch('NodeToCode.ini', SETTINGS_SECTION, 'VerifyLanguages', LANGUAGES[args.language])
        s._patch('NodeToCode.ini', SETTINGS_SECTION, 'MaxRefineTurns', '2')
        for state_file in ('BlueprintGraphState.json', 'BlueprintGraphState.json.bak'):
            s.guard_file(os.path.join(project_dir, 'Saved', 'NodeToCode', state_file))
        report['started'] = s.start()
        if report['started']:
            if args.language == 'cpp':
                report['cvar'] = run_py(port, CVAR_PY, 5)
            sends_before = 0
            if args.blueprint:
                _, listed = call_tool(port, 'list-blueprint-graphs', {'blueprintPath': args.blueprint}, 9)
                structured = ((listed or {}).get('result') or {}).get('structuredContent') or {}
                graphs = structured.get('graphs') or []
                if not graphs:
                    report['graph_raw'] = json.dumps(listed)[:800]
                match = [g for g in graphs if g.get('name') == args.graph]
                report['graph'] = match[0] if match else {'missing': args.graph, 'listed': [g.get('name') for g in graphs]}
                if match:
                    sends_before = len(re.findall(r'Sending request to EN2CLLMProvider::', session.log_text() or ''))
                    report['start'] = run_py(port, START_GRAPH_PY % {'graph': args.graph, 'guid': match[0].get('graphGuid'),
                                                                    'path': args.blueprint}, 20)
                else:
                    report['start'] = None
            else:
                name = 'BP_N2C0313_%s' % stamp
                run_py(port, SETUP_PY % {'dir': ASSET_DIR, 'name': name}, 10)
                report['tagged'] = run_py(port, TAG_PY % {'tag': tag}, 11)
                sends_before = len(re.findall(r'Sending request to EN2CLLMProvider::', session.log_text() or ''))
                report['start'] = run_py(port, START_PY % {'tag': tag}, 20)
            outcome, rid, deadline = None, 20, time.time() + 900
            while report['start'] and report['start'].get('started') and time.time() < deadline:
                rid += 1
                r = run_py(port, POLL_PY, rid)
                if not r or not r.get('info') or r['info']['state'] in ('SUCCEEDED', 'FAILED', 'CANCELLED'):
                    outcome = r
                    break
                time.sleep(2.0)
            report['outcome'] = outcome
            time.sleep(2.0)
            text = session.log_text() or ''
            report['log'] = {
                'requests': len(re.findall(r'Sending request to EN2CLLMProvider::', text)) - sends_before,
                'verify_lines': re.findall(r'Verify and refine: [^\r\n]*?(?= - VerifyRefine|$)', text),
            }

            run_folder = ((outcome or {}).get('info') or {}).get('run_folder') or ''
            verify_files = glob.glob(os.path.join(run_folder, '*', 'Verify.json')) if run_folder else []
            verify = json.load(open(verify_files[0], encoding='utf-8')) if verify_files else {}
            summary_path = os.path.join(run_folder, 'BatchSummary.json') if run_folder else ''
            summary = json.load(open(summary_path, encoding='utf-8')) if summary_path and os.path.isfile(summary_path) else {}
            summary_status = [i.get('verifyStatus') for i in summary.get('items', [])]
            report['verify'] = {'file': verify_files[0] if verify_files else None, 'status': verify.get('status'),
                                'refine_turns': verify.get('refine_turns'),
                                'attempts': [{'turn': a.get('turn'), 'passed': a.get('passed'), 'errors': a.get('errors'),
                                              'warnings': a.get('warnings'),
                                              'first_errors': [d.get('message', '')[:200] for r in a.get('reports', [])
                                                               for d in r.get('diagnostics', []) if d.get('severity') == 'error'][:3]}
                                             for a in verify.get('attempts', [])],
                                'summary_status': summary_status}
            turns = verify.get('refine_turns')
            status = str(verify.get('status') or '')
            report['ok'] = bool(((outcome or {}).get('batch_result') or {}).get('success') == 1
                                and status and (status in ('passed', 'skipped') or status.startswith('failed-after-'))
                                and isinstance(turns, int) and len(verify.get('attempts', [])) == turns + 1
                                and summary_status == [status]
                                and report['log']['requests'] == turns + 1
                                and len(report['log']['verify_lines']) == turns + 1)
            print(json.dumps({'outcome': outcome, 'verify': report['verify'], 'log': report['log']})[:3000], flush=True)

    report['config_restored'] = session.restore_ok
    report['leaks'] = [list(x) for x in find_leaks(keys, [log_path, os.path.join(project_dir, 'Saved', 'Logs'),
                                                          os.path.join(project_dir, 'Saved', 'NodeToCode')])]
    report['ok'] = bool(report.get('ok') and not report['leaks'])
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print(json.dumps({'ok': report['ok'], 'status': (report.get('verify') or {}).get('status'),
                      'refine_turns': (report.get('verify') or {}).get('refine_turns'), 'leaks': report['leaks'],
                      'config_restored': report['config_restored'], 'report': report_path}))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
