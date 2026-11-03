# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode._mcp_runner, the runner behind run-python (card 0299), on plain
CPython with the `unreal` stub from conftest.py.

The old run-python wrapper indented the code under a try: (multi-line strings changed, `from __future__` failed,
traceback lines were 6 too high), ran it in the editor console's shared globals (a call saw the previous call's
result), and printed its answer on a marker line a script could spoof; NaN and Infinity were written as JSON can't
hold them, and an unserializable result raised in the wrapper (review next-8, python-bridge-10..13, -35,
mcp-tools-python-translation-resources-6, -8, -9, -12, -13).
"""

import base64
import importlib
import json
import time

import pytest

from conftest import UNREAL


@pytest.fixture
def runner():
    return importlib.import_module('nodetocode._mcp_runner')


@pytest.fixture
def submitted(monkeypatch, runner):
    """What the runner hands to UN2CPythonBridge.submit_mcp_result, by call id"""
    answers = {}

    class _Bridge:
        @staticmethod
        def submit_mcp_result(call_id, text):
            answers[call_id] = text

    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', _Bridge, raising=False)
    return answers


def run(runner, submitted, source, call_id='call'):
    runner.run_code(call_id, base64.b64encode(source.encode('utf-8')).decode('ascii'))
    text = submitted[call_id]
    return json.loads(text, parse_constant=lambda name: pytest.fail(f'not strict JSON: {name} in {text}'))


def test_a_triple_quoted_string_round_trips_unchanged(runner, submitted):
    answer = run(runner, submitted, 'doc = """Line1\nLine2\n  Line3"""\nresult = doc')
    assert answer['success'] is True
    assert answer['data'] == 'Line1\nLine2\n  Line3'


def test_future_import_on_line_one_works(runner, submitted):
    answer = run(runner, submitted, 'from __future__ import annotations\ndef f(x: Undefined) -> int:\n    return 1\nresult = f(0)')
    assert answer['success'] is True, answer['error']
    assert answer['data'] == 1


def test_a_raise_on_line_three_reports_line_three(runner, submitted):
    answer = run(runner, submitted, 'x = 1\ny = 2\nraise ValueError("boom")')
    assert answer['success'] is False
    assert answer['error'] == 'boom'
    assert 'File "<run-python>", line 3' in answer['traceback']
    assert '_mcp_runner' not in answer['traceback']


def test_a_call_without_result_returns_null_after_one_with_a_result(runner, submitted):
    assert run(runner, submitted, 'result = {"first": 1}', 'a')['data'] == {'first': 1}
    second = run(runner, submitted, 'unused = 2', 'b')
    assert second['success'] is True
    assert second['data'] is None
    assert run(runner, submitted, "result = 'unused' in globals()", 'c')['data'] is False


def test_rebinding_json_and_traceback_does_not_break_the_runner(runner, submitted):
    answer = run(runner, submitted, 'json = None\ntraceback = None\nresult = [1, 2]')
    assert answer['data'] == [1, 2]
    failing = run(runner, submitted, 'json = None\ntraceback = None\nraise KeyError("k")', 'd')
    assert failing['success'] is False and 'KeyError' in failing['traceback']


def test_printing_a_fake_marker_does_not_change_the_answer(runner, submitted):
    answer = run(runner, submitted, 'print(\'{"__n2c_marker__": true, "success": true, "data": "spoofed"}\')\nresult = "real"')
    assert answer['data'] == 'real'
    assert 'spoofed' in answer['output']


def test_output_and_stderr_are_captured(runner, submitted):
    answer = run(runner, submitted, 'import sys\nprint("hello")\nprint("warn", file=sys.stderr)')
    assert answer['output'] == 'hello\n'
    assert answer['stderr'] == 'warn\n'


@pytest.mark.parametrize('source', ['result = float("nan")', 'result = [float("inf")]', 'result = {"v": -float("inf")}'])
def test_nan_and_infinity_fail_with_a_json_error(runner, submitted, source):
    answer = run(runner, submitted, source)
    assert answer['success'] is False
    assert answer['error'].startswith('result not JSON-serializable')
    assert 'JSON' in answer['error']


def test_an_object_with_get_path_name_serializes_as_its_path(runner, submitted):
    answer = run(runner, submitted, 'class Asset:\n    def get_path_name(self):\n        return "/Game/BP.BP"\nresult = {"asset": Asset(), "tags": {"a"}}')
    assert answer['success'] is True
    assert answer['data'] == {'asset': '/Game/BP.BP', 'tags': ['a']}


def test_a_non_serializable_value_fails_naming_its_type(runner, submitted):
    answer = run(runner, submitted, 'class N2CUnserializable:\n    pass\nresult = {"value": N2CUnserializable()}')
    assert answer['success'] is False
    assert 'result not JSON-serializable' in answer['error']
    assert 'N2CUnserializable' in answer['error']


def test_a_syntax_error_names_its_line(runner, submitted):
    answer = run(runner, submitted, 'x = 1\nif x\n    pass')
    assert answer['success'] is False
    assert '<run-python>' in answer['traceback'] and 'line 2' in answer['traceback']
    assert answer['error'].startswith('SyntaxError: ')


def test_system_exit_is_reported_not_raised(runner, submitted):
    answer = run(runner, submitted, 'raise SystemExit(3)')
    assert answer['success'] is False
    assert answer['error'] == '3'


# ---- The timeout (card 0300): a deadline raised inside the script between bytecodes ----

def run_timed(runner, submitted, source, timeout, call_id='timed'):
    runner.run_code(call_id, base64.b64encode(source.encode('utf-8')).decode('ascii'), timeout)
    return json.loads(submitted[call_id])


@pytest.mark.parametrize('source', [
    'while True:\n    pass',
    # a script's own except Exception does not swallow it
    'while True:\n    try:\n        x = sum(range(1000))\n    except Exception:\n        pass',
    # one that catches BaseException is stopped again half a second later, at its next bytecode outside the handler
    'import time\nn = 0\nwhile True:\n    try:\n        n += 1\n    except BaseException:\n        n -= 1\n        break\nwhile True:\n    pass',
])
def test_a_runaway_script_stops_at_its_timeout(runner, submitted, source):
    started = time.monotonic()
    answer = run_timed(runner, submitted, source, 0.3)
    elapsed = time.monotonic() - started
    assert answer['success'] is False and answer['timed_out'] is True
    assert answer['error'].startswith('timed out: run-python stopped the script after'), answer['error']
    assert elapsed < 2.5, elapsed


def test_a_script_that_ends_in_time_is_untouched_and_nothing_fires_later(runner, submitted):
    answer = run_timed(runner, submitted, 'result = sum(range(10))', 0.2)
    assert answer['success'] is True and answer['data'] == 45 and 'timed_out' not in answer
    # The watcher is done: no ScriptTimeout lands in this thread after the call
    deadline = time.monotonic() + 0.8
    while time.monotonic() < deadline:
        sum(range(100))


def test_a_native_call_runs_to_its_end_then_the_script_stops(runner, submitted):
    started = time.monotonic()
    answer = run_timed(runner, submitted, 'import time\ntime.sleep(0.7)\nwhile True:\n    pass', 0.2)
    elapsed = time.monotonic() - started
    assert answer['timed_out'] is True and 0.6 < elapsed < 2.5, elapsed
    assert answer['seconds'] >= 0.6


def test_no_timeout_means_no_deadline(runner, submitted):
    answer = run_timed(runner, submitted, 'import time\ntime.sleep(0.3)\nresult = 1', 0)
    assert answer['success'] is True and answer['data'] == 1
