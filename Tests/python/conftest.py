# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Plain-CPython test setup for the NodeToCode Python package (Content/Python/nodetocode).

The package imports `unreal`, which only exists inside the editor. This file installs a small stand-in module in
sys.modules before anything imports the package, so the package's logic (the wrappers, invoke_tool, the run-python runner)
runs on any CPython without UE installed. The stub has only what the tested code uses: unreal.Paths
(project_content_dir, project_plugins_dir, project_dir) and unreal.log / log_warning / log_error. Any other
attribute raises an AttributeError that names the stub, so a new editor dependency shows up as a test failure instead
of passing silently.

Run (from the repo root; 3.11 is the Python UE 5.6-5.8 embed):
    uv run --no-project --python 3.11 --with-requirements Tests/python/requirements.txt -- python -m pytest Tests/python

Tests/python/editor holds tests that need the real editor (run_editor_tests.py, see README.md); pytest skips it.
"""

import json
import os
import sys
import types

import pytest

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CONTENT_PYTHON = os.path.join(REPO_ROOT, 'Content', 'Python')

# Content/Python is junctioned into the host projects and packaged (minus __pycache__): never write .pyc files there
sys.dont_write_bytecode = True
if CONTENT_PYTHON not in sys.path:
    sys.path.insert(0, CONTENT_PYTHON)

# In-editor tests import the real `unreal` and run through editor/run_editor_tests.py, never under this stub
collect_ignore = ['editor']


def _ue_dir(path):
    """UE's Paths functions return forward slashes with a trailing slash."""
    return path.replace('\\', '/').rstrip('/') + '/'


class _UnrealStub(types.ModuleType):
    """Stand-in for the editor's `unreal` module. Tests point the paths at pytest tmp folders through `ue`."""

    def __init__(self):
        super().__init__('unreal')
        self.__file__ = __file__
        self.logs = []  # (level, message)
        self._content_dir = ''
        self._plugins_dir = ''
        self._project_dir = ''
        stub = self

        class Paths:
            @staticmethod
            def project_content_dir():
                return stub._content_dir

            @staticmethod
            def project_plugins_dir():
                return stub._plugins_dir

            @staticmethod
            def project_dir():
                return stub._project_dir

        self.Paths = Paths

    def log(self, message):
        self.logs.append(('info', str(message)))

    def log_warning(self, message):
        self.logs.append(('warning', str(message)))

    def log_error(self, message):
        self.logs.append(('error', str(message)))

    def messages(self, level):
        return [m for lvl, m in self.logs if lvl == level]

    def __getattr__(self, name):
        # Only reached for names the stub does not define. Still an AttributeError, so hasattr()/getattr(x, n, d)
        # probes in the package behave as they do for a missing editor class.
        if name.startswith('__') and name.endswith('__'):
            raise AttributeError(name)
        raise AttributeError(f"unreal.{name} is not in the test stub (Tests/python/conftest.py): the code under test "
                             f"now needs it, so stub it there or test that code in the editor (Tests/python/editor)")


UNREAL = _UnrealStub()
sys.modules['unreal'] = UNREAL

import nodetocode  # noqa: E402,F401  (needs the stub above)


class ToolStub:
    """Stand-in for unreal.N2CPythonBridge.invoke_tool (card 0342): records (tool, arguments) and answers with the
    envelope InvokeTool writes. answers maps a tool name to the tool's value (a dict), to an error string (sent as
    a failure with code TOOL_FAILED), or to a callable(arguments) returning either."""

    def __init__(self, answers=None):
        self.calls = []
        self.answers = dict(answers or {})

    def invoke_tool(self, tool_name, arguments_json):
        arguments = json.loads(arguments_json)
        self.calls.append((tool_name, arguments))
        answer = self.answers.get(tool_name, {})
        if callable(answer):
            answer = answer(arguments)
        if isinstance(answer, str):
            return json.dumps({'success': False, 'data': None, 'error': answer, 'code': 'TOOL_FAILED'},
                              ensure_ascii=False)
        return json.dumps({'success': True, 'data': answer, 'error': None}, ensure_ascii=False)


@pytest.fixture
def tools(monkeypatch):
    """unreal.N2CPythonBridge as a ToolStub; set stub.answers[tool] to answer a tool"""
    stub = ToolStub()
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', stub, raising=False)
    return stub
