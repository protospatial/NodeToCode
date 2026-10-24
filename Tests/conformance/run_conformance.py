# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""Run the official MCP conformance suite and the MCP Inspector CLI against a running NodeToCode MCP server and compare
the results with Tests/conformance/baseline.json.

Stdlib only. Needs Node.js (npx) on PATH; both tools are fetched by npx at the exact versions baseline.json pins.
smoke_check.py calls run() in the default-mode smoke session (build.ps1 -Smoke); by hand, against an editor started
with -N2CMcp (Tests/mcp_probe/editor_session.py):

    python -I Tests/conformance/run_conformance.py --url http://127.0.0.1:27000/mcp --out-dir <folder> --label 5.8

What runs:
  * npx -y @modelcontextprotocol/conformance@<pin> server --url <url> --requirements 2025-11-25 -o <out>/conformance
    (the scenarios the 2025-11-25 spec revision requires, frozen at its release, plus the not-yet-scored ones the
    suite runs with them). Each scenario writes <out>/conformance/server-<scenario>-<time>/checks.json.
  * npx -y @modelcontextprotocol/inspector@<pin> --cli <url> --method tools/list, stdout saved as
    <out>/inspector_tools_list.json; it passes when it returns a tools array of the expected length.
The negative Origin case is not repeated here: probe P1 (Tests/mcp_probe) covers it, and the suite's
dns-rebinding-protection scenario sends a foreign Host and Origin too.

Classification (baseline.json): a scenario passes when none of its checks is FAILURE. Every scenario is listed once:
  * pass: passes today. A failure fails the smoke step.
  * known_failures: fails today on a defect a later card fixes ({card, checks, why}). Fails the smoke step only if a
    check outside `checks` fails. When it passes, a NOW PASSES line asks the owning card to move it to pass.
  * not_applicable: the scenario needs something NodeToCode does not offer (the suite's fixture tools, prompts and
    resources, a capability the server does not advertise), {checks, why}. Same rule as known_failures.
  * a scenario in none of the lists (a new suite version) fails the smoke step when it fails and prints a reminder
    when it passes, so it gets classified.
known_failures['inspector tools/list'] may hold {card, match, why}: an Inspector error whose text matches `match` is
known; any other error fails. WARNING checks are printed (with the owning card when known_warnings names it) and do
not fail.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(HERE, 'baseline.json')
INSPECTOR_CHECK = 'inspector tools/list'


def load_baseline(path=BASELINE):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def find_npx():
    return shutil.which('npx') or shutil.which('npx.cmd')


def npx_env():
    env = dict(os.environ)
    env.update({'npm_config_yes': 'true', 'npm_config_update_notifier': 'false', 'npm_config_fund': 'false',
                'npm_config_audit': 'false', 'NO_COLOR': '1', 'FORCE_COLOR': '0'})
    return env


def run_npx(npx, args, cwd, timeout):
    """(exit code or None on timeout, stdout, stderr)"""
    try:
        p = subprocess.run([npx, '-y'] + args, cwd=cwd, env=npx_env(), capture_output=True, timeout=timeout,
                           stdin=subprocess.DEVNULL)
        return p.returncode, p.stdout.decode('utf-8', 'replace'), p.stderr.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        out = (e.stdout or b'').decode('utf-8', 'replace')
        err = (e.stderr or b'').decode('utf-8', 'replace')
        return None, out, err + '\n[timed out after %d s]' % timeout


def scenario_name(dir_name):
    """server-<scenario>-2026-10-24T09-32-55-173Z -> <scenario>"""
    m = re.match(r'^server-(.+)-\d{4}-\d{2}-\d{2}T[\d-]+Z$', dir_name)
    return m.group(1) if m else None


def read_scenarios(conf_dir):
    """{scenario: {'failed': [check ids], 'warnings': [(id, message)], 'passed': n, 'messages': {id: text}}}"""
    out = {}
    for path in sorted(glob.glob(os.path.join(conf_dir, 'server-*', 'checks.json'))):
        name = scenario_name(os.path.basename(os.path.dirname(path)))
        if not name:
            continue
        try:
            with open(path, encoding='utf-8') as f:
                checks = json.load(f)
        except (OSError, ValueError) as e:
            out[name] = {'failed': ['<unreadable checks.json>'], 'warnings': [], 'passed': 0,
                         'messages': {'<unreadable checks.json>': str(e)}}
            continue
        entry = {'failed': [], 'warnings': [], 'passed': 0, 'messages': {}}
        for c in checks if isinstance(checks, list) else []:
            status, cid = c.get('status'), c.get('id', '?')
            msg = (c.get('errorMessage') or c.get('description') or '').strip()
            if status == 'SUCCESS':
                entry['passed'] += 1
            elif status == 'FAILURE':
                entry['failed'].append(cid)
                entry['messages'][cid] = msg
            elif status == 'WARNING':
                entry['warnings'].append((cid, msg))
        out[name] = entry
    return out


