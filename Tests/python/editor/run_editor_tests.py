# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Runs the in-editor Python tests: every test_*.py in this folder, inside a real Unreal Editor with the real `unreal`
module and the NodeToCode plugin loaded. There is no pytest in the editor, so this is a small runner: each module-level
function named test_* is called with no arguments, in file order; a test passes when it returns and fails when it raises.

The result is one log line to grep for:
    N2C_EDITOR_TESTS PASS <count>
    N2C_EDITOR_TESTS FAIL <failed>/<count>: <module>::<test>, ...
Each failure's traceback is logged as an error above that line.

Run headless (the editor's -ExecutePythonScript runner quits the editor once this script returns):
    UnrealEditor-Cmd.exe ..\\Hosts\\NodeToCodeHost_5_8\\NodeToCodeHost_5_8.uproject
        -ExecutePythonScript=<repo>/Tests/python/editor/run_editor_tests.py -unattended -nullrhi -nosplash -nosound -stdout
See Tests/python/README.md.
"""

import importlib.util
import os
import sys
import traceback

import unreal

RESULT_TAG = 'N2C_EDITOR_TESTS'
HERE = os.path.dirname(os.path.abspath(__file__))


def _load(path):
    name = 'n2c_editor_tests_' + os.path.splitext(os.path.basename(path))[0]
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def _tests(module):
    found = [(getattr(obj, '__code__').co_firstlineno, name, obj) for name, obj in vars(module).items()
             if name.startswith('test_') and callable(obj) and getattr(obj, '__module__', None) == module.__name__
             and hasattr(obj, '__code__')]
    return [(name, obj) for _, name, obj in sorted(found)]


def run(folder=HERE):
    """Run every test in folder; returns (passed names, failed names)."""
    passed, failed = [], []
    for file_name in sorted(os.listdir(folder)):
        if not (file_name.startswith('test_') and file_name.endswith('.py')):
            continue
        label = os.path.splitext(file_name)[0]
        try:
            module = _load(os.path.join(folder, file_name))
        except Exception:
            unreal.log_error(f'{RESULT_TAG} {label}: import failed\n{traceback.format_exc()}')
            failed.append(f'{label}::<import>')
            continue
        for test_name, test in _tests(module):
            full = f'{label}::{test_name}'
            try:
                test()
            except Exception:
                unreal.log_error(f'{RESULT_TAG} {full} failed\n{traceback.format_exc()}')
                failed.append(full)
            else:
                unreal.log(f'{RESULT_TAG} {full} ok')
                passed.append(full)
    return passed, failed


def main():
    try:
        passed, failed = run()
    except Exception:
        unreal.log_error(f'{RESULT_TAG} runner error\n{traceback.format_exc()}')
        passed, failed = [], ['<runner>']
    total = len(passed) + len(failed)
    if failed or not total:
        unreal.log_error(f'{RESULT_TAG} FAIL {len(failed)}/{total}: ' + (', '.join(failed) or 'no tests found'))
    else:
        unreal.log(f'{RESULT_TAG} PASS {total}')


main()
