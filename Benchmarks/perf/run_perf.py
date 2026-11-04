# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The performance and token baseline (card 0360): one command reruns the whole measurement.

    python -I Benchmarks/perf/run_perf.py [--engines 5.8 5.6] [--out Benchmarks/perf/<date>] [--skip-mcp]

Per engine (UE 5.8, and 5.6 for comparison), with the build.ps1 host at <checkout>/../Hosts/NodeToCodeHost_<x_y>:
1. A headless editor with -trace=cpu,memory runs capture_in_editor.py: card 0359's perf fixtures read whole (one
   warm-up, then five timed reads each), the agent and translation documents minified and pretty, working-set deltas,
   BP_N2CPerf_5k's F_Chain_0 at Max Translation Depth 0-5, and the tag search with every function graph tagged.
2. UnrealInsights exports the trace's timer statistics (TimingInsights.ExportTimerStatistics); the N2C_* rows go to
   <out>/<engine>/timers_n2c.csv (count, inclusive median/average/min/max per scope over every call in the session).
3. A second headless editor with -N2CMcp answers initialize and tools/list on a free port; the response's bytes and
   tokens are recorded (the default tool mode).
4. Benchmarks/common/tokens.py counts each document: chars/4 always, tiktoken o200k_base and the Qwen tokenizer only
   when already on this machine, Anthropic count_tokens when ANTHROPIC_API_KEY is in the checkout's .env.
Writes <out>/<engine>/editor_results.json, timers_n2c.csv, tokens.json and <out>/summary.json and summary.md (the
tables Context/translation-pipeline/performance-baseline.md carries). The trace files and captured documents stay
under BuildLogs/perf/<date> (large; not committed).

