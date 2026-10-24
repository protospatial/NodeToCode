"""A live translation cut at a tiny Max Output Tokens is reported as a Truncated error in the MCP result (card 0092).

Stdlib only; run with `python -I`:

    python -I live_truncated.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

For each case it sets the provider's Max Output Tokens setting (card 0060) to a few tokens on the settings object,
runs translate-focused-blueprint over MCP on an in-memory probe Blueprint with a current registry model, and checks the
CallToolResult is an error whose text is NodeToCode error JSON with error_kind "Truncated", the provider's own stop or
finish reason in provider_error_type (max_tokens, MAX_TOKENS, length, max_output_tokens) and a message that names the
Max Output Tokens setting. The setting is restored afterwards. A cap this small costs a few output tokens per case.

Keys come from the repo's .env into the editor's environment only (never printed or saved); the per-user secrets file
must hold none for these providers. The editor log, Saved/Logs, the plugin log, new translation folders and the
report are scanned for key values. Refusals and safety blocks are not triggered live; the spec
NodeToCode.LLM.Parse covers them with documented response bodies. Report: logs/live_truncated_<label>.json.
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import ASSET_DIR, SETUP_PY, STORE_ID, run_tool_call, stored_key_present, tool_text  # noqa: E402
from live_provider_matrix import CONNECT_PY, apply_settings, restore_spec, run_py  # noqa: E402

# Current registry models only (owner instruction 2026-10-23); a cap of 16 output tokens cuts any translation
CASES = [
    {'id': 'anthropic/haiku-5-5', 'provider': 'anthropic', 'model': 'claude-haiku-5-5', 'variable': 'ANTHROPIC_API_KEY',
     'setting': 'anthropic_max_output_tokens', 'cap': 16, 'reasons': ['max_tokens'], 'hint': 'Anthropic > Max Output Tokens'},
    {'id': 'gemini/3.5-flash-lite', 'provider': 'gemini', 'model': 'gemini-3.5-flash-lite', 'variable': 'GEMINI_API_KEY',
     'setting': 'gemini_max_output_tokens', 'cap': 16, 'reasons': ['MAX_TOKENS'], 'hint': 'Gemini > Max Output Tokens'},
    {'id': 'openai/gpt-6-luna', 'provider': 'openai', 'model': 'gpt-6-luna', 'variable': 'OPENAI_API_KEY',
     'setting': 'open_ai_max_output_tokens', 'cap': 16, 'reasons': ['length', 'max_output_tokens'], 'hint': 'OpenAI > Max Output Tokens'},
    {'id': 'deepseek/flash', 'provider': 'deepseek', 'model': 'deepseek-flash', 'variable': 'DEEPSEEK_API_KEY',
     'setting': 'deep_seek_max_output_tokens', 'cap': 16, 'reasons': ['length'], 'hint': 'DeepSeek > Max Output Tokens'},
]


def check_case(port, c, rid, timeout):
    rec = {'id': c['id'], 'provider': c['provider'], 'model': c['model'], 'cap': c['cap']}
    rec['settings_applied'] = apply_settings(port, {c['setting']: c['cap']}, rid)
    try:
        events = run_tool_call(port, 'translate-focused-blueprint',
                               {'provider': c['provider'], 'model': c['model'], 'language': 'cpp'}, rid + 1,
                               meta={'progressToken': 'truncated-%d' % rid}, timeout=timeout)
    finally:
        previous = (rec['settings_applied'] or {}).get('previous') if isinstance(rec['settings_applied'], dict) else None
        rec['settings_restored'] = apply_settings(port, restore_spec(previous), rid + 2) if previous else None
    final = [d for e, d in events if e == 'response']
    if not final:
        rec['ok'] = False
        rec['reason'] = 'no response'
        return rec
    message = json.loads(final[-1])
    result = message.get('result') or {}
    rec['is_error'] = bool(result.get('isError'))
    text = tool_text(message)
    rec['text'] = text[:1500]
    try:
        error = json.loads(text)
    except ValueError:
        error = {}
    if not isinstance(error, dict):
        error = {}
    for k in ('error_kind', 'provider_error_type', 'message', 'retryable', 'provider', 'model', 'http_status'):
        rec[k] = error.get(k)
    rec['ok'] = bool(rec['is_error'] and rec['error_kind'] == 'Truncated'
                     and rec['provider_error_type'] in c['reasons']
                     and c['hint'] in (rec['message'] or '') and rec['retryable'] is False
                     and rec['http_status'] is None
                     and isinstance(rec['settings_applied'], dict)
                     and (rec['settings_applied'].get('applied') or {}).get(c['setting']) == c['cap'])
    return rec


def run_session(session, args, port, cases, report):
    with session as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CTruncated_%s' % time.strftime('%H%M%S')
            report['setup'] = run_py(port, SETUP_PY % {'dir': ASSET_DIR, 'name': name}, 1)
            report['wiring'] = run_py(port, CONNECT_PY, 2)
            for i, c in enumerate(cases):
                rec = check_case(port, c, 100 + 10 * i, args.timeout)
                report['cases'].append(rec)
                print('%-24s %s  kind=%s reason=%s  %s' % (c['id'], 'PASS' if rec['ok'] else 'FAIL', rec.get('error_kind'),
                                                          rec.get('provider_error_type'), (rec.get('message') or rec.get('text') or '')[:160]),
                      flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', default='5.8')
    ap.add_argument('--port', type=int, default=27192)
    ap.add_argument('--timeout', type=int, default=240)
    ap.add_argument('--only', nargs='*', help='case ids or providers to run (default: all)')
    args = ap.parse_args()

    cases = [c for c in CASES if not args.only or c['id'] in args.only or c['provider'] in args.only]
    keys = load_provider_keys(only=sorted({c['variable'] for c in cases}))
    missing = sorted({c['variable'] for c in cases} - set(keys))
    if missing:
        print(json.dumps({'ok': False, 'reason': 'not in .env: %s' % missing}))
        return 2
    saved = [c['provider'] for c in cases if stored_key_present(STORE_ID[c['provider']])]
    if saved:
        print(json.dumps({'ok': False, 'reason': 'keys saved in the per-user secrets file for %s' % saved}))
        return 2

    logs = os.path.join(HERE, 'logs')
    os.makedirs(logs, exist_ok=True)
    log_path = os.path.join(logs, 'live_truncated_%s_editor.log' % args.label)
    report_path = os.path.join(logs, 'live_truncated_%s.json' % args.label)
    project_dir = os.path.dirname(os.path.abspath(args.project))
    translations_dir = os.path.join(project_dir, 'Saved', 'NodeToCode', 'Translations')
    before_dirs = set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()
    port = free_port(args.port)
    report = {'label': args.label, 'date': time.strftime('%Y-%m-%d'), 'port': port, 'cases': []}

    # Changing a setting through the editor saves it to the host's Config/DefaultNodeToCode.ini (and stamps the
    # settings version, which the settings migration specs then see), so the file is put back as it was afterwards
    config_path = os.path.join(project_dir, 'Config', 'DefaultNodeToCode.ini')
    config_before = open(config_path, 'rb').read() if os.path.isfile(config_path) else None

    session = EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                            extra_args=['-RenderOffScreen'])
    try:
        run_session(session, args, port, cases, report)
    finally:
        if config_before is None:
            if os.path.isfile(config_path):
                os.remove(config_path)
        else:
            with open(config_path, 'wb') as f:
                f.write(config_before)
        report['config_restored'] = (open(config_path, 'rb').read() if os.path.isfile(config_path) else None) == config_before

    log_text = session.log_text()
    report['log'] = {
        'truncated_lines': [l.strip()[:400] for l in log_text.splitlines() if '(Truncated)' in l][:8],
        'unbalanced_brace_lines': log_text.count('Potentially truncated or malformed JSON'),
    }
    new_dirs = [os.path.join(translations_dir, d) for d in sorted((set(os.listdir(translations_dir)) if os.path.isdir(translations_dir) else set()) - before_dirs)]
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    scan = [log_path, report_path, os.path.join(project_dir, 'Saved', 'Logs'),
            os.path.join(project_dir, 'Saved', 'NodeToCode.log')] + new_dirs
    report['leaks'] = [{'path': p, 'variable': n} for p, n in find_leaks(keys, scan)]
    report['ok'] = bool(report.get('started') and report['cases'] and all(c['ok'] for c in report['cases'])
                        and report['log']['unbalanced_brace_lines'] == 0 and not report['leaks']
                        and report.get('config_restored'))
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'ok': report['ok'], 'leaks': report['leaks'], 'log': report['log']}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
