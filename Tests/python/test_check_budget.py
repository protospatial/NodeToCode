# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Benchmarks/perf/check_budget.py (card 0362): the committed baseline run meets the proposed budget (exit 0), a run
edited to exceed one limit fails (exit 1), a metric the run did not record is skipped, and timers come from
timers_n2c.csv when summary.json has no scopes.
"""
import importlib.util
import json
import os
import shutil

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
PERF = os.path.join(REPO, 'Benchmarks', 'perf')
BASELINE = os.path.join(PERF, '2026-11-03')


def load_module():
    spec = importlib.util.spec_from_file_location('check_budget', os.path.join(PERF, 'check_budget.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def copy_run(tmp_path):
    run = tmp_path / 'run'
    shutil.copytree(BASELINE, run)
    return run


def edit_summary(run, edit):
    path = run / 'summary.json'
    data = json.loads(path.read_text(encoding='utf-8'))
    edit(data)
    path.write_text(json.dumps(data), encoding='utf-8')


def test_budget_ids_are_unique_and_every_limit_has_a_max():
    limits = json.loads(open(os.path.join(PERF, 'budget.json'), encoding='utf-8').read())['limits']
    ids = [limit['id'] for limit in limits]
    assert len(ids) == len(set(ids))
    assert all(isinstance(limit['max'], (int, float)) and limit['max'] > 0 for limit in limits)


def test_committed_baseline_meets_the_budget(capsys):
    assert load_module().main([BASELINE]) == 0
    out = capsys.readouterr().out
    assert 'BREACH' not in out
    assert 'token_estimate_tick_ms       not measured' in out


def test_one_limit_exceeded_fails(tmp_path, capsys):
    run = copy_run(tmp_path)
    edit_summary(run, lambda d: d['engines']['5.8']['fixtures'][1].update(read_ms_median=2500.0))
    assert load_module().main([str(run)]) == 1
    out = capsys.readouterr().out
    assert 'read_ms_1k                   BREACH 2500.0 > 2000' in out


def test_scopes_fall_back_to_the_timers_csv(tmp_path, capsys):
    run = copy_run(tmp_path)
    edit_summary(run, lambda d: [e.pop('scopes') for e in d['engines'].values()])
    assert load_module().main([str(run), '--engine', '5.8']) == 0
    assert 'serialize_max_ms             ok     2785.37' in capsys.readouterr().out


def test_unreadable_results_exit_2(tmp_path):
    assert load_module().main([str(tmp_path)]) == 2
