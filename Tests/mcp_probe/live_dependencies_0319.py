"""get-blueprint-dependencies over the dependency fixtures, live through claude -p, plus the Python wrapper (card 0319).

Stdlib only; run with `python -I`:

    python -I live_dependencies_0319.py --engine <UE_5.8> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor on a host that has Tests/NodeToCodeTestContent linked into its Plugins folder (build.ps1
does that), so the fixtures of card 0318 are at /NodeToCodeTestContent/Dependencies. Then claude -p, with only
mcp__nodetocode__* allowed, builds the dependency index of that folder with get-blueprint-dependencies and answers
with the order, the cycle groups and the graph where BP_N2CDep_A calls BP_N2CDep_B.

Checks: the agent called get-blueprint-dependencies with that folder and it did not fail; the result is marked
untrusted first and holds the exact order (E, S, BPI, BPML, B, C, A, Child), the one cycle group {B, C} and the calls
edge A -> B in EventGraph; the agent's answer matches. The probe then calls the tool with toFile (the export under
Saved/NodeToCode/Dependencies parses and has the same order; it is removed afterwards) and runs
nodetocode.get_blueprint_dependencies through run-python (same order). Report:
logs/live_dependencies_0319_<label>.json.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import Probe, run_cli  # noqa: E402

FOLDER = '/NodeToCodeTestContent/Dependencies'
NAMES = ['E_N2CDep', 'S_N2CDep', 'BPI_N2CDep', 'BPML_N2CDep', 'BP_N2CDep_B', 'BP_N2CDep_C', 'BP_N2CDep_A', 'BP_N2CDep_Child']


def path(name):
    return '%s/%s.%s' % (FOLDER, name, name)


ORDER = [path(n) for n in NAMES]
CYCLE = [path('BP_N2CDep_B'), path('BP_N2CDep_C')]

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), build the dependency index of the content folder "%s" '
    'with get-blueprint-dependencies, one call. Do nothing else. Reply with only a JSON object {"order": [<asset '
    'names, e.g. BP_N2CDep_A, in the order the result gives>], "cycleGroups": [[<asset names>]], "aCallsBIn": <the '
    'graph name of the calls edge from BP_N2CDep_A to BP_N2CDep_B>}.') % FOLDER

PYTHON = r'''
import nodetocode
r = nodetocode.get_blueprint_dependencies("%s")
result = {"success": r.get("success"), "error": r.get("error"),
          "order": (r.get("data") or {}).get("order"), "cycleGroups": (r.get("data") or {}).get("cycleGroups")}
''' % FOLDER


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27196)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_dependencies_0319_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_dependencies_0319_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_dependencies_0319_%s_mcp.json' % args.label)

    port = free_port(args.port)
    report = {'label': args.label, 'port': port}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            p = Probe(port)
            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)

            run = run_cli(PROMPT, mcp_config_path, args.cli_model, args.timeout,
                          os.path.join(log_dir, 'live_dependencies_0319_%s_cli.jsonl' % args.label))
            report['cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                             'answer': run['answer'], 'exit': run['exit'], 'seconds': run['seconds'], 'final': run['final']}
            calls = [c for c in run['calls'] if c['tool'] == 'get-blueprint-dependencies']
            checks['the agent called get-blueprint-dependencies with the folder'] = bool(calls) and \
                not calls[-1]['error'] and FOLDER in (calls[-1]['input'].get('paths') or [])
            # Claude Code may call a deferred tool before loading its schema (ToolSearch) and guess an argument name;
            # that call is refused with INVALID_ARGUMENT and retried. Every other NodeToCode call must succeed.
            ours = [c for c in run['calls'] if c['tool'] != 'ToolSearch']
            report['refused_guesses'] = [(c['input'], c['text'][:200]) for c in ours if c['error']]
            checks['every failed call was a refused argument guess'] = all(
                'INVALID_ARGUMENT' in c['text'] for c in ours if c['error'])
            checks['the agent used no other NodeToCode tool'] = all(c['tool'] == 'get-blueprint-dependencies' for c in ours)
            body = strip_mark(calls[-1]['body']) if calls else {}
            report['result'] = {k: body.get(k) for k in ('nodeCount', 'edgeCount', 'order', 'cycleGroups', 'warnings', 'inline')}
            checks['result: marked untrusted first'] = bool(calls) and calls[-1]['text'].lstrip().startswith('{"untrusted_content"')
            checks['result: the exact dependencies-first order'] = body.get('order') == ORDER
            checks['result: the one cycle group {B, C}'] = body.get('cycleGroups') == [CYCLE]
            checks['result: A calls B in EventGraph'] = any(
                e.get('from') == path('BP_N2CDep_A') and e.get('to') == path('BP_N2CDep_B') and e.get('kind') == 'calls'
                and e.get('graph') == 'EventGraph' for e in body.get('edges') or [])
            answer = run['answer'] if isinstance(run['answer'], dict) else {}
            checks['the answer matches the result'] = answer.get('order') == NAMES and \
                answer.get('cycleGroups') == [['BP_N2CDep_B', 'BP_N2CDep_C']] and answer.get('aCallsBIn') == 'EventGraph'

            status, text, err = p.tool('get-blueprint-dependencies', {'paths': [FOLDER], 'toFile': True})
            try:
                saved = strip_mark(json.loads(text))
            except ValueError:
                saved = {}
            report['to_file'] = {k: saved.get(k) for k in ('savedTo', 'bytes', 'inline')}
            file_ok = False
            if saved.get('savedTo') and os.path.isfile(saved['savedTo']):
                with open(saved['savedTo'], 'rb') as f:
                    exported = f.read()
                try:
                    file_ok = json.loads(exported.decode('utf-8')).get('order') == ORDER and \
                        len(exported) == saved.get('bytes')
                except ValueError:
                    pass
                report['to_file']['under_dependencies_folder'] = \
                    os.path.normpath(saved['savedTo']).split(os.sep)[-3:-1] == ['NodeToCode', 'Dependencies']
                os.remove(saved['savedTo'])
            checks['toFile: the export parses, has the order and the stated size'] = not err and file_ok \
                and report['to_file'].get('under_dependencies_folder') is True

            data, text = p.python(PYTHON)
            report['python'] = data or text
            checks['python: nodetocode.get_blueprint_dependencies gives the same order'] = \
                data.get('success') is True and data.get('order') == ORDER and data.get('cycleGroups') == [CYCLE]
            checks['editor_alive_at_end'] = s.health()

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok'], 'cli': report.get('cli', {}).get('calls')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
