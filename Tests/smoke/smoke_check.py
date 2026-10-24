# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""Smoke check run by build.ps1 -Smoke: start the host editor headless once per MCP tool-registration mode and fail on
anything a healthy plugin would not do.

Stdlib only; build.ps1 runs it with the engine's own Python (`python -I -u`). By hand:

    python -I Tests/smoke/smoke_check.py --engine G:/UE/UE_5.8 \\
        --project "G:/Personal Projects/n2c/Hosts/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject" --label 5.8

Per mode (default, dynamic, python_only; set through the host's Config/DefaultNodeToCode.ini, which the session puts
back afterwards) it starts UnrealEditor-Cmd with -N2CMcp through Tests/mcp_probe/editor_session.py and fails on:
  * an error line in the editor log (any `Log<Category>: Error:`, so LogNodeToCode, LogPython, LogBlueprint compile
    errors and LogPluginManager among them), an ensure, an assertion or a crash, unless ALLOWLIST below names it;
  * a missing 'MCP HTTP server started on port <port>' line, no /mcp/health answer, or a /mcp/health answer from a
    process that is not this editor (mcp-transport-10: a failed bind still logs that the server started);
  * a tools/list count that differs from Tests/smoke/expected.json for the engine and mode;
  * any change to the content of the host's Config/*.ini files while the editor ran (hashes before and after;
    imp-auth-settings-module-10, auth-settings-module-14/15);
In the default mode, with the editor still up, it also fails on:
  * the in-editor check: run-python lists the plugin's N2C* types, opens an engine Blueprint that exists in UE 5.6-5.8,
    and json.loads get_focused_blueprint_json with node_count > 0;
  * any Tests/mcp_probe probe status (P1-P3, P5-P7 in this session, P4 in its own editor) that differs from
    Tests/mcp_probe/expected.json;
  * the official MCP conformance suite and the MCP Inspector CLI (`--method tools/list`), both pinned, through
    Tests/conformance/run_conformance.py: a scenario listed as passing in Tests/conformance/baseline.json that fails,
    or a failure the baseline does not list. Needs Node.js (npx). Results: <log-dir>/<log-prefix>_conformance/.
Failures already present at a commit are listed in expected.json under known_failures with their finding ids; a
fix card removes its entry. Prints one PASS/FAIL line per check, a summary line, and N2C_SMOKE PASS or FAIL.
"""
import argparse
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE_DIR = os.path.join(os.path.dirname(HERE), 'mcp_probe')
sys.path.insert(0, PROBE_DIR)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'conformance'))
import run_conformance  # noqa: E402
from editor_session import EditorSession, free_port  # noqa: E402
import mcp_probe  # noqa: E402
from e05_regressions import rpc, run_python  # noqa: E402

SETTINGS_SECTION = '[/Script/NodeToCode.N2CSettings]'
# Registration modes and the settings each writes to the host's Config/DefaultNodeToCode.ini (default writes nothing,
# so it also checks the plugin's own defaults)
MODES = [
    ('default', {}),
    ('dynamic', {'bEnableDynamicToolDiscovery': 'True', 'bEnablePythonScriptOnlyMode': 'False'}),
    ('python_only', {'bEnablePythonScriptOnlyMode': 'True', 'bEnableDynamicToolDiscovery': 'False'}),
]

# An engine Blueprint present in UE 5.6, 5.7 and 5.8, and a graph of it with nodes (its EventGraph is empty)
ENGINE_BLUEPRINT = '/Engine/EngineSky/BP_Sky_Sphere'
ENGINE_GRAPH = 'UserConstructionScript'

# Log lines that fail the smoke step
FAIL_PATTERNS = [
    ('error', re.compile(r'\bLog\w+: Error: ')),
    ('ensure', re.compile(r'Ensure condition failed|=== Handled ensure')),
    ('assertion', re.compile(r'Assertion failed')),
    ('crash', re.compile(r'Fatal error|Unhandled Exception|LogNodeToCode: Fatal|=== Critical error ===')),
    ('plugin load', re.compile(r'LogPluginManager: (Error|Warning).*NodeToCode|Unable to load module.*NodeToCode|'
                               r'Plugin \'NodeToCode\' failed')),
]

# Known-benign lines that match FAIL_PATTERNS. Each entry: (regex, sessions it applies to, why it is benign). Keep it
# narrow: match the specific message, never a whole category.
ALLOWLIST = [
    # Empty since card 0112. P4 holds the MCP port with a plain socket, but NodeToCode now checks a port with its own
    # bind before handing it to HTTPServer and moves on to the next one (a warning, not an error), so no
    # "LogHttpListener: Error: HttpListener unable to bind" line appears there any more. That line in any session,
    # P4 included, is now a real failure: HTTPServer was given a port someone else holds.
]


def ts():
    return time.strftime('%H:%M:%S')


def say(msg):
    print(msg, flush=True)


# ---------------------------------------------------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------------------------------------------------

def config_hashes(project):
    """{relative path: sha256} for every .ini under the host's Config folder"""
    root = os.path.join(os.path.dirname(os.path.abspath(project)), 'Config')
    out = {}
    for path in sorted(glob.glob(os.path.join(root, '**', '*.ini'), recursive=True)):
        with open(path, 'rb') as f:
            out[os.path.relpath(path, root).replace('\\', '/')] = hashlib.sha256(f.read()).hexdigest()
    return out


