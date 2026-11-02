"""Capture the wiki's screenshots for one docs group (or all) from the UE 5.8 host editor.

Stdlib only; run from the repository root with `python -I`:

    python -I Tests/DocsShots/capture.py --group main-window
    python -I Tests/DocsShots/capture.py --group all [--engine <UE_5.8 folder>] [--project <host>.uproject]
    python -I Tests/DocsShots/capture.py --which-engine

The 5.8 engine is found as build.ps1 finds it: --engine, then $env:N2C_UE_5_8 (or the older N2C_UE58), then the '5.8'
entry of $Engines in build.local.ps1, then the installs ue_python_finder.py lists (registry, the launcher's
LauncherInstalled.dat, the usual install folders). Each candidate must be a UE 5.8 install by its
Engine/Build/Build.version; an explicit setting that is not one is an error, not a reason to try the next.
--which-engine prints the engine it would use and starts nothing.

This is the `capture` command in `.agents/docs-parity.json` (card 0410). It starts UnrealEditor-Cmd on the 5.8 host
(build.ps1's `<checkout>/../Hosts/NodeToCodeHost_5_8` unless --project says otherwise) with a real RHI and no window
(-RenderOffScreen, never -nullrhi), runs the automation test N2CDocs.Shots.<group>
(Source/Private/Tests/Docs/N2CDocsShots.cpp), which draws NodeToCode's widgets from fixture data at 2x into
<host>/Saved/DocsShots/<group>/<shot>.png, then replaces wiki/images/<group>/ with those files and records each shot in
wiki/images/shots.json with the commit checked out (`git rev-parse HEAD`), which `tools/docs_check.py --stale` reads.
No provider key reaches the editor: the provider variables are removed from its environment, and the settings shot
refuses to capture while a key is saved for NodeToCode. The editor's MCP server does not start (-unattended without
-N2CMcp). Build the host first (`build.ps1 -UEVersion 5.8`). Look at every picture it wrote before committing them.

Every shot of a retaken group is stamped with that commit, also when its picture comes out byte-identical (the
fixtures are deterministic, so that is the usual case): the stamp says the shot was checked against the code at that
commit, which is what `--stale` compares. Commit the manifest even when no image changed. After a squash or rebase merge
the stamp needs no fixing: `--stale` uses the commit that wrote it into shots.json (card 0517).

`--restamp` is kept only so old scripts get an explanation: it changes nothing (card 0519). It used to set each stamp to
its image's last commit, which after a byte-identical retake moved the stamp back before the code change and made the
shot stale again (seen in #262).
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
sys.path.insert(0, os.path.join(REPO, 'Content', 'Python', 'mcp_bridge'))
from ue_python_finder import get_ue_installations, read_build_version  # noqa: E402

ENGINE_VERSION = (5, 8)

GROUPS = ('main-window', 'translate-entry', 'settings')
PROVIDER_VARIABLES = ('ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'GEMINI_API_KEY', 'GOOGLE_API_KEY', 'DEEPSEEK_API_KEY')
IMAGES = os.path.join(REPO, 'wiki', 'images')
MANIFEST = os.path.join(IMAGES, 'shots.json')


def head_commit():
    return subprocess.run(['git', '-C', REPO, 'rev-parse', 'HEAD'], capture_output=True, text=True,
                          check=True).stdout.strip()


def find_engine(explicit):
    """(engine root, where it came from) for UE 5.8, in build.ps1's order; SystemExit with the reason when none is found"""
    def is_58(path):
        return bool(path) and (read_build_version(path) or (0, 0))[:2] == ENGINE_VERSION

    local = os.path.join(REPO, 'build.local.ps1')
    from_local = None
    if os.path.isfile(local):
        m = re.search(r"['\"]?5\.8['\"]?\s*=\s*['\"]([^'\"]+)['\"]", open(local, encoding='utf-8-sig').read())
        from_local = m.group(1) if m else None
    for source, path in (('--engine', explicit), ('$env:N2C_UE_5_8', os.environ.get('N2C_UE_5_8')),
                         ('$env:N2C_UE58', os.environ.get('N2C_UE58')), ('build.local.ps1', from_local)):
        if not path:
            continue
        path = path.strip().strip('"').rstrip('\\/')
        if not is_58(path):
            raise SystemExit("%s '%s' is not a UE 5.8 install (no Engine/Build/Build.version saying 5.8)" % (source, path))
        return path, source
    for version, path in get_ue_installations():
        if version[:2] == ENGINE_VERSION and is_58(path):
            return path, 'installed engines (registry, LauncherInstalled.dat, install folders)'
    raise SystemExit('no UE 5.8 install found: pass --engine <folder that holds Engine/>, set N2C_UE_5_8, or add '
                     "'5.8' to $Engines in build.local.ps1")


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


