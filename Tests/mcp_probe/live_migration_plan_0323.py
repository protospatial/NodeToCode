"""create-migration-plan over the dependency fixtures, live through claude -p (card 0323).

Stdlib only; run with `python -I`:

    python -I live_migration_plan_0323.py --engine <UE_5.8> \
        --project "<checkout>/../Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

It starts a headless editor on a host that has Tests/NodeToCodeTestContent linked into its Plugins folder (build.ps1
does that), so the fixtures of card 0318 are at /NodeToCodeTestContent/Dependencies. Then claude -p, with only
mcp__nodetocode__* allowed, writes a migration plan for that folder into the host project's own module and answers
with the plan id, the step count and the assets of the cycle step.

Checks: the agent called create-migration-plan with that folder and the host's module and it did not fail (a refused
argument guess before Claude Code loads the schema is allowed); the result is marked untrusted first and is a draft
with seven steps, step 5 the cycle group {B, C}; plan.json and plan.md exist in Saved/NodeToCode/Migration/<planId>,
plan.json is a draft with the same steps, and the plan folder holds nothing else; the host's Source folder is
unchanged; the agent's answer matches. The plan folder is removed afterwards. Report:
logs/live_migration_plan_0323_<label>.json.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from editor_session import EditorSession, free_port  # noqa: E402
from live_graph_target_0152 import run_cli  # noqa: E402

FOLDER = '/NodeToCodeTestContent/Dependencies'
CYCLE = ['%s/%s.%s' % (FOLDER, n, n) for n in ('BP_N2CDep_B', 'BP_N2CDep_C')]

PROMPT = (
    'Using only the nodetocode MCP tools (not run-python), write a migration plan with create-migration-plan for the '
    'content folder "%(folder)s" into the C++ module "%(module)s", one call. Do nothing else. Reply with only a JSON '
    'object {"planId": <the plan id>, "steps": <the number of steps>, "cycleStep": [<asset names of the cycle-group '
    'step, e.g. BP_N2CDep_B>]}.')


def strip_mark(body):
    return {k: v for k, v in (body or {}).items() if k != 'untrusted_content'}


def files_under(folder):
    out = []
    for root, _dirs, files in os.walk(folder):
        out.extend(os.path.join(root, f) for f in files)
    return sorted(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True)
    ap.add_argument('--port', type=int, default=27194)
    ap.add_argument('--cli-model', default='haiku')
    ap.add_argument('--timeout', type=int, default=600)
    args = ap.parse_args()

    log_dir = os.path.join(HERE, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, 'live_migration_plan_0323_%s_editor.log' % args.label)
    report_path = os.path.join(log_dir, 'live_migration_plan_0323_%s.json' % args.label)
    mcp_config_path = os.path.join(log_dir, 'live_migration_plan_0323_%s_mcp.json' % args.label)

    module = os.path.splitext(os.path.basename(args.project))[0]
    source_dir = os.path.join(os.path.dirname(os.path.abspath(args.project)), 'Source')
    source_before = files_under(source_dir)
    port = free_port(args.port)
    report = {'label': args.label, 'port': port, 'module': module}
    checks = {}

    with EditorSession(args.engine, args.project, port, log_path, allow_python=True) as s:
        report['started'] = s.start()
        if not report['started']:
            checks['editor_started'] = False
        else:
            with open(mcp_config_path, 'w', encoding='utf-8') as f:
                json.dump({'mcpServers': {'nodetocode': {'type': 'http', 'url': 'http://127.0.0.1:%d/mcp' % port}}}, f)
            run = run_cli(PROMPT % {'folder': FOLDER, 'module': module}, mcp_config_path, args.cli_model, args.timeout,
                          os.path.join(log_dir, 'live_migration_plan_0323_%s_cli.jsonl' % args.label))
            report['cli'] = {'calls': [(c['tool'], c['input'], c['error']) for c in run['calls']],
                             'answer': run['answer'], 'exit': run['exit'], 'seconds': run['seconds'], 'final': run['final']}
            # Claude Code may call a deferred tool before loading its schema (ToolSearch) and guess an argument name;
            # that call is refused with INVALID_ARGUMENT and retried
            ours = [c for c in run['calls'] if c['tool'] != 'ToolSearch']
            calls = [c for c in ours if c['tool'] == 'create-migration-plan']
            report['refused_guesses'] = [(c['input'], c['text'][:200]) for c in ours if c['error']]
            checks['the agent called create-migration-plan with the folder and module'] = bool(calls) and \
                not calls[-1]['error'] and FOLDER in (calls[-1]['input'].get('paths') or []) and \
                calls[-1]['input'].get('targetModule') == module
            checks['every failed call was a refused argument guess'] = all(
                'INVALID_ARGUMENT' in c['text'] for c in ours if c['error'])
            checks['the agent used no other NodeToCode tool'] = all(c['tool'] == 'create-migration-plan' for c in ours)
            checks['one plan was written'] = sum(1 for c in calls if not c['error']) == 1
            body = strip_mark(calls[-1]['body']) if calls else {}
            report['result'] = {k: body.get(k) for k in ('planId', 'status', 'planDir', 'stepCount', 'riskCounts', 'warnings')}
            checks['result: marked untrusted first'] = bool(calls) and calls[-1]['text'].lstrip().startswith('{"untrusted_content"')
            steps = body.get('steps') or []
            checks['result: a draft with seven steps, step 5 the cycle {B, C}'] = body.get('status') == 'draft' and \
                body.get('stepCount') == 7 and len(steps) == 7 and steps[4].get('kind') == 'cycle-group' and \
                steps[4].get('assets') == CYCLE
            plan = {}
            plan_dir = body.get('planDir') or ''
            if body.get('planJson') and os.path.isfile(body['planJson']):
                with open(body['planJson'], 'rb') as f:
                    plan = json.loads(f.read().decode('utf-8'))
            checks['plan.json: a draft with the same steps, for the module'] = plan.get('status') == 'draft' and \
                [st.get('kind') for st in plan.get('steps') or []] == [st.get('kind') for st in steps] and \
                plan.get('targetModule') == module
            checks['plan folder: plan.json and plan.md only, under Saved/NodeToCode/Migration'] = bool(plan_dir) and \
                sorted(os.path.basename(p) for p in files_under(plan_dir)) == ['plan.json', 'plan.md'] and \
                os.path.normpath(plan_dir).split(os.sep)[-3:-1] == ['NodeToCode', 'Migration']
            checks['the host Source folder is unchanged'] = files_under(source_dir) == source_before
            answer = run['answer'] if isinstance(run['answer'], dict) else {}
            checks['the answer matches the result'] = answer.get('planId') == body.get('planId') and \
                answer.get('steps') == 7 and answer.get('cycleStep') == ['BP_N2CDep_B', 'BP_N2CDep_C']
            if body.get('planMd') and os.path.isfile(body['planMd']):
                with open(body['planMd'], encoding='utf-8') as f:
                    report['plan_md'] = f.read()[:3000]
            checks['editor_alive_at_end'] = s.health()
            if plan_dir and os.path.isdir(plan_dir):
                shutil.rmtree(plan_dir)

    report['restore_ok'] = s.restore_ok
    report['checks'] = checks
    report['ok'] = bool(checks) and all(checks.values())
    with open(report_path, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
    print(json.dumps({'checks': checks, 'ok': report['ok'], 'cli': report.get('cli', {}).get('calls')}, indent=1))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
