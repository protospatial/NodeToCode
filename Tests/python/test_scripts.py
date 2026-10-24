# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests for nodetocode.scripts (the script library behind list/search/get/save/delete/run-python-script) on plain
CPython, with the `unreal` stub from conftest.py pointed at a tmp project.

Known defects are xfail(strict=True) with the review finding id as the reason: the card that fixes one must remove
its marker, because a strict xfail that passes fails the run.
"""

import importlib
import json
import os
import sys

import pytest

from conftest import CONTENT_PYTHON, REPO_ROOT


def save(scripts, name, code='result = 1', description='A test script', **kwargs):
    return scripts.save_script(name, code, description, **kwargs)


def names(result):
    assert result['success'], result['error']
    key = 'scripts' if 'scripts' in result['data'] else 'matches'
    return [s['name'] for s in result['data'][key]]


def registry_entry(path, category='general', **extra):
    entry = {'path': path, 'description': extra.pop('description', 'entry'), 'tags': extra.pop('tags', []),
             'category': category, 'parameters': []}
    entry.update(extra)
    return entry


def write_project_registry(ue, scripts, **top):
    registry = {'version': '1.0', 'scripts': scripts, 'categories': ['general'], 'stats': {}}
    registry.update(top)
    ue.write_registry(json.dumps(registry, indent=2))


def write_file(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


# ---------------------------------------------------------------------------------------------------------------------
# Name and category sanitizing

@pytest.mark.parametrize('raw, expected', [
    ('MyScript', 'myscript'),
    ('My Script!', 'my_script'),
    ('  spaced  out  ', 'spaced_out'),
    ('a__b___c', 'a_b_c'),
    ('../../evil', 'evil'),
    ('..\\..\\evil', 'evil'),
    ('C:/Windows/evil', 'c_windows_evil'),
    ('', 'unnamed_script'),
    ('!!!', 'unnamed_script'),
    ('my-tool', 'my-tool'),  # '-' survives sanitizing; _check_identifier then rejects it on save
])
def test_sanitize_name(scripts_module, raw, expected):
    assert scripts_module._sanitize_name(raw) == expected


@pytest.mark.parametrize('category', [
    '..', '../..', '../../..', 'a/../..', 'sub/dir', 'sub\\dir', '/abs', 'C:/somewhere', 'C:\\somewhere',
    '\\\\server\\share', 'ui tools', '3d', 'my-cat', 'init_unreal', '__init__', 'x' * 65,
])
def test_save_rejects_unsafe_categories_and_writes_nothing(ue, scripts_module, category):
    before = ue.files()
    result = save(scripts_module, 'payload', code='print("payload")', category=category)
    assert not result['success']
    assert 'Invalid category' in result['error'] or 'outside the scripts folder' in result['error']
    assert ue.files() == before, 'a rejected save wrote files'


@pytest.mark.parametrize('name', ['init_unreal', 'INIT_UNREAL', 'my-tool', '3d tools', '3d', 'n' * 65])
def test_save_rejects_unsafe_names(ue, scripts_module, name):
    result = save(scripts_module, name)
    assert not result['success']
    assert 'Invalid script name' in result['error']
    assert not os.path.exists(ue.registry_path)


def test_traversal_name_is_flattened_inside_the_category(ue, scripts_module):
    result = save(scripts_module, '../../init_unreal_x', category='general')
    assert result['success'], result['error']
    assert result['data']['path'] == 'general/init_unreal_x.py'
    assert os.path.isfile(os.path.join(ue.scripts_dir, 'general', 'init_unreal_x.py'))
    assert not any(f.endswith('init_unreal.py') for f in ue.files())


def test_category_is_lowercased_and_defaults_to_general(ue, scripts_module):
    assert save(scripts_module, 'one', category='Gameplay')['data']['category'] == 'gameplay'
    assert save(scripts_module, 'two', category='   ')['data']['category'] == 'general'
    assert save(scripts_module, 'three', category=None)['data']['category'] == 'general'
    registry = json.loads(ue.read_registry())
    assert set(registry['categories']) >= {'general', 'gameplay'}


@pytest.mark.parametrize('field, value, message', [
    ('name', '  ', 'Script name cannot be empty'),
    ('code', '', 'Script code cannot be empty'),
    ('description', ' ', 'Script description cannot be empty'),
])
def test_save_requires_name_code_and_description(ue, scripts_module, field, value, message):
    args = {'name': 'x', 'code': 'result = 1', 'description': 'd'}
    args[field] = value
    result = scripts_module.save_script(**args)
    assert result == {'success': False, 'data': None, 'error': message}


@pytest.mark.xfail(strict=True, reason='python-bridge-43: keywords pass _check_identifier, so scripts.general.class '
                                       'can never be imported with a from-import')
def test_save_rejects_python_keywords(ue, scripts_module):
    assert not save(scripts_module, 'class')['success']


def _link_dir(target, link):
    """A directory symlink, or a junction on Windows when symlinks need a privilege this account lacks."""
    try:
        os.symlink(target, link, target_is_directory=True)
        return
    except (OSError, NotImplementedError, AttributeError) as e:
        error = e
    if sys.platform == 'win32':
        import _winapi
        _winapi.CreateJunction(target, link)
        return
    pytest.skip(f'cannot create a directory link here: {error}')


def test_save_refuses_a_category_folder_that_links_outside(ue, scripts_module, tmp_path):
    outside = tmp_path / 'outside'
    outside.mkdir()
    os.makedirs(ue.scripts_dir)
    _link_dir(str(outside), os.path.join(ue.scripts_dir, 'linked'))
    result = save(scripts_module, 'payload', category='linked')
    assert not result['success']
    assert 'outside the scripts folder' in result['error']
    assert list(outside.iterdir()) == []


# ---------------------------------------------------------------------------------------------------------------------
# Registry load, merge, save and corruption handling

def test_save_creates_the_registry_and_file(ue, scripts_module):
    result = save(scripts_module, 'Health System', code='result = 100', description=' Adds health ',
                  tags=[' Gameplay ', '', 'HP'], parameters=[{'name': 'hp', 'type': 'float'}])
    assert result['success'], result['error']
    assert result['data']['name'] == 'health_system'
    with open(os.path.join(ue.scripts_dir, 'general', 'health_system.py'), encoding='utf-8') as f:
        assert f.read() == 'result = 100'
    registry = json.loads(ue.read_registry())
    entry = registry['scripts']['health_system']
    assert entry['path'] == 'general/health_system.py'
    assert entry['description'] == 'Adds health'
    assert entry['tags'] == ['gameplay', 'hp']
    assert entry['parameters'] == [{'name': 'hp', 'type': 'float'}]
    assert entry['usage_count'] == 0 and entry['last_used'] is None
    assert registry['stats']['total_scripts'] == 1


def test_saved_registry_has_no_internal_fields_and_keeps_unknown_top_level_fields(ue_with_plugin, scripts_module):
    write_project_registry(ue_with_plugin, {}, schema_note='kept')
    assert save(scripts_module, 'first')['success']
    run = scripts_module.run_script('first')  # loads the merged registry with _source/_base_dir, then saves stats
    assert run['success'], run['error']
    text = ue_with_plugin.read_registry()
    assert '_source' not in text and '_base_dir' not in text
    registry = json.loads(text)
    assert registry['schema_note'] == 'kept'
    assert list(registry['scripts']) == ['first'], 'plugin scripts leaked into the project registry'


def test_second_save_keeps_a_backup_and_leaves_no_temp_file(ue, scripts_module):
    assert save(scripts_module, 'first')['success']
    first = ue.read_registry()
    assert save(scripts_module, 'second')['success']
    with open(ue.registry_path + '.bak', encoding='utf-8') as f:
        assert f.read() == first
    assert not os.path.exists(ue.registry_path + '.tmp')
    assert set(json.loads(ue.read_registry())['scripts']) == {'first', 'second'}


def test_missing_registries_list_nothing(ue, scripts_module):
    result = scripts_module.list_scripts()
    assert result['success']
    assert result['data']['scripts'] == [] and result['data']['total'] == 0


def test_bundled_scripts_are_listed_with_their_source(ue_with_plugin, scripts_module):
    result = scripts_module.list_scripts(limit=100)
    listed = {s['name']: s for s in result['data']['scripts']}
    assert 'asset_iterator' in listed and listed['asset_iterator']['source'] == 'plugin'
    assert listed['asset_iterator']['category'] == 'core'


def test_project_script_overrides_plugin_script_of_the_same_name(ue_with_plugin, scripts_module):
    write_file(os.path.join(ue_with_plugin.scripts_dir, 'mine', 'asset_iterator.py'), 'result = "project"')
    write_project_registry(ue_with_plugin, {'asset_iterator': registry_entry('mine/asset_iterator.py', 'mine')},
                           categories=['general', 'mine'])
    got = scripts_module.get_script('asset_iterator')
    assert got['success'], got['error']
    assert got['data']['source'] == 'project' and got['data']['code'] == 'result = "project"'
    merged = scripts_module.list_scripts(limit=100)['data']
    assert merged['categories'] == sorted(set(merged['categories']))
    assert {'core', 'data', 'ui', 'utilities', 'general', 'mine'} <= set(merged['categories'])
    assert [s['name'] for s in merged['scripts']].count('asset_iterator') == 1


CORRUPT_TEXTS = [
    '{"version": "1.0", "scripts": {"keep": {"path": "general/keep.py"}},',  # truncated mid-write
    '{"version": "1.0", "scripts": {}, }',                                    # hand-edited trailing comma
    '[]',                                                                    # valid JSON, wrong shape
    '{"scripts": ["not", "entries"]}',
    '{"scripts": {}, "categories": "general"}',
    '',
]


@pytest.mark.parametrize('text', CORRUPT_TEXTS)
def test_corrupt_project_registry_is_never_overwritten(ue, scripts_module, text):
    ue.write_registry(text)
    result = save(scripts_module, 'new_script')
    assert not result['success']
    assert 'does not parse' in result['error']
    assert ue.read_registry() == text, 'the corrupt registry was overwritten'
    assert not os.path.exists(os.path.join(ue.scripts_dir, 'general', 'new_script.py'))
    copies = [f for f in os.listdir(ue.scripts_dir) if f.startswith('script_registry.json.corrupt-')]
    assert len(copies) == 1
    with open(os.path.join(ue.scripts_dir, copies[0]), encoding='utf-8') as f:
        assert f.read() == text
    # Reads report the problem instead of an empty library
    assert not scripts_module.list_scripts()['success']
    assert not scripts_module.search_scripts('anything')['success']
    assert not scripts_module.delete_script('keep')['success']
    assert ue.read_registry() == text


def test_repeated_loads_of_a_corrupt_registry_keep_one_copy(ue, scripts_module):
    ue.write_registry('{broken')
    for _ in range(3):
        assert not save(scripts_module, 'again')['success']
    copies = [f for f in os.listdir(ue.scripts_dir) if '.corrupt-' in f]
    assert len(copies) == 1


def test_unreadable_project_registry_blocks_saves(ue, scripts_module):
    os.makedirs(ue.registry_path)  # a folder where the file should be: open() raises an OSError
    result = save(scripts_module, 'new_script')
    assert not result['success']
    assert 'Could not read the script registry' in result['error']
    assert os.path.isdir(ue.registry_path)


def test_corrupt_bundled_registry_is_skipped_and_project_scripts_still_work(ue_with_plugin, scripts_module):
    write_file(os.path.join(ue_with_plugin.plugin_scripts_dir, 'script_registry.json'), '{oops')
    assert save(scripts_module, 'mine')['success']
    assert names(scripts_module.list_scripts(limit=100)) == ['mine']
    assert any('bundled script registry' in m for m in ue_with_plugin.unreal.messages('error'))
    assert not any('.corrupt-' in f for f in os.listdir(ue_with_plugin.plugin_scripts_dir))


def test_interrupted_write_is_finished_from_the_temp_file(ue, scripts_module):
    write_file(os.path.join(ue.scripts_dir, 'general', 'kept.py'), 'result = "kept"')
    os.makedirs(ue.scripts_dir, exist_ok=True)
    with open(ue.registry_path + '.tmp', 'w', encoding='utf-8') as f:
        json.dump({'version': '1.0', 'scripts': {'kept': registry_entry('general/kept.py')}}, f)
    assert names(scripts_module.list_scripts()) == ['kept']
    assert os.path.isfile(ue.registry_path) and not os.path.exists(ue.registry_path + '.tmp')


def test_incomplete_temp_file_is_left_alone(ue, scripts_module):
    os.makedirs(ue.scripts_dir)
    with open(ue.registry_path + '.tmp', 'w', encoding='utf-8') as f:
        f.write('{"scripts": {')
    assert names(scripts_module.list_scripts()) == []
    assert os.path.exists(ue.registry_path + '.tmp')


# ---------------------------------------------------------------------------------------------------------------------
# Case handling, listing and search scoring

@pytest.mark.parametrize('text, query, score', [
    ('health', 'health', 1.0),
    ('Health', 'HEALTH', 1.0),
    ('health_system', 'health', 0.8),
    ('Creates health variables', 'CREATES HEALTH', 0.8),
    ('creates health variables', 'variables health', 0.6),
    ('creates health variables', 'var', 0.8),
    ('creates health variables', 'health mana', 0.3 * 1 / 2),
    ('creates health variables', 'health mana stamina', 0.3 * 1 / 3),
    ('creates health variables', 'mana', 0.0),
    ('', 'health', 0.0),
    ('health', '', 0.0),
])
def test_match_query_scores(scripts_module, text, query, score):
    assert scripts_module._match_query(text, query) == pytest.approx(score)


@pytest.fixture
def library(ue, scripts_module):
    assert save(scripts_module, 'health_system', description='Creates health variables', tags=['gameplay'])['success']
    assert save(scripts_module, 'damage', description='Applies damage to health', tags=['combat'])['success']
    assert save(scripts_module, 'spawner', description='Spawns actors', tags=['level'], category='level')['success']
    return scripts_module


def test_get_and_delete_are_case_insensitive(library, ue):
    got = library.get_script('HEALTH_SYSTEM')
    assert got['success'] and got['data']['name'] == 'health_system'
    deleted = library.delete_script('Damage')
    assert deleted['success'] and deleted['data']['name'] == 'damage'
    assert not os.path.exists(os.path.join(ue.scripts_dir, 'general', 'damage.py'))
    assert 'damage' not in json.loads(ue.read_registry())['scripts']
    assert not library.get_script('damage')['success']


@pytest.mark.xfail(strict=True, reason='mcp-tools-python-translation-resources-26 / python-bridge-44: '
                                       'get_script_functions looks names up case-sensitively')
def test_get_script_functions_is_case_insensitive(library):
    assert library.get_script_functions('Health_System')['success']


def test_search_ranks_name_over_description_over_nothing(library):
    result = library.search_scripts('health')
    assert result['success']
    matches = result['data']['matches']
    assert [m['name'] for m in matches] == ['health_system', 'damage']
    assert [m['relevance'] for m in matches] == [0.8, 0.64]
    assert result['data']['total'] == 2 and result['data']['query'] == 'health'


def test_search_exact_name_scores_one(library):
    assert library.search_scripts('HEALTH_SYSTEM')['data']['matches'][0] == {
        'name': 'health_system', 'description': 'Creates health variables', 'category': 'general',
        'tags': ['gameplay'], 'source': 'project', 'relevance': 1.0, 'usage_count': 0}


def test_search_tags_are_weighted(library):
    matches = library.search_scripts('combat')['data']['matches']
    assert [(m['name'], m['relevance']) for m in matches] == [('damage', 0.9)]


def test_search_drops_weak_matches_and_rejects_empty_queries(library):
    assert library.search_scripts('teleport')['data']['matches'] == []
    # one of four words matches a description: 0.3 * 1/4 * 0.8 = 0.06, under the 0.1 threshold
    assert library.search_scripts('actors qq zz kk')['data']['total'] == 0
    for query in ('', '   ', None):
        assert library.search_scripts(query) == {'success': False, 'data': None, 'error': 'Search query cannot be empty'}


def test_search_limit_and_usage_tiebreak(ue, scripts_module):
    write_project_registry(ue, {
        'alpha_tool': registry_entry('general/alpha_tool.py', usage_count=1),
        'beta_tool': registry_entry('general/beta_tool.py', usage_count=5),
        'gamma_tool': registry_entry('general/gamma_tool.py', usage_count=3),
    })
    result = scripts_module.search_scripts('tool', limit=2)['data']
    assert [m['name'] for m in result['matches']] == ['beta_tool', 'gamma_tool']
    assert result['total'] == 3 and result['returned'] == 2


def test_list_filters_sorts_and_limits(ue, scripts_module):
    write_project_registry(ue, {
        'b_script': registry_entry('general/b_script.py', usage_count=2),
        'a_script': registry_entry('general/a_script.py', usage_count=2),
        'c_script': registry_entry('level/c_script.py', 'level', usage_count=9),
        'd_script': registry_entry('general/d_script.py'),
    })
    assert names(scripts_module.list_scripts()) == ['c_script', 'a_script', 'b_script', 'd_script']
    assert names(scripts_module.list_scripts(category='general', limit=2)) == ['a_script', 'b_script']
    assert scripts_module.list_scripts(category='general', limit=2)['data']['total'] == 3
    assert names(scripts_module.list_scripts(category='nope')) == []


def test_script_stats(library):
    library.run_script('damage')
    library.run_script('damage')
    library.run_script('spawner')
    stats = library.get_script_stats()['data']
    assert stats['total_scripts'] == 3
    assert stats['most_used'][:2] == [{'name': 'damage', 'usage_count': 2}, {'name': 'spawner', 'usage_count': 1}]
    assert {r['name'] for r in stats['recently_used']} == {'damage', 'spawner'}


# ---------------------------------------------------------------------------------------------------------------------
# run_script namespaces

def test_run_script_passes_parameters_as_locals_and_params(ue, scripts_module):
    assert save(scripts_module, 'adder', code="result = a + b + params['c']")['success']
    run = scripts_module.run_script('adder', a=1, b=2, c=3)
    assert run == {'success': True, 'error': None,
                   'data': {'script_name': 'adder', 'result': 6, 'parameters_used': ['a', 'b', 'c']}}


def test_run_script_exposes_unreal_and_builtins(ue, scripts_module):
    assert save(scripts_module, 'probe', code='result = (unreal.Paths.project_dir(), len([1, 2]))')['success']
    assert scripts_module.run_script('probe')['data']['result'] == (sys.modules['unreal'].Paths.project_dir(), 2)


def test_run_script_without_result_returns_none(ue, scripts_module):
    assert save(scripts_module, 'quiet', code='x = 1')['success']
    run = scripts_module.run_script('quiet')
    assert run['success'] and run['data']['result'] is None and run['data']['parameters_used'] == []


def test_run_script_namespaces_do_not_leak_between_runs(ue, scripts_module):
    assert save(scripts_module, 'setter', code='leaked = 1\nresult = sorted(params)')['success']
    assert save(scripts_module, 'checker', code="result = 'leaked' in dir() or 'secret' in dir()")['success']
    assert scripts_module.run_script('setter', secret=1)['data']['result'] == ['secret']
    assert scripts_module.run_script('checker')['data']['result'] is False


def test_run_script_reports_exceptions(ue, scripts_module):
    assert save(scripts_module, 'boom', code='raise ValueError("bad input")')['success']
    run = scripts_module.run_script('boom')
    assert run == {'success': False, 'data': None, 'error': 'Script execution failed: bad input'}
    assert json.loads(ue.read_registry())['scripts']['boom']['usage_count'] == 0


def test_run_script_unknown_name(ue, scripts_module):
    assert scripts_module.run_script('missing') == {'success': False, 'data': None, 'error': "Script 'missing' not found"}


def test_run_script_counts_usage_of_project_scripts(ue, scripts_module):
    assert save(scripts_module, 'counted')['success']
    scripts_module.run_script('counted')
    scripts_module.run_script('counted')
    entry = json.loads(ue.read_registry())['scripts']['counted']
    assert entry['usage_count'] == 2 and entry['last_used'].endswith('Z')


def test_run_script_leaves_the_bundled_registry_alone(ue_with_plugin, scripts_module):
    plugin_registry = os.path.join(ue_with_plugin.plugin_scripts_dir, 'script_registry.json')
    with open(plugin_registry, encoding='utf-8') as f:
        registry = json.load(f)
    registry['scripts']['plugin_hello'] = registry_entry('general/plugin_hello.py')
    write_file(plugin_registry, json.dumps(registry))
    write_file(os.path.join(ue_with_plugin.plugin_scripts_dir, 'general', 'plugin_hello.py'), 'result = "hi"')
    with open(plugin_registry, encoding='utf-8') as f:
        before = f.read()
    run = scripts_module.run_script('plugin_hello')
    assert run['success'] and run['data']['result'] == 'hi'
    with open(plugin_registry, encoding='utf-8') as f:
        assert f.read() == before
    assert not os.path.exists(ue_with_plugin.registry_path)


@pytest.mark.xfail(strict=True, reason='python-bridge-5: run_script execs with separate globals and locals, so '
                                       "functions cannot see the script's own imports")
def test_run_script_functions_see_module_level_names(ue, scripts_module):
    code = 'import json\n\ndef dump():\n    return json.dumps({"a": 1})\n\nresult = dump()\n'
    assert save(scripts_module, 'uses_import', code=code)['success']
    run = scripts_module.run_script('uses_import')
    assert run['success'], run['error']
    assert run['data']['result'] == '{"a": 1}'


@pytest.mark.xfail(strict=True, reason='python-bridge-5: run_script updates usage stats under the name as typed, not '
                                       'the registry name')
def test_run_script_counts_usage_under_the_registry_name(ue, scripts_module):
    assert save(scripts_module, 'counted')['success']
    assert scripts_module.run_script('COUNTED')['success']
    assert json.loads(ue.read_registry())['scripts']['counted']['usage_count'] == 1


# ---------------------------------------------------------------------------------------------------------------------
# get_script_functions

FUNCTIONS_SOURCE = '''
import math
from typing import Dict, List


def area(radius: float, scale: float = 1.0, *extra, label: str = "r", **opts) -> float:
    """Area of a circle."""
    return math.pi * radius * radius * scale


def _private():
    pass


def names(items: List[str], flags=[1, 2], table={"a": 1}):
    return items
'''


def test_get_script_functions_extracts_signatures(ue, scripts_module):
    assert save(scripts_module, 'geometry', code=FUNCTIONS_SOURCE, category='math_tools')['success']
    result = scripts_module.get_script_functions('geometry')
    assert result['success'], result['error']
    data = result['data']
    assert data['category'] == 'math_tools' and data['source'] == 'project' and data['function_count'] == 2
    area, listing = data['functions']
    assert area == {
        'name': 'area', 'return_type': 'float', 'docstring': 'Area of a circle.', 'line_number': 6,
        'parameters': [
            {'name': 'radius', 'type_hint': 'float', 'default_value': None},
            {'name': 'scale', 'type_hint': 'float', 'default_value': '1.0'},
            {'name': '*extra', 'type_hint': None, 'default_value': None},
            {'name': 'label', 'type_hint': 'str', 'default_value': "'r'"},
            {'name': '**opts', 'type_hint': None, 'default_value': None},
        ]}
    assert listing['name'] == 'names'
    assert [(p['name'], p['type_hint'], p['default_value']) for p in listing['parameters']] == [
        ('items', 'List[str]', None), ('flags', None, '[1, 2]'), ('table', None, "{'a': 1}")]


def test_get_script_functions_reports_syntax_errors(ue, scripts_module):
    assert save(scripts_module, 'broken', code='def f(:\n    pass')['success']
    result = scripts_module.get_script_functions('broken')
    assert not result['success'] and result['error'].startswith('Script has syntax errors')


@pytest.mark.xfail(strict=True, reason='python-bridge-44: ast.walk lists methods and nested functions as if they '
                                       'were importable module functions')
def test_get_script_functions_lists_module_level_functions_only(ue, scripts_module):
    code = 'class Helper:\n    def run(self):\n        pass\n\ndef outer():\n    def inner():\n        pass\n'
    assert save(scripts_module, 'nested', code=code)['success']
    assert [f['name'] for f in scripts_module.get_script_functions('nested')['data']['functions']] == ['outer']


@pytest.mark.xfail(strict=True, reason='python-bridge-44: tuple subscripts render as Dict[(str, int)]')
def test_get_script_functions_renders_tuple_subscripts(ue, scripts_module):
    code = 'from typing import Dict\n\ndef f() -> Dict[str, int]:\n    return {}\n'
    assert save(scripts_module, 'typed', code=code)['success']
    assert scripts_module.get_script_functions('typed')['data']['functions'][0]['return_type'] == 'Dict[str, int]'


# ---------------------------------------------------------------------------------------------------------------------
# Path validation of registry entries

@pytest.fixture
def escaping_entries(ue, tmp_path):
    """A hand-edited project registry whose entries point outside the scripts folder, at files that exist."""
    outside = tmp_path / 'outside.py'
    outside.write_text('result = "outside"', encoding='utf-8')
    absolute = tmp_path / 'absolute.py'
    absolute.write_text('result = "absolute"', encoding='utf-8')
    write_project_registry(ue, {
        'relative_escape': registry_entry('../../../outside.py'),
        'absolute_escape': registry_entry(str(absolute)),
    })
    return [outside, absolute]


@pytest.mark.parametrize('name', ['relative_escape', 'absolute_escape'])
def test_registry_paths_outside_the_scripts_folder_are_refused(escaping_entries, scripts_module, name):
    for call in (scripts_module.get_script, scripts_module.get_script_functions, scripts_module.run_script,
                 scripts_module.delete_script):
        result = call(name)
        assert not result['success'], call.__name__
        assert 'points outside the scripts folder' in result['error'], call.__name__
    assert all(p.exists() for p in escaping_entries), 'delete_script removed a file outside the scripts folder'


def test_bundled_registry_paths_outside_the_plugin_are_refused(ue_with_plugin, scripts_module, tmp_path):
    plugin_registry = os.path.join(ue_with_plugin.plugin_scripts_dir, 'script_registry.json')
    with open(plugin_registry, encoding='utf-8') as f:
        registry = json.load(f)
    registry['scripts']['sneaky'] = registry_entry('../../../../../../outside.py')
    write_file(plugin_registry, json.dumps(registry))
    (tmp_path / 'outside.py').write_text('x = 1', encoding='utf-8')
    result = scripts_module.get_script('sneaky')
    assert not result['success'] and 'points outside the scripts folder' in result['error']


def test_missing_script_file_is_reported(ue, scripts_module):
    write_project_registry(ue, {'ghost': registry_entry('general/ghost.py')})
    result = scripts_module.get_script('ghost')
    assert not result['success'] and result['error'].startswith('Script file not found')


def test_plugin_scripts_cannot_be_deleted(ue_with_plugin, scripts_module):
    result = scripts_module.delete_script('asset_iterator')
    assert not result['success'] and 'read-only' in result['error']
    assert os.path.isfile(os.path.join(ue_with_plugin.plugin_scripts_dir, 'core', 'asset_iterator.py'))


def test_is_inside(scripts_module, tmp_path):
    base = tmp_path / 'base'
    (base / 'sub').mkdir(parents=True)
    assert scripts_module._is_inside(str(base), str(base / 'sub' / 'x.py'))
    assert scripts_module._is_inside(str(base), str(base))
    assert not scripts_module._is_inside(str(base), str(base / '..' / 'x.py'))
    assert not scripts_module._is_inside(str(base), str(tmp_path / 'base_sibling' / 'x.py'))


# ---------------------------------------------------------------------------------------------------------------------
# Where the bundled scripts are looked for

@pytest.mark.xfail(strict=True, reason='mcp-tools-python-translation-resources-19: bundled scripts are only found at '
                                       '<Project>/Plugins/NodeToCode, not in an engine (Fab) install')
def test_bundled_scripts_found_in_an_engine_install(real_plugin_lookup, scripts_module):
    # No NodeToCode under the project's Plugins folder, as with a Fab install under Engine/Plugins/Marketplace
    assert 'asset_iterator' in names(scripts_module.list_scripts(limit=100))


@pytest.mark.xfail(strict=True, reason='mcp-tools-python-translation-resources-19: bundled scripts are not found when '
                                       'the plugin folder is not named NodeToCode')
def test_bundled_scripts_found_in_a_renamed_plugin_folder(real_plugin_lookup, scripts_module):
    real_plugin_lookup.install_plugin('NodeToCode-main')
    assert 'asset_iterator' in names(scripts_module.list_scripts(limit=100))


# ---------------------------------------------------------------------------------------------------------------------
# Import resolution: saved scripts are documented as importable as `from scripts.<category>.<name> import ...`

def _purge_scripts_modules():
    for module in [m for m in sys.modules if m == 'scripts' or m.startswith('scripts.')]:
        del sys.modules[module]
    importlib.invalidate_caches()


@pytest.fixture
def ue_sys_path(ue_with_plugin, monkeypatch):
    """sys.path as the editor has it: the project's and the plugin's Content/Python, nothing from this checkout."""
    plugin_python = os.path.dirname(ue_with_plugin.plugin_scripts_dir)
    os.makedirs(ue_with_plugin.content_python, exist_ok=True)
    drop = {os.path.normcase(os.path.abspath(p)) for p in (CONTENT_PYTHON, REPO_ROOT, os.getcwd())}
    rest = [p for p in sys.path if p and os.path.normcase(os.path.abspath(p)) not in drop]
    monkeypatch.setattr(sys, 'path', [ue_with_plugin.content_python, plugin_python] + rest)
    _purge_scripts_modules()
    yield ue_with_plugin
    _purge_scripts_modules()


HELPER_CODE = 'def answer():\n    return 42\n'


def test_saved_script_in_a_new_category_is_importable(ue_sys_path, scripts_module):
    assert save(scripts_module, 'spawn_helpers', code=HELPER_CODE, category='gameplay')['success']
    module = importlib.import_module('scripts.gameplay.spawn_helpers')
    assert module.answer() == 42
    assert os.path.commonpath([module.__file__, ue_sys_path.scripts_dir]) == ue_sys_path.scripts_dir


def test_bundled_packages_are_importable(ue_sys_path):
    module = importlib.import_module('scripts.utilities')
    assert os.path.dirname(module.__file__).startswith(ue_sys_path.plugin_scripts_dir)


@pytest.mark.xfail(strict=True, reason='mcp-tools-python-translation-resources-20: a project category named like a '
                                       "bundled package (core, data, ui, utilities) is shadowed by the plugin's "
                                       'regular package')
@pytest.mark.parametrize('category', ['utilities', 'core'])
def test_saved_script_in_a_bundled_category_is_importable(ue_sys_path, scripts_module, category):
    assert save(scripts_module, 'spawn_helpers', code=HELPER_CODE, category=category)['success']
    assert importlib.import_module(f'scripts.{category}.spawn_helpers').answer() == 42
