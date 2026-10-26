# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for Content/Python/init_unreal.py (card 0175) on plain CPython with the `unreal` stub from conftest.py. The
editor runs this file at startup on every engine. It registers NodeToCodeTools only on UE 5.8+ with Epic's
ToolsetRegistry plugin loaded; anywhere else it must import nothing of the toolset, log nothing and never raise. The
registration itself is checked in the editor (NodeToCode.Toolsets.PythonTests, Tests/mcp_probe/live_toolset_0175.py).
"""

import os
import runpy
import sys
import types

import pytest

from conftest import CONTENT_PYTHON, UNREAL

INIT_UNREAL = os.path.join(CONTENT_PYTHON, 'init_unreal.py')


class _SystemLibrary:
    version = ''

    @classmethod
    def get_engine_version(cls):
        return cls.version


@pytest.fixture
def engine(monkeypatch):
    """Sets the stub's engine version and which ToolsetRegistry classes exist; clears the log and toolset modules"""
    UNREAL.logs.clear()
    for name in [m for m in sys.modules if m.startswith(('nodetocode.toolsets', 'toolset_registry'))]:
        monkeypatch.delitem(sys.modules, name)

    def setup(version, registry_classes=True, registry_package=None):
        monkeypatch.setattr(_SystemLibrary, 'version', version)
        monkeypatch.setattr(UNREAL, 'SystemLibrary', _SystemLibrary, raising=False)
        if registry_classes:
            monkeypatch.setattr(UNREAL, 'ToolsetDefinition', type('ToolsetDefinition', (), {}), raising=False)
            monkeypatch.setattr(UNREAL, 'PythonTestRunner', type('PythonTestRunner', (), {}), raising=False)
        if registry_package is not None:
            monkeypatch.setitem(sys.modules, 'toolset_registry', registry_package)
        else:
            # No ToolsetRegistry plugin: its Content/Python is not on sys.path, so the import fails
            monkeypatch.setitem(sys.modules, 'toolset_registry', None)
    return setup


def _run():
    runpy.run_path(INIT_UNREAL, run_name='init_unreal')


def _toolset_modules():
    return [m for m in sys.modules if m.startswith('nodetocode.toolsets')]


@pytest.mark.parametrize('version', ['5.6.1-44394996+++UE5+Release-5.6', '5.7.4-0+++UE5+Release-5.7'])
def test_does_nothing_before_5_8(engine, version):
    engine(version, registry_classes=False)
    _run()
    assert UNREAL.logs == []
    assert _toolset_modules() == []


def test_does_nothing_on_5_8_without_the_plugin(engine):
    engine('5.8.3-0+++UE5+Release-5.8', registry_classes=False)
    _run()
    assert UNREAL.logs == []
    assert _toolset_modules() == []


def test_does_nothing_when_the_registry_package_is_missing(engine):
    engine('5.8.3-0+++UE5+Release-5.8', registry_classes=True)
    _run()
    assert UNREAL.logs == []
    assert _toolset_modules() == []


def test_does_nothing_before_5_8_even_with_registry_classes(engine):
    engine('5.7.4-0+++UE5+Release-5.7', registry_classes=True, registry_package=types.ModuleType('toolset_registry'))
    _run()
    assert UNREAL.logs == []
    assert _toolset_modules() == []


def test_an_unreadable_engine_version_does_nothing(engine):
    engine('not a version', registry_classes=True, registry_package=types.ModuleType('toolset_registry'))
    _run()
    assert UNREAL.logs == []
    assert _toolset_modules() == []


def test_a_registration_failure_is_one_warning_not_an_exception(engine):
    # The registry package exists but has no registration module: importing the toolsets fails inside the editor's
    # startup script, which must only warn
    engine('5.8.3-0+++UE5+Release-5.8', registry_classes=True, registry_package=types.ModuleType('toolset_registry'))
    _run()
    warnings = UNREAL.messages('warning')
    assert len(warnings) == 1 and warnings[0].startswith('NodeToCode: NodeToCodeTools toolset not registered')
    assert UNREAL.messages('error') == []
