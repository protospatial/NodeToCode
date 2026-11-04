# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Check a performance baseline run against the proposed budget (card 0362).

    python -I Benchmarks/perf/check_budget.py [RESULTS] [--budget Benchmarks/perf/budget.json] [--engine 5.8 ...]

RESULTS is what Benchmarks/perf/run_perf.py wrote: a run folder (Benchmarks/perf/<date>), its summary.json, or one
engine's timers_n2c.csv (scope limits only). Without it, the newest dated folder under Benchmarks/perf is used.
Scope times come from summary.json's scopes, or from <run>/<engine>/timers_n2c.csv (I.Max, seconds) when summary.json
has none. Every limit in the budget is checked for every engine in the run; a metric the run did not record (for
example N2C_TokenEstimate, which a script read never runs) is listed as not measured and does not fail the check.

Exit codes: 0 every measured limit holds; 1 at least one breach; 2 the results or the budget could not be read.
Stdlib only.
"""
import argparse
import csv
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def newest_run():
    runs = sorted(d for d in glob.glob(os.path.join(HERE, '*')) if re.fullmatch(r'\d{4}-\d{2}-\d{2}', os.path.basename(d))
                  and os.path.isdir(d))
    return runs[-1] if runs else None


def read_timers_csv(path):
    """{scope: {'max_ms', 'median_ms', 'count'}} from a TimingInsights export (times in seconds)"""
    scopes = {}
    with open(path, encoding='utf-8-sig', newline='') as f:
        for row in csv.DictReader(f):
            name = row.get('Name')
            if not name:
                continue
            scopes[name] = {'max_ms': float(row['I.Max']) * 1000.0, 'median_ms': float(row['I.Med']) * 1000.0,
                            'count': int(float(row['Count']))}
    return scopes


def load_results(path):
    """{engine: engine dict as in summary.json}; a lone timers CSV becomes {'csv': {'scopes': ...}}"""
    if os.path.isdir(path):
        summary = os.path.join(path, 'summary.json')
        if not os.path.isfile(summary):
            raise ValueError('no summary.json in %s' % path)
        run_dir, path = path, summary
    else:
        run_dir = os.path.dirname(path)
    if path.lower().endswith('.csv'):
        return {'csv': {'scopes': read_timers_csv(path)}}
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    engines = data.get('engines')
    if not isinstance(engines, dict) or not engines:
        raise ValueError('%s has no engines' % path)
    for name, engine in engines.items():
        if not engine.get('scopes'):
            timers = os.path.join(run_dir, name, 'timers_n2c.csv')
            if os.path.isfile(timers):
                engine['scopes'] = read_timers_csv(timers)
    return engines


def measure(limit, engine):
    """The engine's value for the limit's metric, with what it was measured on, or (None, reason)"""
    metric = limit['metric']
    fixtures = engine.get('fixtures') or []
    tokens = engine.get('tokens') or {}
    if metric == 'fixture_read_ms':
        for fx in fixtures:
            if fx.get('name') == limit['fixture'] and fx.get('read_ms_median') is not None:
                return fx['read_ms_median'], limit['fixture']
        return None, 'no read of %s' % limit['fixture']
    if metric == 'scope_max_ms':
        scope = (engine.get('scopes') or {}).get(limit['scope'])
        if not scope or scope.get('max_ms') is None:
            return None, 'scope %s not in the trace' % limit['scope']
        return scope['max_ms'], limit['scope']
    if metric in ('tokens_per_node', 'bytes_per_node'):
        worst, where = None, None
        for fx in fixtures:
            nodes = fx.get('nodes') or 0
            if nodes <= 0:
                continue
            if metric == 'bytes_per_node':
                value = (fx.get('bytes') or {}).get('translation_min')
            else:
                value = ((tokens.get(fx.get('name')) or {}).get('translation_min') or {}).get(limit['counter'])
            if value is None:
                continue
            per_node = value / nodes
            if worst is None or per_node > worst:
                worst, where = per_node, fx['name']
        return (round(worst, 1), where) if worst is not None else (None, 'no %s count' % limit.get('counter', 'byte'))
    if metric == 'tools_list_tokens':
        value = ((engine.get('tools_list') or {}).get('tokens') or {}).get(limit['counter'])
        return (value, 'tools/list') if value is not None else (None, 'no tools/list %s count' % limit['counter'])
    if metric == 'tools_list_bytes':
        value = (engine.get('tools_list') or {}).get('bytes')
        return (value, 'tools/list') if value is not None else (None, 'no tools/list')
    if metric == 'tag_search_ms':
        value = (engine.get('tags') or {}).get('search_ms_median')
        return (value, 'SearchTags') if value is not None else (None, 'no tag search')
    return None, 'unknown metric %s' % metric


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0].strip())
    parser.add_argument('results', nargs='?', help='run folder, summary.json or timers_n2c.csv (default: newest run)')
    parser.add_argument('--budget', default=os.path.join(HERE, 'budget.json'))
    parser.add_argument('--engine', nargs='+', help='only these engines (default: every engine in the run)')
    args = parser.parse_args(argv)

    results = args.results or newest_run()
    if not results:
        print('check_budget: no run under %s; pass a results path' % HERE)
        return 2
    try:
        with open(args.budget, encoding='utf-8') as f:
            limits = json.load(f)['limits']
        engines = load_results(results)
    except (OSError, ValueError, KeyError) as e:
        print('check_budget: %s' % e)
        return 2
    if args.engine:
        engines = {k: v for k, v in engines.items() if k in args.engine}
        if not engines:
            print('check_budget: none of %s in %s' % (', '.join(args.engine), results))
            return 2

    breaches, measured, skipped = 0, 0, 0
    print('check_budget: %s against %s' % (results, os.path.relpath(args.budget)))
    for name, engine in sorted(engines.items()):
        for limit in limits:
            value, where = measure(limit, engine)
            if value is None:
                skipped += 1
                print('  %-6s %-28s not measured (%s)' % (name, limit['id'], where))
                continue
            measured += 1
            ok = value <= limit['max']
            breaches += 0 if ok else 1
            print('  %-6s %-28s %-6s %s %s %s (%s)' % (name, limit['id'], 'ok' if ok else 'BREACH', value,
                                                     '<=' if ok else '>', limit['max'], where))
    print('check_budget: %d measured, %d breach(es), %d not measured' % (measured, breaches, skipped))
    if measured == 0:
        return 2
    return 1 if breaches else 0


if __name__ == '__main__':
    sys.exit(main())
