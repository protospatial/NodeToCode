# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The Agent Skills with a script (card 0306, decision 0010): asset-iterator, asset-validator and
project-folder-scaffold, each a SKILL.md with its script under scripts/, loaded in the editor with
nodetocode.skill_script and run through run-python. On plain CPython with the `unreal` stub from conftest.py (the
AssetRegistry and EditorAssetLibrary are small fakes here): every script parses and loads, every SKILL.md passes
Agents/sync_skills.py's checks, and the review findings fixed in the port stay fixed (python-bridge-31 to -34, -39).
NodeToCode.Python.SkillScripts runs the scripts on real assets in the editor.
"""

import ast
import importlib.util
import json
from types import SimpleNamespace
import os

import pytest

from conftest import REPO_ROOT, UNREAL

import nodetocode
from nodetocode import utils

SKILLS = os.path.join(REPO_ROOT, 'Agents', 'claude-code', 'nodetocode', 'skills')
SCRIPTED = {'asset-iterator': 'asset_iterator', 'asset-validator': 'asset_validator',
            'project-folder-scaffold': 'project_folder_scaffold'}


@pytest.fixture(scope='module')
def sync_skills():
    spec = importlib.util.spec_from_file_location('sync_skills', os.path.join(REPO_ROOT, 'Agents', 'sync_skills.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---- The skills and their scripts ----

def test_the_loader_finds_the_repository_skills():
    assert os.path.normpath(utils.skills_dir()) == os.path.normpath(SKILLS)


@pytest.mark.parametrize('skill', sorted(SCRIPTED))
def test_each_script_parses_and_its_skill_md_is_valid(sync_skills, skill):
    folder = os.path.join(SKILLS, skill)
    scripts = [f for f in os.listdir(os.path.join(folder, 'scripts')) if f.endswith('.py')]
    assert scripts == [SCRIPTED[skill] + '.py']
    for name in scripts:
        with open(os.path.join(folder, 'scripts', name), encoding='utf-8') as f:
            source = f.read()
        tree = ast.parse(source, name)
        assert ast.get_docstring(tree), name
        assert 'except:' not in source, f'{name} has a bare except'
    tools, _unparsed = sync_skills.registered_tools()
    assert sync_skills.check_skill(folder, tools, sync_skills.skill_names()) == []
    with open(os.path.join(folder, 'SKILL.md'), encoding='utf-8') as f:
        assert f"n2c.skill_script('{skill}')" in f.read(), 'SKILL.md shows how to load the script'


@pytest.mark.parametrize('skill', sorted(SCRIPTED))
def test_each_script_loads_through_skill_script(skill):
    module = nodetocode.skill_script(skill)
    assert module is nodetocode.skill_script(skill, SCRIPTED[skill])  # loaded once
    public = [n for n, v in vars(module).items() if callable(v) and not n.startswith('_')
              and getattr(v, '__module__', '') == module.__name__]
    assert len(public) >= 4, public
    # Nothing is written into the plugin's skills folder (sync_skills would mirror it)
    assert not os.path.exists(os.path.join(SKILLS, skill, 'scripts', '__pycache__'))


@pytest.mark.parametrize('skill,script', [('../asset-validator', None), ('Asset-Validator', None), ('a/b', None),
                                          ('asset-validator', '../x'), ('asset-validator', 'asset_validator.py')])
def test_skill_script_refuses_names_that_are_not_skill_or_script_names(skill, script):
    with pytest.raises(ValueError):
        nodetocode.skill_script(skill, script)


def test_skill_script_says_when_the_file_is_missing():
    with pytest.raises(FileNotFoundError):
        nodetocode.skill_script('asset-validator', 'no_such_script')
    with pytest.raises(FileNotFoundError):
        nodetocode.skill_script('no-such-skill')


# ---- AssetRegistry fakes ----

class _ClassPath:
    def __init__(self, name):
        self.asset_name = name


class _AssetData:
    def __init__(self, package_name, class_name, parent=None):
        self.package_name = package_name
        self.asset_name = package_name.rsplit('/', 1)[-1]
        self.package_path = package_name.rsplit('/', 1)[0]
        self.asset_class_path = _ClassPath(class_name)
        self._parent = parent

    def is_valid(self):
        return True

    def get_tag_value(self, tag):
        return self._parent if tag == 'ParentClass' else None


class _Registry:
    def __init__(self, assets):
        self.assets = assets

    def get_assets_by_path(self, folder, recursive, _on_disk):
        return [a for a in self.assets if a.package_path == folder or (recursive and a.package_path.startswith(folder + '/'))]

    def get_assets_by_class(self, _class_path, _subclasses):
        return list(self.assets)


@pytest.fixture
def registry(monkeypatch):
    reg = _Registry([])

    class Helpers:
        @staticmethod
        def get_asset_registry():
            return reg

    class TopLevelAssetPath(str):
        pass

    by_path = {}

    class EditorAssetLibrary:
        loaded = {}

        @staticmethod
        def find_asset_data(path):
            return by_path.get(path.split('.')[0])

        @staticmethod
        def does_asset_exist(path):
            return path in EditorAssetLibrary.loaded

        @staticmethod
        def load_asset(path):
            return EditorAssetLibrary.loaded.get(path)

    monkeypatch.setattr(UNREAL, 'AssetRegistryHelpers', Helpers, raising=False)
    monkeypatch.setattr(UNREAL, 'TopLevelAssetPath', TopLevelAssetPath, raising=False)
    monkeypatch.setattr(UNREAL, 'EditorAssetLibrary', EditorAssetLibrary, raising=False)

    def add(package_name, class_name, parent=None):
        data = _AssetData(package_name, class_name, parent)
        reg.assets.append(data)
        by_path[package_name] = data
        return data
    reg.add = add
    reg.library = EditorAssetLibrary
    return reg


# ---- asset-iterator: python-bridge-34, -39 ----

def test_find_assets_by_type_keeps_to_the_folder(registry):
    iterator = nodetocode.skill_script('asset-iterator')
    for package in ('/Game/Char/BP_A', '/Game/Char/Sub/BP_B', '/Game/Characters/BP_C'):
        registry.add(package, 'Blueprint')
    flat = iterator.find_assets_by_type('Blueprint', '/Game/Char', recursive=False)['data']['assets']
    deep = iterator.find_assets_by_type('Blueprint', '/Game/Char/', recursive=True)['data']['assets']
    assert [a['name'] for a in flat] == ['BP_A']  # was empty: package_path held the package name
    assert sorted(a['name'] for a in deep) == ['BP_A', 'BP_B']  # /Game/Characters is a sibling, not a subfolder
    assert flat[0] == {'name': 'BP_A', 'path': '/Game/Char/BP_A.BP_A', 'class_name': 'Blueprint',
                       'package_name': '/Game/Char/BP_A', 'package_path': '/Game/Char'}


def test_iterate_assets_counts_load_failures_once(registry):
    iterator = nodetocode.skill_script('asset-iterator')
    registry.library.loaded = {'/Game/A.A': 'a', '/Game/C.C': 'c'}
    entries = [{'path': '/Game/A.A'}, {'path': '/Game/Missing.Missing'}, {'path': '/Game/C.C'}]
    data = iterator.iterate_assets(entries, lambda asset: asset.upper())['data']
    assert (data['processed'], data['failed']) == (2, 1)  # was processed 2 - 1 = 1
    assert [r.get('result', r.get('error')) for r in data['results']] == ['A', 'could not be loaded', 'C']
    raised = iterator.iterate_assets(entries[:1], lambda asset: 1 / 0)['data']
    assert (raised['processed'], raised['failed']) == (0, 1)


def test_get_blueprints_by_parent_reads_the_parent_class_tag(registry):
    iterator = nodetocode.skill_script('asset-iterator')
    registry.add('/Game/BP_Door', 'Blueprint', "/Script/CoreUObject.Class'/Script/Engine.Actor'")
    registry.add('/Game/BP_Hero', 'Blueprint', "/Script/CoreUObject.Class'/Script/Engine.Character'")
    found = iterator.get_blueprints_by_parent('Actor')['data']['blueprints']
    assert [b['name'] for b in found] == ['BP_Door']


# ---- asset-validator: python-bridge-31, -32, -33 ----

@pytest.mark.parametrize('class_name,prefix', [('WidgetBlueprint', 'WBP_'), ('AnimBlueprint', 'ABP_'),
                                               ('MaterialInstanceConstant', 'MI_'), ('TextureRenderTarget2D', 'RT_'),
                                               ('Blueprint', 'BP_'), ('Material', 'M_'), ('SomethingElse', None)])
def test_expected_prefix_uses_the_exact_class(class_name, prefix):
    assert nodetocode.skill_script('asset-validator').expected_prefix(class_name) == prefix


def test_check_naming_accepts_wbp_abp_and_mi(registry):
    validator = nodetocode.skill_script('asset-validator')
    for package, class_name in (('/Game/T/WBP_Menu', 'WidgetBlueprint'), ('/Game/T/ABP_Hero', 'AnimBlueprint'),
                                ('/Game/T/MI_Rock', 'MaterialInstanceConstant'), ('/Game/T/BP_Door', 'Blueprint'),
                                ('/Game/T/GM_Main', 'Blueprint'), ('/Game/T/Menu', 'WidgetBlueprint'),
                                ('/Game/T/Odd', 'SomethingElse')):
        registry.add(package, class_name)
    data = validator.check_naming('/Game/T')['data']
    assert (data['valid'], data['invalid'], data['unchecked']) == (5, 1, 1)
    assert [(i['name'], i['expected_prefix']) for i in data['issues']] == [('Menu', 'WBP_')]


def test_validate_blueprint_reports_compile_errors(registry, tools):
    validator = nodetocode.skill_script('asset-validator')
    registry.add('/Game/BP_Broken', 'Blueprint')
    tools.answers['compile-blueprint'] = {
        'success': False, 'blueprintName': 'BP_Broken', 'message': 'Blueprint compilation failed with 1 error(s)',
        'compilationStatus': {'currentStatus': 'Error'},
        'results': {'errorCount': 1, 'warningCount': 0, 'messages': [{'severity': 'Error', 'message': 'no Target'}]}}
    data = validator.validate_blueprint('/Game/BP_Broken', compile_check=True)['data']
    assert data['is_valid'] is False  # was always valid: the compile result was never read
    assert data['issues'] == ['Compile error: no Target'] and data['compile']['error_count'] == 1
    assert validator.validate_blueprint('/Game/BP_Broken')['data']['compile_checked'] is False  # opt-in
    assert [name for name, _ in tools.calls] == ['compile-blueprint']


def test_check_for_empty_blueprints_counts_logic_nodes(registry, monkeypatch):
    validator = nodetocode.skill_script('asset-validator')
    registry.add('/Game/E/BP_Empty', 'Blueprint')
    registry.add('/Game/E/BP_Full', 'Blueprint')
    ghost = {'type': 'Event', 'disabled': True}
    entry = {'type': 'FunctionEntry'}
    graphs = {'/Game/E/BP_Empty.BP_Empty': [[entry], [ghost, ghost, ghost]],
              '/Game/E/BP_Full.BP_Full': [[entry, {'type': 'CallFunction'}], [ghost]]}

    class Subsystem:
        """UN2CEditorSubsystem.GetBlueprintN2CJson (card 0301): the struct, with the document as text"""
        @staticmethod
        def get_blueprint_n2c_json(path, *args):
            doc = {'graphs': [{'name': 'G%d' % i, 'nodes': nodes} for i, nodes in enumerate(graphs[path])]}
            return SimpleNamespace(blueprint_path=path, blueprint_name=path.rsplit('.', 1)[-1], graph_count=len(doc['graphs']),
                                   node_count=0, estimated_tokens=0, n2c_json=json.dumps(doc), file_path='', file_bytes=0,
                                   graph_name='', graph_guid='', graph_kind='', resolved_by='', deprecated_id='', warning='')
    monkeypatch.setattr(UNREAL, 'N2CEditorSubsystem', Subsystem, raising=False)
    monkeypatch.setattr(UNREAL, 'get_editor_subsystem', lambda cls: Subsystem, raising=False)
    data = validator.check_for_empty_blueprints('/Game/E')['data']
    assert [b['name'] for b in data['empty_blueprints']] == ['BP_Empty']  # was every Blueprint
    assert data['checked'] == 2 and data['unreadable'] == []


# ---- project-folder-scaffold ----

def test_scaffold_refuses_paths_outside_project_content(monkeypatch):
    scaffold = nodetocode.skill_script('project-folder-scaffold')
    made = []

    class EditorAssetLibrary:
        @staticmethod
        def does_directory_exist(path):
            return path == '/Game/F'

        @staticmethod
        def make_directory(path):
            made.append(path)
            return True
    monkeypatch.setattr(UNREAL, 'EditorAssetLibrary', EditorAssetLibrary, raising=False)
    result = scaffold.create_folders(['/Game/F', '/Game/F/Data/', '/Engine/X', '/Game/../Engine', 'Game/NoSlash'])
    assert result['success'] is False and result['data']['failed_count'] == 3
    assert result['data']['created'] == ['/Game/F/Data'] and result['data']['already_existed'] == ['/Game/F']
    assert made == ['/Game/F/Data']
    assert not hasattr(scaffold, 'delete_folder')


def test_scaffold_templates_are_the_ones_skill_md_lists():
    scaffold = nodetocode.skill_script('project-folder-scaffold')
    with open(os.path.join(SKILLS, 'project-folder-scaffold', 'SKILL.md'), encoding='utf-8') as f:
        text = ' '.join(f.read().split())  # the list wraps across lines
    for name, folders in scaffold.get_available_templates().items():
        assert f"`{name}` ({', '.join(folders)})" in text, name
    assert 'unknown template' in scaffold.create_from_template('/Game/F', 'nope')['error']