Run it with no other build or editor on the machine (aw lease cpu --cpu 24, coordinate skill): timings are wall clock.
Set N2C_PORT_OFFSET as for any lane session. Engines are found as Scripts/N2CEngines.ps1 does for the default install
(C:/Program Files/Epic Games/UE_<x.y>), or from N2C_UE_<x>_<y>.
"""
import argparse
import csv
import datetime
import io
import json
import os
import platform
import statistics
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'Benchmarks', 'common'))
sys.path.insert(0, os.path.join(REPO, 'Tests', 'mcp_probe'))

import tokens  # noqa: E402
from editor_session import EditorSession, DDC_ARGS, find_editor, free_port, kill_with_parent  # noqa: E402

FIXTURES = ('BP_N2CPerf_100', 'BP_N2CPerf_1k', 'BP_N2CPerf_5k')
FOLDER = '/NodeToCodeTestContent/Perf'
DOCUMENT_KINDS = ('agent_min', 'agent_pretty', 'translation_min', 'translation_pretty')


def engine_root(version):
    override = os.environ.get('N2C_UE_%s' % version.replace('.', '_'))
    return override or os.path.join(os.environ.get('ProgramFiles', r'C:\Program Files'), 'Epic Games', 'UE_%s' % version)


def host_project(version):
    name = 'NodeToCodeHost_%s' % version.replace('.', '_')
    return os.path.normpath(os.path.join(REPO, '..', 'Hosts', name, name + '.uproject'))


def load_keys():
    """Provider keys from the checkout's .env into this process's environment (never printed)"""
    try:
        from env_keys import load_provider_keys
        for key, value in load_provider_keys().items():
            os.environ.setdefault(key, value)
    except Exception:
        pass


def run_capture(version, out_dir, scratch):
    editor = find_editor(engine_root(version))
    config = {'out_dir': scratch,
              'fixtures': [{'name': name, 'path': '%s/%s' % (FOLDER, name)} for name in FIXTURES],
              'chain': {'path': '%s/BP_N2CPerf_5k' % FOLDER, 'graph': 'F_Chain_0'}}
    config_path = os.path.join(scratch, 'config.json')
    json.dump(config, open(config_path, 'w', encoding='utf-8'), indent=2)
    trace = os.path.join(scratch, 'capture.utrace')
    args = [editor, host_project(version), '-ExecutePythonScript=%s' % os.path.join(HERE, 'capture_in_editor.py'),
            '-trace=cpu,memory', '-tracefile=%s' % trace, '-unattended', '-nullrhi', '-nosplash', '-nosound',
            '-NoLiveCoding', '-RenderOffScreen', '-culture=en', '-abslog=%s' % os.path.join(scratch, 'capture.log')] + list(DDC_ARGS)
    env = dict(os.environ, N2C_PERF_CONFIG=config_path, UE_SKIP_UBT_SDK_SETUP='1')
    process = subprocess.Popen(args, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    job = kill_with_parent(process)  # noqa: F841 (kept alive for the run)
    process.wait(timeout=1800)
    results_path = os.path.join(scratch, 'editor_results.json')
    if not os.path.isfile(results_path):
        raise RuntimeError('UE %s: the capture script wrote no results (see %s)' % (version, os.path.join(scratch, 'capture.log')))
    results = json.load(open(results_path, encoding='utf-8'))
    json.dump(results, open(os.path.join(out_dir, 'editor_results.json'), 'w', encoding='utf-8'), indent=2)
    return results, trace


def export_timers(version, trace, out_dir, scratch):
    """The N2C_* rows of the trace's timer statistics, also written to <out_dir>/timers_n2c.csv"""
    insights = os.path.join(engine_root(version), 'Engine', 'Binaries', 'Win64', 'UnrealInsights.exe')
    full_csv = os.path.join(scratch, 'timers.csv')
    # Insights reads the value only when the quote follows the '=' directly (-Cmd="..."), so the command line is built
    # by hand: subprocess would escape the inner quotes. Paths here have no spaces (the checkout's BuildLogs).
    command_line = '"%s" -OpenTraceFile="%s" -NoUI -AutoQuit -log -ABSLOG="%s" -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics %s"' % (
        insights, trace, os.path.join(scratch, 'insights.log'), full_csv)
    subprocess.run(command_line, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)
    if not os.path.isfile(full_csv):
        return []
    with open(full_csv, encoding='utf-8-sig', newline='') as f:
        rows = [row for row in csv.DictReader(f) if (row.get('Name') or '').startswith('N2C_')]
    if rows:
        with open(os.path.join(out_dir, 'timers_n2c.csv'), 'w', encoding='utf-8', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)
    return rows


def tools_list_size(version, scratch):
    """(bytes, token counts) of the tools/list response from a headless editor's MCP server"""
    port = free_port(27000 + int(os.environ.get('N2C_PORT_OFFSET', '0') or 0) + 70)
    session = EditorSession(engine_root(version), host_project(version), port, os.path.join(scratch, 'mcp.log'), allow_python=False)
    if not session.start():
        session.stop()
        return None, None
    try:
        url = 'http://127.0.0.1:%d/mcp' % port
        headers = {'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream'}

        def post(body, extra=None):
            request = urllib.request.Request(url, data=json.dumps(body).encode('utf-8'), headers=dict(headers, **(extra or {})))
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read(), response.headers.get('Mcp-Session-Id')

        _, session_id = post({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
            'protocolVersion': '2025-11-25', 'capabilities': {}, 'clientInfo': {'name': 'n2c-perf', 'version': '1'}}})
        extra = {'Mcp-Session-Id': session_id} if session_id else {}
        post({'jsonrpc': '2.0', 'method': 'notifications/initialized'}, extra)
        raw, _ = post({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}, extra)
        text = raw.decode('utf-8')
        return len(raw), tokens.count_all(text)
    finally:
        session.stop()