def config_diff(before, after):
    return sorted([k for k in before if after.get(k) != before[k]] + [k for k in after if k not in before])


def strip_prefix(line):
    """Drop the '[timestamp][frame]' prefix of an editor log line"""
    return re.sub(r'^\[[^\]]*\]\[[^\]]*\]', '', line).strip()


def scan_log(text, session):
    """(failures, allowed): lines that match FAIL_PATTERNS, split by whether ALLOWLIST covers them in this session"""
    failures, allowed = [], []
    for raw in text.splitlines():
        line = strip_prefix(raw)
        kinds = [k for k, rx in FAIL_PATTERNS if rx.search(line)]
        if not kinds:
            continue
        why = next((w for rx, where, w in ALLOWLIST if session in where and rx.search(line)), None)
        (allowed if why else failures).append('%s: %s' % (kinds[0], line[:300]))
    return failures, allowed


def listener_pids(port):
    """PIDs listening on TCP port (Windows netstat), or None where that cannot be read"""
    if os.name != 'nt':
        return None
    try:
        out = subprocess.run(['netstat', '-ano', '-p', 'TCP'], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    pids = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 5 and parts[0] == 'TCP' and parts[3] == 'LISTENING' and parts[1].endswith(':%d' % port):
            pids.add(int(parts[4]))
    return pids


def tools_count(url):
    resp = rpc(url, 'tools/list')
    tools = (resp.get('result') or {}).get('tools')
    return len(tools) if isinstance(tools, list) else None


IN_EDITOR_PY = r'''
import json, unreal
classes = sorted(c for c in dir(unreal) if c.startswith("N2C"))
opened = json.loads(unreal.N2CPythonBridge.open_blueprint("%(bp)s", "%(graph)s"))
raw = unreal.N2CPythonBridge.get_focused_blueprint_json()
try:
    parsed = json.loads(raw)
    data = parsed.get("data") or {}
    node_count = data.get("node_count")
    n2c_ok = isinstance(data.get("n2c_json"), dict)
    error = parsed.get("error")
except ValueError as e:
    parsed, node_count, n2c_ok, error = None, None, False, "json.loads failed: %%s" %% e
bp = unreal.load_asset("%(bp)s")
if bp:
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(bp)
result = {"classes": len(classes), "opened": bool(opened.get("success")), "parsed": parsed is not None,
          "node_count": node_count, "n2c_json": n2c_ok, "error": error, "raw_head": raw[:200]}
'''


# ---------------------------------------------------------------------------------------------------------------------
# Sessions
# ---------------------------------------------------------------------------------------------------------------------

class Smoke:
    def __init__(self, args):
        self.args = args
        self.label = args.label
        expected = json.load(open(os.path.join(HERE, 'expected.json'), encoding='utf-8'))
        self.expected_tools = expected.get('tool_counts', {}).get(self.label, {})
        self.known = expected.get('known_failures', {}).get(self.label, [])
        self.failures = []
        self.summary = {'tools': {}, 'probes': {}, 'config': 'unchanged', 'in_editor': None, 'durations': {},
                        'conformance': 'not run'}

    def check(self, ok, name, detail=''):
        """Record one check; a failure that matches a known_failures entry is reported as KNOWN and does not fail"""
        if ok:
            say('PASS %s%s' % (name, ('  ' + detail) if detail else ''))
            return True
        known = next((k for k in self.known if k.get('check') == name), None)
        if known:
            say('KNOWN %s  %s (%s)' % (name, detail, ', '.join(known.get('ids', []))))
            return True
        say('FAIL %s  %s' % (name, detail))
        self.failures.append('%s: %s' % (name, detail))
        return False

    def log_path(self, name):
        return os.path.join(self.args.log_dir, '%s_%s.log' % (self.args.log_prefix, name))

    def run_mode(self, mode, settings):
        start = time.time()
        port = free_port(self.args.port)
        log = self.log_path(mode)
        extra = ['-RenderOffScreen'] if mode == 'default' else []
        # -RenderOffScreen: on UE 5.6 under -nullrhi an opened Blueprint editor's window reaches
        # FGenericWindow::GetRestoredDimensions, which is Fatal
        session = EditorSession(self.args.engine, self.args.project, port, log, allow_python=True, extra_args=extra)
        ini = session.project_config_path()
        session.guard_file(ini)
        for key, value in settings.items():
            session.patch_file(ini, SETTINGS_SECTION, key, value)
        before = config_hashes(self.args.project)
        after = {}
        say('== %s mode (port %d, log %s) %s' % (mode, port, log, ts()))
        try:
            started = session.start()
            self.check(started, '%s: /mcp/health' % mode, 'answered' if started else 'no answer within 240 s')
            if started:
                url = 'http://127.0.0.1:%d/mcp' % port
                pids = listener_pids(port)
                if pids is not None:
                    self.check(pids == {session.proc.pid}, '%s: listener is this editor' % mode,
                               'port %d held by pid(s) %s, editor pid %d' % (port, sorted(pids), session.proc.pid))
                count = tools_count(url)
                self.summary['tools'][mode] = count
                want = self.expected_tools.get(mode)
                self.check(count == want, '%s: tools/list count' % mode, '%s (expected %s)' % (count, want))
                if mode == 'default':
                    self.in_editor(url)
                    self.probes(port, session.proc.pid)
                    if not self.args.skip_conformance:
                        self.conformance(url, count)
                self.quit(url, session, mode)
        finally:
            session.stop(before_restore=lambda: after.update(config_hashes(self.args.project)))
        self.after_session(mode, session.log_text(), port, before, after)
        self.check(session.restore_ok is not False, '%s: host config restored' % mode,
                   'put back' if session.restore_ok is not False else
                   'EditorSession could not put the guarded ini files back')
        self.summary['durations'][mode] = int(time.time() - start)

    def quit(self, url, session, mode):
        """Close the editor from inside (quit_editor through run-python), so its shutdown is logged and scanned too and
        stop() does not wait out WM_CLOSE, which UnrealEditor-Cmd ignores. Dynamic mode lists only
        assess-needed-tools, so it first enables the 'Python' category (the category the Python tools register under;
        assess-needed-tools does not advertise it), which also exercises that path."""
        if mode == 'dynamic':
            rpc(url, 'tools/call', {'name': 'assess-needed-tools', 'arguments': {'categories': ['Python']}})
        try:
            run_python(url, 'import unreal\nunreal.SystemLibrary.quit_editor()\nresult = {"quit": True}')
        except Exception:  # the editor may close the connection while it exits
            pass
        deadline = time.time() + 60
        while time.time() < deadline and session.proc.poll() is None:
            time.sleep(0.5)
        code = session.proc.poll()
        self.check(code == 0, '%s: editor exits cleanly' % mode,
                   'exit code %s' % code if code is not None else 'still running 60 s after quit_editor')

    def after_session(self, session_name, text, port, before, after):
        if port is not None:
            line = 'MCP HTTP server started on port %d' % port
            self.check(line in text, '%s: server start line' % session_name, repr(line))
        failures, allowed = scan_log(text, session_name)
        for a in allowed:
            say('ALLOWED %s  %s' % (session_name, a))
        self.check(not failures, '%s: editor log' % session_name,
                   '%d bad line(s):\n      %s' % (len(failures), '\n      '.join(failures[:20])) if failures else
                   'clean')
        changed = config_diff(before, after)
        if changed:
            self.summary['config'] = 'changed: ' + ', '.join(changed)
        self.check(not changed, '%s: host Config/*.ini unchanged' % session_name,
                   'changed: %s' % ', '.join(changed) if changed else '%d files' % len(before))

    def in_editor(self, url):
        data, raw = run_python(url, IN_EDITOR_PY % {'bp': ENGINE_BLUEPRINT, 'graph': ENGINE_GRAPH})
        self.summary['in_editor'] = data.get('node_count')
        self.check(data.get('classes', 0) > 0, 'default: N2C types in Python', '%s classes' % data.get('classes'))
        ok = data.get('opened') and data.get('parsed') and data.get('n2c_json') and (data.get('node_count') or 0) > 0
        self.check(bool(ok), 'default: get_focused_blueprint_json on %s:%s' % (ENGINE_BLUEPRINT, ENGINE_GRAPH),
                   'node_count=%s' % data.get('node_count') if ok else 'result %s / %s' % (json.dumps(data)[:300], raw))

    def probes(self, port, pid):
        saved = os.path.join(os.path.dirname(os.path.abspath(self.args.project)), 'Saved')
        results = mcp_probe.session_probes(port, pid, saved)
        self.report_probes(results)

    def report_probes(self, results):
        diff = mcp_probe.mismatches(results, self.label)
        for k, v in sorted(results.items()):
            self.summary['probes'][k] = v['status']
            self.check(k not in diff, 'probe %s' % k, '%s (expected %s; ids %s)' % (
                v['status'], diff[k]['expected'] if k in diff else v['status'], ', '.join(v.get('ids', []))))

    def conformance(self, url, tool_count):
        """Pinned MCP conformance suite plus Inspector CLI tools/list, compared with Tests/conformance/baseline.json"""
        start = time.time()
        out_dir = os.path.join(self.args.log_dir, '%s_conformance' % self.args.log_prefix)
        say('== conformance suite and Inspector CLI (results %s) %s' % (out_dir, ts()))
        result = run_conformance.run(url, out_dir, self.label, expected_tools=tool_count)
        lines, failures, counts = run_conformance.evaluate(result, self.label)
        for kind, name, detail in lines:
            if kind == 'FAIL':
                self.check(False, name, detail)
            else:
                say('%s %s  %s' % (kind, name, detail))
        tally = {}
        for kind, _, _ in lines:
            tally[kind] = tally.get(kind, 0) + 1
        insp = result.get('inspector') or {}
        self.summary['conformance'] = '%d pass %d fail (known %d, n/a %d, now passing %d), inspector %s' % (
            counts['pass'], counts['fail'], sum(1 for k, n, _ in lines if k == 'KNOWN' and n.startswith('conformance')),
            tally.get('N/A', 0), tally.get('NOW PASSES', 0) + tally.get('NEW', 0),
            ('%s tools' % insp['tools']) if insp.get('tools') is not None else
            'known' if any(k == 'KNOWN' and n == run_conformance.INSPECTOR_CHECK for k, n, _ in lines) else 'fail')
        say('CONFORMANCE UE %s: %s; versions %s; %s' % (self.label, self.summary['conformance'],
                                                        json.dumps(result.get('versions')),
                                                        json.dumps(result.get('durations'))))
        self.summary['durations']['conformance'] = int(time.time() - start)

    def run_p4(self):
        start = time.time()
        say('== P4 (MCP port held by another process) %s' % ts())
        before = config_hashes(self.args.project)
        log_dir = os.path.join(self.args.log_dir, self.args.log_prefix + '_p4')
        os.makedirs(log_dir, exist_ok=True)
        args = types.SimpleNamespace(engine=self.args.engine, project=self.args.project)
        result = mcp_probe.probe_p4(args, free_port(self.args.port), log_dir)
        after = config_hashes(self.args.project)
        self.report_probes({'P4': result})
        text = open(os.path.join(log_dir, 'p4_editor.log'), 'rb').read().decode('utf-8', 'replace')
        self.after_session('p4', text, None, before, after)
        self.summary['durations']['p4'] = int(time.time() - start)


def main():
    ap = argparse.ArgumentParser(description='NodeToCode smoke check (build.ps1 -Smoke)')
    ap.add_argument('--engine', required=True)
    ap.add_argument('--project', required=True)
    ap.add_argument('--label', required=True, help='engine label in the expected files, e.g. 5.8')
    ap.add_argument('--port', type=int, default=27170)
    ap.add_argument('--log-dir', default=os.path.join(PROBE_DIR, 'logs'))
    ap.add_argument('--log-prefix', default=None, help='editor log name prefix (default Smoke_<label>)')
    ap.add_argument('--modes', nargs='*', default=[m for m, _ in MODES], help='registration modes to run')
    ap.add_argument('--skip-p4', action='store_true', help='skip P4 (its own editor launch)')
    ap.add_argument('--skip-conformance', action='store_true',
                    help='skip the MCP conformance suite and Inspector CLI (they need Node.js and npm registry access)')
    args = ap.parse_args()
    args.log_prefix = args.log_prefix or 'Smoke_%s' % args.label
    os.makedirs(args.log_dir, exist_ok=True)

    smoke = Smoke(args)
    t0 = time.time()
    for mode, settings in MODES:
        if mode in args.modes:
            smoke.run_mode(mode, settings)
    if args.skip_p4:
        smoke.summary['probes']['P4'] = 'skipped'
    else:
        smoke.run_p4()
    s = smoke.summary
    tools = ' '.join('%s=%s' % (m, s['tools'].get(m)) for m in args.modes)
    probes = ' '.join('%s=%s' % (k, v) for k, v in sorted(s['probes'].items()))
    say('N2C_SMOKE SUMMARY UE %s: tools %s; probes %s; conformance %s; config %s; focused BP nodes %s; %ds' % (
        args.label, tools, probes, s['conformance'], s['config'], s['in_editor'], int(time.time() - t0)))
    if smoke.failures:
        say('N2C_SMOKE FAIL (%d): %s' % (len(smoke.failures), ' | '.join(f.split('\n')[0] for f in smoke.failures)))
        return 1
    say('N2C_SMOKE PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
