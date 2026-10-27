# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.compile_blueprint(path) and save_blueprint(path) (card 0297, review python-bridge-9), on plain
CPython with the `unreal` stub from conftest.py.

Both logged "blueprint_path parameter not yet implemented" and acted on the focused Blueprint, so saving /Game/BP_A
while BP_B was focused saved BP_B and reported success. They now pass the path to the bridge (UN2CPythonBridge's
CompileFocusedBlueprint / SaveFocusedBlueprint take BlueprintPath since card 0274), which loads that Blueprint and
never looks at the focused one; without a path the bridge call is the same as before.
"""

import importlib
import json

import pytest

from conftest import UNREAL


def envelope(data):
    return json.dumps({'success': True, 'data': data, 'error': None})


class _RecordingBridge:
    """Stand-in for unreal.N2CPythonBridge's compile and save calls: records each call"""

    def __init__(self):
        self.calls = []
        bridge = self

        def compile_focused_blueprint(**kwargs):
            bridge.calls.append(('compile', (), kwargs))
            return envelope({'blueprint_path': kwargs.get('blueprint_path', '/Game/Focused.Focused')})

        def save_focused_blueprint(only_if_dirty, **kwargs):
            bridge.calls.append(('save', (only_if_dirty,), kwargs))
            return envelope({'blueprint_path': kwargs.get('blueprint_path', '/Game/Focused.Focused'), 'was_saved': True})

        self.compile_focused_blueprint = compile_focused_blueprint
        self.save_focused_blueprint = save_focused_blueprint


@pytest.fixture
def stub(monkeypatch):
    bridge = _RecordingBridge()
    monkeypatch.setattr(UNREAL, 'N2CPythonBridge', bridge, raising=False)
    return bridge


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def test_compile_by_path_compiles_that_blueprint(stub, blueprint_module):
    result = blueprint_module.compile_blueprint('/Game/BP_A')
    assert result['success'] is True
    assert stub.calls == [('compile', (), {'blueprint_path': '/Game/BP_A'})]
    assert result['data']['blueprint_path'] == '/Game/BP_A'


def test_save_by_path_saves_that_blueprint(stub, blueprint_module):
    result = blueprint_module.save_blueprint('/Game/BP_A', only_if_dirty=False)
    assert result['success'] is True
    assert stub.calls == [('save', (False,), {'blueprint_path': '/Game/BP_A'})]
    assert result['data']['blueprint_path'] == '/Game/BP_A'


def test_without_a_path_the_focused_blueprint_is_used_as_before(stub, blueprint_module):
    blueprint_module.compile_blueprint()
    blueprint_module.save_blueprint()
    blueprint_module.compile_blueprint('')
    assert stub.calls == [('compile', (), {}), ('save', (True,), {}), ('compile', (), {})]