def run(url, out_dir, label, expected_tools=None, baseline=None, timeout=300):
    """Run both tools against url. Returns {'conformance': {...}, 'inspector': {...}, 'durations': {...}, 'error': str}
    and writes <out_dir>/conformance_results.json."""
    baseline = baseline or load_baseline()
    os.makedirs(out_dir, exist_ok=True)
    result = {'url': url, 'label': label, 'conformance': {}, 'inspector': {}, 'durations': {}, 'error': None,
              'versions': {'conformance': baseline['conformance']['package'],
                           'inspector': baseline['inspector']['package']}}
    npx = find_npx()
    if not npx:
        result['error'] = 'npx not found on PATH (Node.js is required for the smoke step, see AGENTS.md)'
        return result
    node_v = node_version()
    result['versions']['node'] = node_v

    conf_dir = os.path.join(out_dir, 'conformance')
    if os.path.isdir(conf_dir):
        shutil.rmtree(conf_dir, ignore_errors=True)
    t = time.time()
    code, out, err = run_npx(npx, [baseline['conformance']['package']] + baseline['conformance']['args'] +
                             ['--url', url, '-o', conf_dir], out_dir, timeout)
    result['durations']['conformance'] = round(time.time() - t, 1)
    with open(os.path.join(out_dir, 'conformance_stdout.txt'), 'w', encoding='utf-8') as f:
        f.write(out + '\n--- stderr ---\n' + err)
    result['conformance'] = {'exit': code, 'scenarios': read_scenarios(conf_dir)}
    if not result['conformance']['scenarios']:
        result['error'] = 'conformance suite produced no results (exit %s): %s' % (code, (err or out)[-400:].strip())

    t = time.time()
    code, out, err = run_npx(npx, [baseline['inspector']['package'], '--cli', url, '--method', 'tools/list'],
                             out_dir, 120)
    result['durations']['inspector'] = round(time.time() - t, 1)
    with open(os.path.join(out_dir, 'inspector_tools_list.json'), 'w', encoding='utf-8') as f:
        f.write(out)
    with open(os.path.join(out_dir, 'inspector_stderr.txt'), 'w', encoding='utf-8') as f:
        f.write(err)
    insp = {'exit': code, 'tools': None, 'error': None, 'expected_tools': expected_tools}
    data = parse_json(out)
    if data is None:
        # The CLI prints a failure as one JSON line on stderr ({"error": {...}})
        data = next((d for d in map(parse_json, err.splitlines()) if isinstance(d, dict) and 'error' in d), None)
    if isinstance(data, dict) and isinstance(data.get('tools'), list):
        insp['tools'] = len(data['tools'])
    elif isinstance(data, dict) and data.get('error'):
        e = data['error']
        insp['error'] = e.get('message', json.dumps(e)) if isinstance(e, dict) else str(e)
    else:
        insp['error'] = 'no JSON tools/list result (exit %s): %s' % (code, (err or out)[-300:].strip())
    result['inspector'] = insp

    with open(os.path.join(out_dir, 'conformance_results.json'), 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=1)
    return result


def parse_json(text):
    try:
        return json.loads(text.strip()) if text.strip() else None
    except ValueError:
        return None


def node_version():
    node = shutil.which('node')
    try:
        p = subprocess.run([node, '--version'], capture_output=True, text=True, timeout=30) if node else None
        return p.stdout.strip() if p else None
    except (OSError, subprocess.SubprocessError):
        return None


def applies(entry, label):
    engines = entry.get('engines')
    return not engines or label in engines


