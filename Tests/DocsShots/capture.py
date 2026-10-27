"""Capture the wiki's screenshots for one docs group (or all) from the UE 5.8 host editor.

Stdlib only; run from the repository root with `python -I`:

    python -I Tests/DocsShots/capture.py --group main-window
    python -I Tests/DocsShots/capture.py --group all [--engine G:/UE/UE_5.8] [--project <host>.uproject]

This is the `capture` command in `.agents/docs-parity.json` (card 0410). It starts UnrealEditor-Cmd on the 5.8 host
(build.ps1's `<checkout>/../Hosts/NodeToCodeHost_5_8` unless --project says otherwise) with a real RHI and no window
(-RenderOffScreen, never -nullrhi), runs the automation test N2CDocs.Shots.<group>
(Source/Private/Tests/Docs/N2CDocsShots.cpp), which draws NodeToCode's widgets from fixture data at 2x into
<host>/Saved/DocsShots/<group>/<shot>.png, then replaces wiki/images/<group>/ with those files and records each shot in
wiki/images/shots.json with the commit checked out (`git rev-parse HEAD`), which `tools/docs_check.py --stale` reads.
No provider key reaches the editor: the provider variables are removed from its environment, and the settings shot
refuses to capture while a key is saved for NodeToCode. The editor's MCP server does not start (-unattended without
-N2CMcp). Build the host first (`build.ps1 -UEVersion 5.8`). Look at every picture it wrote before committing them.

    python -I Tests/DocsShots/capture.py --restamp

starts no editor: it sets each shot's commit in shots.json to the last commit that changed its image file. Run it after a
squash merge, which drops the branch commit the shots were taken at (docs_check.py cannot diff against a commit it does
not have, so it would never call those shots stale), and commit the manifest.
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
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, 'Tests', 'mcp_probe'))
from editor_session import DDC_ARGS, STARTUP_ENV, find_editor, kill_with_parent  # noqa: E402

GROUPS = ('main-window', 'translate-entry', 'settings')
PROVIDER_VARIABLES = ('ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'GEMINI_API_KEY', 'GOOGLE_API_KEY', 'DEEPSEEK_API_KEY')
IMAGES = os.path.join(REPO, 'wiki', 'images')
MANIFEST = os.path.join(IMAGES, 'shots.json')


def head_commit():
    return subprocess.run(['git', '-C', REPO, 'rev-parse', 'HEAD'], capture_output=True, text=True,
                          check=True).stdout.strip()


def run_editor(engine, project, test, out, log, timeout):
    exe = find_editor(engine)
    args = [exe, project, '-ExecCmds=Automation RunTests %s' % test, '-TestExit=Automation Test Queue Empty',
            '-unattended', '-RenderOffScreen', '-nosplash', '-nosound', '-NoLiveCoding', '-culture=en',
            '-N2CDocsShots=%s' % out, '-abslog=%s' % log] + list(DDC_ARGS)
    env = {k: v for k, v in os.environ.items() if k not in PROVIDER_VARIABLES}
    env.update(STARTUP_ENV)
    proc = subprocess.Popen(args, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    job = kill_with_parent(proc)  # noqa: F841 (kept referenced: the editor dies with this script)
    try:
        return proc.wait(timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(30)
        return None


def test_results(log):
    """{test name: Success/Fail} from the editor log's 'Test Completed' lines"""
    text = open(log, 'rb').read().decode('utf-8', 'replace') if os.path.exists(log) else ''
    found = {}
    for m in re.finditer(r'Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\} Path=\{([^}]*)\}', text):
        found[m.group(3)] = m.group(1)
    errors = [l.strip()[:300] for l in text.splitlines() if 'LogAutomationController: Error' in l][:10]
    return found, errors


def restamp():
    """shots.json: each shot's commit becomes the last commit that changed its image file"""
    manifest = json.load(open(MANIFEST, encoding='utf-8'))
    changed = {}
    for name, entry in manifest.get('shots', {}).items():
        path = 'wiki/images/%s' % name
        sha = subprocess.run(['git', '-C', REPO, 'log', '-1', '--format=%H', '--', path], capture_output=True,
                             text=True).stdout.strip()
        if sha and entry.get('commit') != sha:
            changed[name] = {'from': entry.get('commit'), 'to': sha}
            entry['commit'] = sha
    with open(MANIFEST, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump(manifest, fh, indent=2)
        fh.write('\n')
    print(json.dumps({'restamped': changed}, indent=1))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--group', choices=GROUPS + ('all',))
    ap.add_argument('--restamp', action='store_true', help='set each shot commit to the last commit of its image file')
    ap.add_argument('--engine', default=os.environ.get('N2C_UE58', 'G:/UE/UE_5.8'))
    ap.add_argument('--project', default=os.path.join(os.path.dirname(REPO), 'Hosts', 'NodeToCodeHost_5_8',
                                                      'NodeToCodeHost_5_8.uproject'))
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()
    if args.restamp:
        return restamp()
    if not args.group:
        ap.error('--group is required')
    project = os.path.abspath(args.project)
    if not os.path.isfile(project):
        raise SystemExit('no host project at %s (build.ps1 -UEVersion 5.8 makes it, or pass --project)' % project)
    groups = list(GROUPS) if args.group == 'all' else [args.group]
    test = 'N2CDocs.Shots' if args.group == 'all' else 'N2CDocs.Shots.%s' % args.group
    out = os.path.join(os.path.dirname(project), 'Saved', 'DocsShots')
    for g in groups:
        shutil.rmtree(os.path.join(out, g), ignore_errors=True)
    log = os.path.join(out, 'capture_%s.log' % args.group)
    os.makedirs(out, exist_ok=True)
    commit = head_commit()

    t0 = time.time()
    code = run_editor(args.engine, project, test, out, log, args.timeout)
    results, errors = test_results(log)
    report = {'group': args.group, 'commit': commit, 'exit': code, 'seconds': round(time.time() - t0, 1),
              'tests': results, 'errors': errors, 'log': log, 'shots': []}
    expected = ['N2CDocs.Shots.%s' % g for g in groups]
    ok = code is not None and all(results.get(t) == 'Success' for t in expected)

    if ok:
        manifest = json.load(open(MANIFEST, encoding='utf-8')) if os.path.exists(MANIFEST) else {}
        shots = manifest.setdefault('shots', {})
        for g in groups:
            src = os.path.join(out, g)
            files = sorted(f for f in os.listdir(src) if f.endswith('.png')) if os.path.isdir(src) else []
            if not files:
                ok = False
                report['errors'].append('group %s wrote no shots' % g)
                continue
            dst = os.path.join(IMAGES, g)
            if os.path.isdir(dst):
                for f in os.listdir(dst):
                    if f.endswith('.png'):
                        os.remove(os.path.join(dst, f))
                        shots.pop('%s/%s' % (g, f), None)
            os.makedirs(dst, exist_ok=True)
            for f in files:
                shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
                shots['%s/%s' % (g, f)] = {'commit': commit}
                report['shots'].append('wiki/images/%s/%s' % (g, f))
        manifest['shots'] = dict(sorted(shots.items()))
        with open(MANIFEST, 'w', encoding='utf-8', newline='\n') as fh:
            json.dump(manifest, fh, indent=2)
            fh.write('\n')
    report['ok'] = ok
    print(json.dumps(report, indent=1))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