RESTAMP_RETIRED = ('--restamp changes nothing any more (card 0519). A stamp from a squashed-away branch commit needs no '
                   'fixing: docs_check.py --stale uses the commit that wrote it into shots.json (card 0517). Setting '
                   "stamps to each image's last commit undid byte-identical retakes and made their shots stale again. "
                   'To refresh a stale shot, retake its group: capture.py --group <group>.')


def restamp():
    """Retired (card 0519): explains why and leaves shots.json as it is."""
    print(json.dumps({'restamped': {}, 'note': RESTAMP_RETIRED}, indent=1))
    return 0


def record_shots(groups, out, commit, images=None, manifest_path=None):
    """Replace wiki/images/<group>/ with the PNGs the test wrote to <out>/<group>/ and stamp every one with commit.

    Every shot of the group gets the new stamp, byte-identical or not: the stamp records the commit the picture was
    checked at, so a retake that changes no pixel still refreshes it (card 0519). Shots the group no longer writes leave
    the manifest. Returns (ok, written repo paths, errors)."""
    images = images or IMAGES
    manifest_path = manifest_path or MANIFEST
    manifest = json.load(open(manifest_path, encoding='utf-8')) if os.path.exists(manifest_path) else {}
    shots = manifest.setdefault('shots', {})
    ok, written, errors = True, [], []
    for g in groups:
        src = os.path.join(out, g)
        files = sorted(f for f in os.listdir(src) if f.endswith('.png')) if os.path.isdir(src) else []
        if not files:
            ok = False
            errors.append('group %s wrote no shots' % g)
            continue
        dst = os.path.join(images, g)
        if os.path.isdir(dst):
            for f in os.listdir(dst):
                if f.endswith('.png'):
                    os.remove(os.path.join(dst, f))
                    shots.pop('%s/%s' % (g, f), None)
        os.makedirs(dst, exist_ok=True)
        for f in files:
            shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
            shots['%s/%s' % (g, f)] = {'commit': commit}
            written.append('wiki/images/%s/%s' % (g, f))
    manifest['shots'] = dict(sorted(shots.items()))
    with open(manifest_path, 'w', encoding='utf-8', newline='\n') as fh:
        json.dump(manifest, fh, indent=2)
        fh.write('\n')
    return ok, written, errors


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--group', choices=GROUPS + ('all',))
    ap.add_argument('--restamp', action='store_true',
                    help='retired (card 0519): changes nothing; prints why. Retake a group to refresh its stamps')
    ap.add_argument('--engine', help='UE 5.8 root, the folder that holds Engine/ (default: found as build.ps1 finds it)')
    ap.add_argument('--which-engine', action='store_true', help='print the UE 5.8 engine this would use and exit')
    ap.add_argument('--project', default=os.path.join(os.path.dirname(REPO), 'Hosts', 'NodeToCodeHost_5_8',
                                                      'NodeToCodeHost_5_8.uproject'))
    ap.add_argument('--timeout', type=int, default=900)
    args = ap.parse_args()
    if args.restamp:
        return restamp()
    if args.which_engine:
        engine, source = find_engine(args.engine)
        print(json.dumps({'engine': engine, 'source': source}))
        return 0
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
    engine, _ = find_engine(args.engine)

    t0 = time.time()
    code = run_editor(engine, project, test, out, log, args.timeout)
    results, errors = test_results(log)
    report = {'group': args.group, 'commit': commit, 'exit': code, 'seconds': round(time.time() - t0, 1),
              'tests': results, 'errors': errors, 'log': log, 'shots': []}
    expected = ['N2CDocs.Shots.%s' % g for g in groups]
    ok = code is not None and all(results.get(t) == 'Success' for t in expected)

    if ok:
        ok, report['shots'], more = record_shots(groups, out, commit)
        report['errors'] += more
    report['ok'] = ok
    print(json.dumps(report, indent=1))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
