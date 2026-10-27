# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
In-editor tests for the bundled script library (nodetocode.scripts) with the real `unreal` module.
Run through run_editor_tests.py in this folder (see Tests/python/README.md), not pytest.
"""

import json
import os

import unreal

import nodetocode.scripts as scripts


def _real(path):
    return os.path.normcase(os.path.realpath(path))


def _plugin_scripts_dir():
    """Content/Python/scripts of the NodeToCode plugin the editor loaded, from the plugin manager."""
    content_dir = unreal.PluginBlueprintLibrary.get_plugin_content_dir('NodeToCode')
    assert content_dir, 'the plugin manager does not know the NodeToCode plugin'
    return os.path.join(unreal.Paths.convert_relative_path_to_full(content_dir), 'Python', 'scripts')


def _bundled_names():
    with open(os.path.join(_plugin_scripts_dir(), 'script_registry.json'), 'r', encoding='utf-8') as f:
        return set(json.load(f)['scripts'])


def test_bundled_scripts_dir_is_the_loaded_plugins_folder():
    got, expected = _real(scripts._get_plugin_scripts_dir()), _real(_plugin_scripts_dir())
    assert got == expected, f'{got} != {expected}'


def test_every_bundled_script_is_listed():
    expected = _bundled_names()
    assert len(expected) >= 13, sorted(expected)
    result = scripts.list_scripts(limit=1000)
    assert result['success'], result.get('error')
    listed = {s['name'] for s in result['data']['scripts']}
    missing = expected - listed
    assert not missing, f'bundled scripts not listed: {sorted(missing)}'


def test_bundled_script_functions_are_read():
    result = scripts.get_script_functions('asset_iterator')
    assert result['success'], result.get('error')
    names = [f['name'] for f in result['data']['functions']]
    assert 'find_assets_by_type' in names, names