def scope_stats(row):
    """One N2C_* row of the timer statistics (seconds) as {count, total_s, median_ms, max_ms} (inclusive times)"""
    def number(key, scale):
        try:
            return round(float(row[key]) * scale, 3)
        except (KeyError, TypeError, ValueError):
            return None
    count = number('Count', 1)
    return {'count': int(count) if count is not None else None, 'total_s': number('Incl', 1),
            'median_ms': number('I.Med', 1000.0), 'max_ms': number('I.Max', 1000.0)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--engines', nargs='+', default=['5.8', '5.6'])
    parser.add_argument('--out', default=os.path.join(REPO, 'Benchmarks', 'perf', datetime.date.today().isoformat()))
    parser.add_argument('--skip-mcp', action='store_true')
    args = parser.parse_args()
    load_keys()
    commit = subprocess.run(['git', '-C', REPO, 'rev-parse', '--short=12', 'HEAD'], capture_output=True, text=True).stdout.strip()
    summary = {'date': datetime.date.today().isoformat(), 'commit': commit, 'machine': {
        'cpu': platform.processor(), 'cores': os.cpu_count(), 'os': platform.platform()}, 'engines': {}}
    try:
        import ctypes

        class Status(ctypes.Structure):
            _fields_ = [('dwLength', ctypes.c_ulong), ('dwMemoryLoad', ctypes.c_ulong), ('ullTotalPhys', ctypes.c_ulonglong),
                        ('ullAvailPhys', ctypes.c_ulonglong), ('ullTotalPageFile', ctypes.c_ulonglong),
                        ('ullAvailPageFile', ctypes.c_ulonglong), ('ullTotalVirtual', ctypes.c_ulonglong),
                        ('ullAvailVirtual', ctypes.c_ulonglong), ('ullAvailExtendedVirtual', ctypes.c_ulonglong)]
        status = Status()
        status.dwLength = ctypes.sizeof(Status)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
        summary['machine']['ram_gb'] = round(status.ullTotalPhys / 2 ** 30, 1)
    except Exception:
        pass

    for version in args.engines:
        out_dir = os.path.join(args.out, version)
        scratch = os.path.join(REPO, 'BuildLogs', 'perf', os.path.basename(args.out), version)
        os.makedirs(out_dir, exist_ok=True)
        os.makedirs(scratch, exist_ok=True)
        print('UE %s: capture' % version, flush=True)
        results, trace = run_capture(version, out_dir, scratch)
        print('UE %s: Insights export' % version, flush=True)
        timers = export_timers(version, trace, out_dir, scratch)
        print('UE %s: tokens' % version, flush=True)
        counted = {}
        for fixture in results['fixtures']:
            counted[fixture['name']] = {kind: tokens.count_all(open(os.path.join(scratch, '%s.%s.json' % (fixture['name'], kind)),
                                                                    encoding='utf-8').read()) for kind in ('agent_min', 'translation_min')}
        for row in results['depth']:
            row['tokens'] = tokens.count_all(open(os.path.join(scratch, 'depth%d.translation_min.json' % row['depth']), encoding='utf-8').read())
        json.dump(counted, open(os.path.join(out_dir, 'tokens.json'), 'w', encoding='utf-8'), indent=2)
        tools = (None, None)
        if not args.skip_mcp:
            print('UE %s: tools/list' % version, flush=True)
            tools = tools_list_size(version, scratch)
        summary['engines'][version] = {
            'engine': results['engine'], 'fixtures': results['fixtures'], 'tokens': counted, 'depth': results['depth'],
            'tags': results['tags'], 'errors': results['errors'],
            'scopes': {row['Name']: scope_stats(row) for row in sorted(timers, key=lambda r: r['Name'])},
            'tools_list': {'bytes': tools[0], 'tokens': tools[1]},
        }
    json.dump(summary, open(os.path.join(args.out, 'summary.json'), 'w', encoding='utf-8'), indent=2)
    open(os.path.join(args.out, 'summary.md'), 'w', encoding='utf-8', newline='\n').write(render(summary))
    print(render(summary))


def fmt(value, digits=0):
    if value is None:
        return 'n/m'
    return ('{:,.%df}' % digits).format(value) if isinstance(value, float) else '{:,}'.format(value)


def render(summary):
    """The markdown tables performance-baseline.md carries"""
    out = io.StringIO()
    counters = [name for name, _ in tokens.COUNTERS]
    out.write('Commit %s, %s; CPU %s (%s threads), %s GB RAM.\n\n' % (summary['commit'], summary['date'], summary['machine'].get('cpu'),
              summary['machine'].get('cores'), summary['machine'].get('ram_gb')))
    out.write('| Fixture | Engine | Nodes | Read ms (median of 5) | Agent JSON bytes min / pretty | Translation JSON bytes min / pretty | '
              + ' | '.join('Translation tokens %s' % c for c in counters) + ' | Bytes per node (translation, min) | '
              'Tokens per node (chars/4 / Anthropic) | Working set delta MB |\n')
    out.write('|' + '---|' * (9 + len(counters)) + '\n')
    for version, engine in summary['engines'].items():
        for fixture in engine['fixtures']:
            counts = engine['tokens'][fixture['name']]['translation_min']
            nodes = max(fixture['nodes'], 1)
            delta = fixture['working_set_delta']
            out.write('| %s | %s | %s | %s | %s / %s | %s / %s | %s | %s | %s / %s | %s |\n' % (
                fixture['name'], version, fmt(fixture['nodes']), fmt(fixture['read_ms_median'], 1),
                fmt(fixture['bytes']['agent_min']), fmt(fixture['bytes']['agent_pretty']),
                fmt(fixture['bytes']['translation_min']), fmt(fixture['bytes']['translation_pretty']),
                ' | '.join(fmt(counts.get(c)) for c in counters), fmt(fixture['bytes']['translation_min'] / nodes, 1),
                fmt(counts['chars_div4'] / nodes, 1), fmt(counts['anthropic'] / nodes, 1) if counts.get('anthropic') else 'n/m',
                fmt(delta / 2 ** 20, 1) if delta is not None else 'n/m'))
    out.write('\nTrace scopes (UnrealInsights timer statistics, inclusive, over every call in the capture session: the warm-up '
              'and timed reads of all three fixtures, then the depth reads):\n\n')
    out.write('| Scope | Engine | Calls | Total s | Median ms | Max ms |\n|---|---|---|---|---|---|\n')
    for version, engine in summary['engines'].items():
        for scope, stats in engine['scopes'].items():
            out.write('| %s | %s | %s | %s | %s | %s |\n' % (scope, version, fmt(stats['count']), fmt(stats['total_s'], 3),
                      fmt(stats['median_ms'], 3), fmt(stats['max_ms'], 1)))
    out.write('\nMax Translation Depth on BP_N2CPerf_5k F_Chain_0:\n\n| Engine | Depth | Graphs | Nodes | Read ms | Translation bytes | Tokens chars/4 | Tokens Anthropic |\n|---|---|---|---|---|---|---|---|\n')
    for version, engine in summary['engines'].items():
        for row in engine['depth']:
            out.write('| %s | %d | %d | %s | %s | %s | %s | %s |\n' % (version, row['depth'], row['graphs'], fmt(row['nodes']),
                      fmt(row['read_ms_median'], 1), fmt(row['translation_min_bytes']), fmt(row['tokens']['chars_div4']),
                      fmt(row['tokens']['anthropic'])))
    out.write('\nOther:\n\n| Engine | tools/list bytes | tools/list tokens chars/4 / Anthropic | Tagged graphs | SearchTags ms | GetAllTags ms |\n|---|---|---|---|---|---|\n')
    for version, engine in summary['engines'].items():
        tools = engine['tools_list']
        tags = engine['tags'] or {}
        out.write('| %s | %s | %s / %s | %s | %s | %s |\n' % (version, fmt(tools['bytes']),
                  fmt((tools['tokens'] or {}).get('chars_div4')), fmt((tools['tokens'] or {}).get('anthropic')),
                  fmt(tags.get('tagged_graphs')), fmt(tags.get('search_ms_median'), 3), fmt(tags.get('get_all_tags_ms_median'), 3)))
    return out.getvalue()


if __name__ == '__main__':
    main()
