# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Integrity of the bundled script library: Content/Python/scripts/script_registry.json against the .py files beside
it. Every registered path must exist inside the folder, and every script file must be registered, so a script added
or renamed without a registry entry (or the reverse) fails here instead of vanishing from list/search-python-scripts.
"""

import ast
import json
import os

import pytest

from conftest import PLUGIN_SCRIPTS

REGISTRY_PATH = os.path.join(PLUGIN_SCRIPTS, 'script_registry.json')


@pytest.fixture(scope='module')
def registry():
    import nodetocode.scripts as scripts
    with open(REGISTRY_PATH, 'r', encoding='utf-8') as f:
        return scripts._parse_registry(f.read())


def script_files():
    found = []
    for folder, dirs, files in os.walk(PLUGIN_SCRIPTS):
        dirs[:] = [d for d in dirs if d != '__pycache__']
        for name in files:
            if name.endswith('.py') and name != '__init__.py':
                found.append(os.path.relpath(os.path.join(folder, name), PLUGIN_SCRIPTS).replace('\\', '/'))
    return sorted(found)


def test_registry_has_scripts(registry):
    assert registry['scripts'], 'the bundled registry lists no scripts'
    assert registry['stats'].get('total_scripts', len(registry['scripts'])) == len(registry['scripts'])


def test_every_registered_path_exists_inside_the_scripts_folder(registry):
    import nodetocode.scripts as scripts
    problems = []
    for name, info in registry['scripts'].items():
        path = info.get('path', '')
        full = os.path.join(PLUGIN_SCRIPTS, path)
        if not path or os.path.isabs(path) or '\\' in path:
            problems.append(f'{name}: path {path!r} is not a relative, /-separated path')
        elif not scripts._is_inside(PLUGIN_SCRIPTS, full):
            problems.append(f'{name}: path {path!r} leaves the scripts folder')
        elif not os.path.isfile(full):
            problems.append(f'{name}: {path} does not exist')
    assert not problems, '\n'.join(problems)


def test_every_script_file_is_registered(registry):
    registered = {info.get('path') for info in registry['scripts'].values()}
    unregistered = [f for f in script_files() if f not in registered]
    assert not unregistered, f'script files missing from script_registry.json: {unregistered}'


def test_entries_are_consistent(registry):
    import nodetocode.scripts as scripts
    problems = []
    categories = set(registry.get('categories', []))
    for name, info in registry['scripts'].items():
        path = info.get('path', '')
        category = info.get('category')
        error = scripts._check_identifier(name, 'script name') or scripts._check_identifier(category, 'category')
        if error:
            problems.append(f'{name}: {error}')
        if name != name.lower():
            problems.append(f'{name}: registry names are lowercase (save_script lowercases them)')
        if path != f'{category}/{name}.py':
            problems.append(f'{name}: path {path!r} is not <category>/<name>.py for category {category!r}')
        if category not in categories:
            problems.append(f'{name}: category {category!r} is missing from the categories list')
        if not str(info.get('description', '')).strip():
            problems.append(f'{name}: no description (search scores descriptions)')
        if not isinstance(info.get('tags', []), list) or not isinstance(info.get('parameters', []), list):
            problems.append(f'{name}: tags and parameters must be lists')
    assert not problems, '\n'.join(problems)


@pytest.mark.parametrize('relative', script_files())
def test_script_parses_on_this_python(relative):
    """The bundled scripts must at least compile on the running interpreter (UE 5.6-5.8 embed Python 3.11)."""
    with open(os.path.join(PLUGIN_SCRIPTS, relative), 'r', encoding='utf-8') as f:
        ast.parse(f.read(), filename=relative)
