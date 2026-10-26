# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for Content/Python/init_unreal.py (cards 0175 and 0177) on plain CPython with the `unreal` stub from
conftest.py. The editor runs this file at startup on every engine. It registers NodeToCode's Agent Skills and
NodeToCodeTools only on UE 5.8+ with Epic's ToolsetRegistry plugin loaded; anywhere else it must import nothing of
either, log nothing and never raise, and a failure of one part must not stop the other. The registration itself is
checked in the editor (NodeToCode.Toolsets.PythonTests, Tests/mcp_probe/live_toolset_0175.py).
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
    prefixes = ('nodetocode.toolsets', 'nodetocode.skills', 'toolset_registry')
    for name in [m for m in sys.modules if m.startswith(prefixes)]:
        monkeypatch.delitem(sys.modules, name)
    import nodetocode
    for name in ('toolsets', 'skills'):
        monkeypatch.delattr(nodetocode, name, raising=False)

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
    return [m for m in sys.modules if m.startswith(('nodetocode.toolsets', 'nodetocode.skills'))]


def _registry_with_agent_skill(monkeypatch, registered):
    """A toolset_registry package with only agent_skill (no registration module), and unreal.AgentSkill: the skills
    register into `registered`, the toolsets fail to import"""
    package = types.ModuleType('toolset_registry')
    package.__path__ = []
    module = types.ModuleType('toolset_registry.agent_skill')

    def agent_skill(cls):
        registered.append(cls)
        return cls
    module.agent_skill = agent_skill
    package.agent_skill = module
    monkeypatch.setitem(sys.modules, 'toolset_registry.agent_skill', module)
    monkeypatch.setattr(UNREAL, 'AgentSkill', type('AgentSkill', (), {}), raising=False)
    return package


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


def test_a_registration_failure_is_one_warning_per_part_not_an_exception(engine):
    # The registry package exists but has neither agent_skill nor registration: importing the skills and the toolsets
    # fails inside the editor's startup script, which must only warn, once for each
    engine('5.8.3-0+++UE5+Release-5.8', registry_classes=True, registry_package=types.ModuleType('toolset_registry'))
    _run()
    warnings = UNREAL.messages('warning')
    assert len(warnings) == 2
    assert warnings[0].startswith('NodeToCode: Agent Skills not registered')
    assert warnings[1].startswith('NodeToCode: NodeToCodeTools toolset not registered')
    assert UNREAL.messages('error') == []


def test_skills_register_on_5_8_even_when_the_toolset_fails(engine, monkeypatch):
    registered = []
    engine('5.8.3-0+++UE5+Release-5.8', registry_classes=True,
           registry_package=_registry_with_agent_skill(monkeypatch, registered))
    _run()
    assert [c.__name__ for c in registered] == [
        'NodeToCodeBlueprintComprehensionSkill', 'NodeToCodeBlueprintToCppSkill', 'NodeToCodeBlueprintMigrationSkill']
    warnings = UNREAL.messages('warning')
    assert len(warnings) == 1 and warnings[0].startswith('NodeToCode: NodeToCodeTools toolset not registered')
    assert UNREAL.messages('error') == []


@pytest.mark.parametrize('version', ['5.6.1-44394996+++UE5+Release-5.6', '5.7.4-0+++UE5+Release-5.7'])
def test_no_skills_before_5_8_even_with_the_registry(engine, monkeypatch, version):
    registered = []
    engine(version, registry_classes=True, registry_package=_registry_with_agent_skill(monkeypatch, registered))
    _run()
    assert registered == []
    assert UNREAL.logs == []
    assert _toolset_modules() == []
