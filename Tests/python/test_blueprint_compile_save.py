# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.compile_blueprint(path) and save_blueprint(path) (card 0297, review python-bridge-9), on plain
CPython with the `unreal` stub from conftest.py.

Both logged "blueprint_path parameter not yet implemented" and acted on the focused Blueprint, so saving /Game/BP_A
while BP_B was focused saved BP_B and reported success. They pass the path on: since card 0342 to compile-blueprint
and save-blueprint through InvokeTool, which load that Blueprint and never look at the focused one; without a path
the tools act on the focused Blueprint. The tools' results carry the 1.x keys (blueprint_path, had_errors, was_saved)
for one release.
"""

import importlib

import pytest


def compiled(arguments, errors=0):
    return {'success': errors == 0, 'blueprintName': 'BP_A',
            'blueprintPath': arguments.get('blueprintPath', '/Game/Focused') + '.BP_A',
            'compilationStatus': {'previousStatus': 'Dirty', 'currentStatus': 'Error' if errors else 'UpToDate'},
            'results': {'errorCount': errors, 'warningCount': 0, 'noteCount': 0,
                        'messages': [{'severity': 'Error', 'message': 'broken'}] * errors},
            'message': 'Blueprint compilation failed with %d error(s) and 0 warning(s)' % errors if errors
            else 'Blueprint compiled successfully'}


def saved(arguments, wrote=True):
    data = {'success': True, 'blueprintName': 'BP_A',
            'blueprintPath': arguments.get('blueprintPath', '/Game/Focused') + '.BP_A',
            'message': 'Blueprint saved successfully' if wrote else 'Blueprint is already saved (not dirty)',
            'packageInfo': {'isDirty': False}}
    if wrote:
        data['sourceControl'] = {'enabled': False}
    return data


@pytest.fixture
def blueprint_module():
    return importlib.import_module('nodetocode.blueprint')


def test_compile_by_path_compiles_that_blueprint(tools, blueprint_module):
    tools.answers['compile-blueprint'] = compiled
    result = blueprint_module.compile_blueprint('/Game/BP_A')
    assert result['success'] is True
    assert tools.calls == [('compile-blueprint', {'blueprintPath': '/Game/BP_A'})]
    assert result['data']['blueprintPath'] == '/Game/BP_A.BP_A'
    assert result['data']['blueprint_path'] == '/Game/BP_A.BP_A'
    assert result['data']['had_errors'] is False and result['data']['status'] == 'UpToDate'


def test_compile_errors_fail_with_the_tools_message_and_keep_the_data(tools, blueprint_module):
    tools.answers['compile-blueprint'] = lambda arguments: compiled(arguments, errors=2)
    result = blueprint_module.compile_blueprint('/Game/BP_A')
    assert result['success'] is False
    assert result['error'] == 'Blueprint compilation failed with 2 error(s) and 0 warning(s)'
    assert result['data']['had_errors'] is True and result['data']['error_count'] == 2
    assert result['data']['messages'] == result['data']['results']['messages']


def test_save_by_path_saves_that_blueprint(tools, blueprint_module):
    tools.answers['save-blueprint'] = saved
    result = blueprint_module.save_blueprint('/Game/BP_A', only_if_dirty=False)
    assert result['success'] is True
    assert tools.calls == [('save-blueprint', {'saveOnlyIfDirty': False, 'blueprintPath': '/Game/BP_A'})]
    assert result['data']['blueprint_path'] == '/Game/BP_A.BP_A'
    assert result['data']['was_saved'] is True


def test_a_clean_blueprint_is_reported_as_not_saved(tools, blueprint_module):
    tools.answers['save-blueprint'] = lambda arguments: saved(arguments, wrote=False)
    result = blueprint_module.save_blueprint('/Game/BP_A')
    assert result['success'] is True
    assert result['data']['was_saved'] is False and result['data']['was_dirty'] is False


def test_without_a_path_the_focused_blueprint_is_used_as_before(tools, blueprint_module):
    tools.answers.update({'compile-blueprint': compiled, 'save-blueprint': saved})
    blueprint_module.compile_blueprint()
    blueprint_module.save_blueprint()
    blueprint_module.compile_blueprint('')
    assert tools.calls == [('compile-blueprint', {}), ('save-blueprint', {'saveOnlyIfDirty': True}),
                           ('compile-blueprint', {})]
