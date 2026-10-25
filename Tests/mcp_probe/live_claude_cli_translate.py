"""A standard MCP client (the headless Claude Code CLI) runs translate-focused-blueprint and gets the translation.

Stdlib only; run with `python -I`:

    python -I live_claude_cli_translate.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor with only the provider's key (from the repo's .env) added to its environment, makes an
in-memory probe Blueprint under /Game/N2CTest (as live_env_translation.py does), then runs
`claude -p ... --mcp-config <the editor's /mcp> --strict-mcp-config --allowedTools "mcp__nodetocode__*"
--output-format stream-json --verbose` and asks it to call translate-focused-blueprint once. It passes when the CLI's
tool result for that call is a translation with code (the whole CallToolResult arrived on the tools/call's own POST),
the call is not an error, the editor log shows how the call was answered (text/event-stream or one JSON response),
and no key value is in the editor log or the report. The CLI uses its own login, never the provider key (the key is
removed from its environment). Key values are never printed. Report: logs/live_claude_cli_<label>.json.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import BLUEPRINT_EDITOR_ARGS, EditorSession, free_port  # noqa: E402
from env_keys import find_leaks, load_provider_keys  # noqa: E402
from live_env_translation import (ASSET_DIR, PROVIDER_VARIABLE, SETUP_PY, STORE_ID, call_tool,  # noqa: E402
                                  stored_key_present, tool_text)

TOOL = 'mcp__nodetocode__translate-focused-blueprint'
PROMPT = ('Call the %s tool exactly once with arguments {"provider": "%s", "model": "%s", "language": "%s"}. '
          'Do not call any other tool. Then reply with only the word DONE.')


def code_chars_of(text):
    """Characters of generated code in a translation's JSON text (0 when it is not a translation)"""
    try:
        translation = json.loads(text)
    except (ValueError, TypeError):
        return 0, None
    if not isinstance(translation, dict):
        return 0, None
    graphs = translation.get('graphs') or []
    chars = sum(len((g.get('code') or {}).get('graphDeclaration', '') or '')
                + len((g.get('code') or {}).get('graphImplementation', '') or '') for g in graphs if isinstance(g, dict))
    return chars, len(graphs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--provider', default='anthropic', choices=sorted(PROVIDER_VARIABLE))
    ap.add_argument('--model', default='claude-haiku-5-5', help='model the editor translates with')
    ap.add_argument('--cli-model', default='haiku', help='model the Claude CLI itself runs on')
    ap.add_argument('--language', default='cpp')
    ap.add_argument('--timeout', type=int, default=600, help='bound on the CLI run')
    ap.add_argument('--port', type=int, default=27190)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_claude_cli_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_claude_cli_%s.json' % args.label)
    cli_out_path = os.path.join(log_dir, 'live_claude_cli_%s_stream.jsonl' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_claude_cli_%s_mcp.json' % args.label)

    variable = PROVIDER_VARIABLE[args.provider]
    keys = load_provider_keys(only=[variable])
    if variable not in keys:
        print(json.dumps({'ok': False, 'reason': '%s not in .env' % variable}))
        return 2
    if stored_key_present(STORE_ID[args.provider]):
        print(json.dumps({'ok': False, 'reason': 'a %s key is saved in the per-user secrets file' % STORE_ID[args.provider]}))
        return 2

    port = free_port(args.port)
    report = {'label': args.label, 'provider': args.provider, 'model': args.model, 'cli_model': args.cli_model, 'port': port}
    checks = {}

    # -RenderOffScreen: on UE 5.6 with -nullrhi the opened Blueprint editor's window is Fatal without it
    with EditorSession(args.engine, args.project, port, log_path, allow_python=True, extra_env=keys,
                       extra_args=BLUEPRINT_EDITOR_ARGS) as s:
        report['started'] = s.start()
        if report['started']:
            name = 'BP_N2CCliProbe_%s' % time.strftime('%H%M%S')
            status, setup = call_tool(port, 'run-python', {'code': SETUP_PY % {'dir': ASSET_DIR, 'name': name}}, 1)
            report['setup'] = tool_text(setup)[:1000] or setup

            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)

            # The CLI uses its own login: no provider key in its environment
            cli_env = {k: v for k, v in os.environ.items() if k not in PROVIDER_VARIABLE.values() and k != 'DEEPSEEK_API'}
            prompt = PROMPT % (TOOL, args.provider, args.model, args.language)
            cmd = [shutil.which('claude') or 'claude', '-p', prompt, '--mcp-config', mcp_config_path, '--strict-mcp-config',
                   '--allowedTools', 'mcp__nodetocode__*', '--output-format', 'stream-json', '--verbose',
                   '--model', args.cli_model]
            t0 = time.time()
            try:
                proc = subprocess.run(cmd, capture_output=True, timeout=args.timeout, env=cli_env,
                                      stdin=subprocess.DEVNULL)
                report['cli_exit'] = proc.returncode
                out = proc.stdout.decode('utf-8', 'replace')
                report['cli_stderr_head'] = proc.stderr.decode('utf-8', 'replace')[:600]
            except subprocess.TimeoutExpired as e:
                report['cli_exit'] = 'timeout'
                out = (e.stdout or b'').decode('utf-8', 'replace')
            report['cli_seconds'] = round(time.time() - t0, 1)
            with open(cli_out_path, 'w', encoding='utf-8') as f:
                f.write(out)

            # Find the tool_use for the translate tool and its tool_result
            uses, results, final = {}, {}, None
            for line in out.splitlines():
                try:
                    msg = json.loads(line)
                except ValueError:
                    continue
                if msg.get('type') == 'result':
                    final = msg
                content = (msg.get('message') or {}).get('content')
                if not isinstance(content, list):
                    continue
                for block in content:
                    if block.get('type') == 'tool_use':
                        uses[block.get('id')] = block
                    elif block.get('type') == 'tool_result':
                        results[block.get('tool_use_id')] = block
            translate_uses = [u for u in uses.values() if u.get('name') == TOOL]
            report['tool_uses'] = [{'name': u.get('name'), 'input': u.get('input')} for u in uses.values()]
            report['final'] = {k: (final or {}).get(k) for k in ('subtype', 'is_error', 'result', 'num_turns', 'duration_ms')}
            if translate_uses:
                result = results.get(translate_uses[0].get('id')) or {}
                content = result.get('content')
                text = content if isinstance(content, str) else ''.join(
                    c.get('text', '') for c in (content or []) if isinstance(c, dict))
                chars, graphs = code_chars_of(text)
                report['tool_result'] = {'is_error': bool(result.get('is_error')), 'chars': len(text), 'graphs': graphs,
                                         'code_chars': chars, 'head': text[:300]}
                checks['cli_called_translate'] = True
                checks['tool_result_not_error'] = not result.get('is_error')
                checks['tool_result_has_code'] = chars > 0
            else:
                checks['cli_called_translate'] = False
            checks['editor_alive_at_end'] = s.health()

    log_text = s.log_text()
    answered = re.findall(r'Started task ([0-9A-F]{32}) for tool translate-focused-blueprint \(progress token ([^)]*)\), answering ([^\r\n]*)', log_text)
    report['editor_answered'] = [{'task': t, 'token': k, 'how': h} for t, k, h in answered]
    checks['editor_started_one_task'] = len(answered) == 1
    report['checks'] = checks

    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    leaks = find_leaks(keys, [log_path, report_path, cli_out_path])
    report['leaks'] = [{'path': p, 'variable': n} for p, n in leaks]
    report['ok'] = bool(checks) and all(checks.values()) and not leaks
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps(report, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