def evaluate(result, label, baseline=None):
    """Compare a run() result with the baseline. Returns (lines, failures, counts): lines are (kind, name, detail) with
    kind PASS, FAIL, KNOWN, N/A, NOW PASSES, NEW, WARN; failures the FAIL names; counts the suite's raw scenario
    pass/fail numbers."""
    baseline = baseline or load_baseline()
    lines, failures = [], []

    def add(kind, name, detail=''):
        lines.append((kind, name, detail))
        if kind == 'FAIL':
            failures.append(name)

    if result.get('error'):
        add('FAIL', 'conformance run', result['error'])
    scenarios = result.get('conformance', {}).get('scenarios', {})
    passing = set(baseline.get('pass', []))
    known = {k: v for k, v in baseline.get('known_failures', {}).items() if k != INSPECTOR_CHECK and applies(v, label)}
    na = {k: v for k, v in baseline.get('not_applicable', {}).items() if applies(v, label)}
    warn_owner = baseline.get('known_warnings', {})
    counts = {'pass': 0, 'fail': 0}
    for name in sorted(scenarios):
        s = scenarios[name]
        failed = s['failed']
        counts['fail' if failed else 'pass'] += 1
        cname = 'conformance %s' % name
        for wid, wmsg in s['warnings']:
            owner = warn_owner.get('%s/%s' % (name, wid), {})
            add('WARN', cname, '%s: %s%s' % (wid, wmsg[:200], ' (card %s)' % owner['card'] if owner else ''))
        first = ('%s: %s' % (failed[0], s['messages'].get(failed[0], '')[:200])) if failed else ''
        if name in passing:
            if failed:
                add('FAIL', cname, '%d failed check(s); %s' % (len(failed), first))
            else:
                add('PASS', cname, '%d check(s)' % s['passed'])
            continue
        entry, kind = (known[name], 'KNOWN') if name in known else (na[name], 'N/A') if name in na else (None, None)
        if entry is None:
            if failed:
                add('FAIL', cname, 'not in baseline.json and fails: %s' % first)
            else:
                add('NEW', cname, 'not in baseline.json and passes: add it to "pass"')
            continue
        unexpected = [c for c in failed if c not in entry.get('checks', [])]
        if unexpected:
            add('FAIL', cname, 'check(s) outside the %s entry failed: %s; %s' % (
                kind, ', '.join(unexpected), s['messages'].get(unexpected[0], '')[:200]))
        elif not failed:
            add('NOW PASSES', cname, 'listed under %s%s; move it to "pass" in Tests/conformance/baseline.json' % (
                'known_failures' if kind == 'KNOWN' else 'not_applicable',
                ' (card %s)' % entry['card'] if entry.get('card') else ''))
        else:
            add(kind, cname, ('card %s: ' % entry['card'] if entry.get('card') else '') + first)
    for name in sorted(passing - set(scenarios)):
        if scenarios or not result.get('error'):
            add('FAIL', 'conformance %s' % name, 'baseline scenario did not run')

    insp = result.get('inspector') or {}
    if insp:
        ik = baseline.get('known_failures', {}).get(INSPECTOR_CHECK)
        ik = ik if ik and applies(ik, label) else None
        want = insp.get('expected_tools')
        if insp.get('tools') is not None:
            ok = want is None or insp['tools'] == want
            if ok and ik:
                add('NOW PASSES', INSPECTOR_CHECK, '%d tools; remove its known_failures entry (card %s)' % (
                    insp['tools'], ik.get('card')))
            else:
                add('PASS' if ok else 'FAIL', INSPECTOR_CHECK, '%d tools (expected %s)' % (insp['tools'], want))
        elif ik and re.search(ik.get('match', '$^'), insp.get('error') or ''):
            add('KNOWN', INSPECTOR_CHECK, 'card %s: %s' % (ik.get('card'), insp['error'][:200]))
        else:
            add('FAIL', INSPECTOR_CHECK, insp.get('error') or 'no result')
    return lines, failures, counts


def main():
    ap = argparse.ArgumentParser(description='MCP conformance suite and Inspector CLI against a NodeToCode server')
    ap.add_argument('--url', required=True, help='e.g. http://127.0.0.1:27000/mcp')
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('--label', default='5.8', help='engine label for engine-specific baseline entries')
    ap.add_argument('--expected-tools', type=int, default=None, help='tools/list length the Inspector must report')
    args = ap.parse_args()
    result = run(args.url, args.out_dir, args.label, args.expected_tools)
    lines, failures, counts = evaluate(result, args.label)
    for kind, name, detail in lines:
        print('%s %s  %s' % (kind, name, detail))
    print('CONFORMANCE %s: %d scenario(s) pass, %d fail; %d smoke failure(s); %s' % (
        args.label, counts['pass'], counts['fail'], len(failures), json.dumps(result['durations'])))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
