"""translate-material explains a real engine material live, directly and through claude -p (card 0455).

Stdlib only; run with `python -I`, on each engine:

    python -I live_translate_material_0455.py --engine <UE_5.x> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_x/NodeToCodeHost_5_x.uproject" --label 5.x

It starts a headless editor with only the providers' keys from the repo's .env in its environment (never printed).
Direct tools/call checks:
- translate-material on /Engine/EngineMaterials/WorldGridMaterial with each --provider/--model pair (default: anthropic
  claude-haiku-5-5 and gemini gemini-3.5-flash-lite). Each must give an explanation with code, target.assetPath,
  marked untrusted first, and no run_folder (nothing archived).
- language cpp, which must be TARGET_NOT_AVAILABLE_FOR_MATERIALS.

Then claude -p (its own login, no provider key), with only mcp__nodetocode__* allowed, asks for an explanation of
WorldGridMaterial with translate-material and the first pair, and answers in one sentence. No key value may appear in
the editor log or the report. Report: logs/live_translate_material_0455_<label>.json.
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
from live_env_translation import PROVIDER_VARIABLE, call_tool, tool_text  # noqa: E402
from live_graph_target_0152 import run_cli  # noqa: E402

WORLD_GRID = '/Engine/EngineMaterials/WorldGridMaterial'
PROMPT = ('Using only the nodetocode MCP tools, explain the material "%s": call translate-material once with '
          '{"materialPath": "%s", "provider": "%s", "model": "%s"}. Do nothing else. Reply with only a JSON object '
          '{"explained": true, "summary": "<one sentence on what the material computes, from the explanation>"}.')


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def code_chars(doc):
    return sum(len((g.get('code') or {}).get('graphImplementation') or '') + len((g.get('code') or {}).get('graphDeclaration') or '')
               for g in (doc or {}).get('graphs') or [] if isinstance(g, dict))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--pair', action='append', help='provider=model (repeatable)')
    ap.add_argument('--port', type=int, default=27191)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()
    pairs = [p.split('=', 1) for p in (args.pair or ['anthropic=claude-haiku-5-5', 'gemini=gemini-3.5-flash-lite'])]

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    stem = 'live_translate_material_0455_%s' % args.label
    log_path = os.path.join(log_dir, stem + '_editor.log')
    report_path = os.path.join(log_dir, stem + '.json')
    mcp_config_path = os.path.join(log_dir, stem + '_mcp.json')
    keys = load_provider_keys(only=sorted({PROVIDER_VARIABLE[p] for p, _ in pairs}))
    port = free_port(args.port)
    report = {'label': args.label, 'port': port, 'pairs': pairs, 'direct': {}}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=False, extra_env=keys) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            rid = 1
            for provider, model in pairs:
                rid += 1
                t0 = time.time()
                status, response = call_tool(port, 'translate-material',
                                             {'materialPath': WORLD_GRID, 'provider': provider, 'model': model}, rid)
                text = tool_text(response)
                is_error = bool((response.get('result') or {}).get('isError'))
                try:
                    doc = strip_mark(json.loads(text))
                except ValueError:
                    doc = {}
                target = doc.get('target') or {}
                report['direct'][provider] = {'status': status, 'seconds': round(time.time() - t0, 1), 'error': text[:400] if is_error else None,
                                              'graphs': len(doc.get('graphs') or []), 'code_chars': code_chars(doc),
                                              'usage': doc.get('usage'), 'target': target, 'keys': sorted(doc),
                                              'excerpt': ((doc.get('graphs') or [{}])[0].get('code') or {}).get('graphImplementation', '')[:600]}
                checks['%s %s: an explanation with code, the target, marked, nothing archived' % (provider, model)] = not is_error \
                    and code_chars(doc) > 0 and str(target.get('assetPath', '')).startswith(WORLD_GRID) \
                    and text.lstrip().startswith('{"untrusted_content"') and 'run_folder' not in doc

            rid += 1
            status, response = call_tool(port, 'translate-material', {'materialPath': WORLD_GRID, 'language': 'cpp'}, rid)
            text = tool_text(response)
            report['cpp'] = text[:300]
            checks['cpp: TARGET_NOT_AVAILABLE_FOR_MATERIALS'] = bool((response.get('result') or {}).get('isError')) \
                and text.startswith('TARGET_NOT_AVAILABLE_FOR_MATERIALS')

            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            provider, model = pairs[0]
            # The CLI uses its own login: no provider key in its environment
            saved_env = {k: os.environ.pop(k) for k in list(os.environ) if k in PROVIDER_VARIABLE.values() or k == 'DEEPSEEK_API'}
            try:
                run = run_cli(PROMPT % (WORLD_GRID, WORLD_GRID, provider, model), mcp_config_path, args.cli_model, args.timeout,
                              os.path.join(log_dir, stem + '_cli.jsonl'))
            finally:
                os.environ.update(saved_env)
            report['cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                             'answer': run['answer'], 'exit': run['exit'], 'seconds': run['seconds']}
            calls = [c for c in run['calls'] if c['tool'] == 'translate-material']
            checks['claude -p: translate-material on WorldGridMaterial, no error'] = bool(calls) and not calls[-1]['error'] \
                and str(calls[-1]['input'].get('materialPath', '')).startswith(WORLD_GRID)
            answer = run['answer'] if isinstance(run['answer'], dict) else {}
            checks['claude -p: answered with a summary'] = answer.get('explained') is True and len(str(answer.get('summary') or '')) > 10
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    leaks = find_leaks(keys, [log_path])
    checks['no key value in the editor log'] = not leaks
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    text = json.dumps(report, indent=1)
    checks['no key value in the report'] = not any(v and v in text for v in keys.values())
    report['ok'] = report['ok'] and checks['no key value in the report']
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok'],
                      'direct': {k: {kk: v[kk] for kk in ('seconds', 'graphs', 'code_chars', 'error')} for k, v in report['direct'].items()},
                      'cli_answer': (report.get('cli') or {}).get('answer')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
