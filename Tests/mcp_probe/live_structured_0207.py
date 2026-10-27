"""Structured tool results, live (card 0207): outputSchema in tools/list, structuredContent in tools/call, error codes.

Stdlib only; run with `python -I`:

    python -I live_structured_0207.py --engine G:/UE/UE_5.8 \
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor and makes an Actor Blueprint BP_Structured under a fresh /Game/N2CTest/Struct0207_<time>
folder (through run-python, never saved). Over raw JSON-RPC: tools/list gives the eight tools of card 0207 an object
outputSchema; list-blueprint-graphs, get-available-translation-targets, get-translation-output-directory and read-path
return structuredContent equal to their text (list-blueprint-graphs marked untrusted in both) with the schema's
required fields; list-blueprint-graphs without blueprintPath is isError with _meta["nodetocode/error"] {code
INVALID_ARGUMENT, field blueprintPath}; tools/call with arguments "x" is an isError result with code INVALID_ARGUMENT,
not a JSON-RPC error. Unless --skip-cli, claude -p with only mcp__nodetocode__* allowed calls list-blueprint-graphs and
answers with the graph names. On 5.8 the scratch folder is deleted at the end.
Report: logs/live_structured_0207_<label>.json.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_env_translation import post  # noqa: E402
from live_graph_target_0152 import Probe  # noqa: E402

NAME = 'BP_Structured'
TOOLS_WITH_SCHEMA = [
    'get-focused-blueprint', 'list-blueprint-graphs', 'get-open-blueprint-editors', 'translate-focused-blueprint',
    'get-available-translation-targets', 'get-available-llm-providers', 'get-translation-output-directory', 'read-path',
]

SETUP_PY = r'''
import unreal
folder = "%(folder)s"
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
bp = tools.create_asset("%(name)s", folder, unreal.Blueprint, factory)
result = {"created": bp is not None, "path": bp.get_path_name() if bp else ""}
'''

CLEANUP_PY = r'''
import unreal
folder = "%(folder)s"
result = {"deleted": unreal.EditorAssetLibrary.delete_directory(folder)}
result["exists_after"] = unreal.EditorAssetLibrary.does_directory_exist(folder)
'''

PROMPT = ('Using only the "nodetocode" MCP server, call list-blueprint-graphs once with blueprintPath "%(path)s". '
          'Do nothing else. Reply with only a JSON object {"graphs": [<the graph names it returned>]}.')


def call(port, rid, name, arguments_json):
    """tools/call with the arguments as raw JSON text (so a non-object can be sent)"""
    body = '{"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":"%s","arguments":%s}}' % (rid, name, arguments_json)
    status, text = post(port, json.loads(body))
    try:
        return status, json.loads(text)
    except ValueError:
        return status, {'raw': (text or '')[:500]}


def structured_check(message, required):
    result = (message or {}).get('result') or {}
    structured = result.get('structuredContent')
    text = ''.join(c.get('text', '') for c in result.get('content') or [] if c.get('type') == 'text')
    try:
        parsed = json.loads(text)
    except ValueError:
        parsed = None
    return {
        'isError': bool(result.get('isError')),
        'has_structured': isinstance(structured, dict),
        'text_equals_structured': parsed == structured,
        'required_present': isinstance(structured, dict) and all(k in structured for k in required),
        'marked': isinstance(structured, dict) and next(iter(structured), None) == 'untrusted_content',
    }, structured


def run_cli(prompt, mcp_config_path, model, timeout, out_path):
    cmd = [shutil.which('claude') or 'claude', '-p', prompt, '--mcp-config', mcp_config_path, '--strict-mcp-config',
           '--allowedTools', 'mcp__nodetocode__*', '--output-format', 'stream-json', '--verbose', '--model', model]
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
        out = proc.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b'').decode('utf-8', 'replace')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(out)
    calls, results, final = [], {}, None
    for line in out.splitlines():
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        if msg.get('type') == 'result':
            final = msg
        for block in (msg.get('message') or {}).get('content') or []:
            if isinstance(block, dict) and block.get('type') == 'tool_use':
                calls.append({'id': block.get('id'), 'tool': block.get('name'), 'input': block.get('input') or {}})
            elif isinstance(block, dict) and block.get('type') == 'tool_result':
                results[block.get('tool_use_id')] = bool(block.get('is_error'))
    for c in calls:
        c['error'] = results.get(c['id'], True)
    answer, text = None, (final or {}).get('result') or ''
    start, end = text.find('{'), text.rfind('}')
    if start >= 0 and end > start:
        try:
            answer = json.loads(text[start:end + 1])
        except ValueError:
            answer = text[:300]
    return {'calls': calls, 'answer': answer}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27207)
    ap.add_argument('--cli-model', default='claude-haiku-5-5')
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--skip-cli', action='store_true')
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_structured_0207_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_structured_0207_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_structured_0207_%s_mcp.json' % args.label)

    port = free_port(args.port + int(os.environ.get('N2C_PORT_OFFSET', '0') or 0))
    folder = '/Game/N2CTest/Struct0207_%s' % time.strftime('%H%M%S')
    report = {'label': args.label, 'port': port, 'folder': folder}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            data, text = p.python(SETUP_PY % {'folder': folder, 'name': NAME})
            report['setup'] = data or text
            path = data.get('path') or '%s/%s' % (folder, NAME)
            checks['setup: BP_Structured made'] = data.get('created') is True

            status, listed = post(port, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list', 'params': {}})
            tools = {t.get('name'): t for t in (json.loads(listed).get('result') or {}).get('tools') or []}
            with_schema = sorted(n for n, t in tools.items() if isinstance(t.get('outputSchema'), dict))
            report['tools_with_outputSchema'] = with_schema
            checks['tools/list: the eight tools carry an object outputSchema'] = all(
                (tools.get(n) or {}).get('outputSchema', {}).get('type') == 'object' for n in TOOLS_WITH_SCHEMA)

            samples = [
                ('list-blueprint-graphs', json.dumps({'blueprintPath': path}), ['blueprintPath', 'graphCount', 'graphs'], True),
                ('get-available-translation-targets', '{}', ['languages', 'currentLanguage'], False),
                ('get-translation-output-directory', '{}', ['isCustomDirectorySet', 'absolutePath'], False),
                ('read-path', '{"relativePath":"Config","pageSize":2}', ['path', 'directories', 'files', 'truncated'], False),
            ]
            rid = 10
            report['samples'] = {}
            structured_by_name = {}
            for name, arguments, required, marked in samples:
                rid += 1
                _, message = call(port, rid, name, arguments)
                result, structured = structured_check(message, required)
                structured_by_name[name] = structured if isinstance(structured, dict) else {}
                report['samples'][name] = dict(result, keys=list(structured)[:8] if isinstance(structured, dict) else None)
                checks['%s: structuredContent equals its text, required fields there' % name] = not result['isError'] \
                    and result['has_structured'] and result['text_equals_structured'] and result['required_present']
                if marked:
                    checks['%s: structuredContent marked untrusted first' % name] = result['marked']

            _, missing = call(port, 30, 'list-blueprint-graphs', '{}')
            missing_result = missing.get('result') or {}
            error_meta = (missing_result.get('_meta') or {}).get('nodetocode/error') or {}
            report['missing_argument'] = {'isError': missing_result.get('isError'), 'error': error_meta}
            checks['missing blueprintPath: isError, code INVALID_ARGUMENT, field blueprintPath'] = \
                missing_result.get('isError') is True and error_meta.get('code') == 'INVALID_ARGUMENT' \
                and error_meta.get('field') == 'blueprintPath'

            _, bad = call(port, 31, 'get-available-translation-targets', '"x"')
            bad_result = bad.get('result') or {}
            report['arguments_not_object'] = {'error': bad.get('error'), 'isError': bad_result.get('isError'),
                                              'meta': bad_result.get('_meta')}
            checks['arguments "x": isError result with INVALID_ARGUMENT, no JSON-RPC error'] = 'error' not in bad \
                and bad_result.get('isError') is True \
                and ((bad_result.get('_meta') or {}).get('nodetocode/error') or {}).get('code') == 'INVALID_ARGUMENT'

            if not args.skip_cli:
                with open(mcp_config_path, 'w', encoding='utf-8') as f:
                    json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
                run = run_cli(PROMPT % {'path': path}, mcp_config_path, args.cli_model, args.timeout,
                              os.path.join(log_dir, 'live_structured_0207_%s_cli.jsonl' % args.label))
                report['cli'] = run
                graphs = [g.get('name') for g in structured_by_name.get('list-blueprint-graphs', {}).get('graphs') or []]
                answer = run['answer'] if isinstance(run['answer'], dict) else {}
                checks['agent: called list-blueprint-graphs without error'] = any(
                    c['tool'] == 'mcp__nodetocode__list-blueprint-graphs' and not c['error'] for c in run['calls'])
                checks['agent: answered with the graph names'] = bool(graphs) and sorted(answer.get('graphs') or []) == sorted(graphs)
                report['graphs'] = graphs

            if args.label.startswith('5.8'):
                data, text = p.python(CLEANUP_PY % {'folder': folder})
                report['cleanup'] = data or text
                checks['scratch folder deleted'] = data.get('exists_after') is False
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
