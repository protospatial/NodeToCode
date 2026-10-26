# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
NodeToCode's Agent Skills (card 0171): Agents/sync_skills.py --check passes on the repository (generated spec copies
current, .agents/skills mirror identical, frontmatter valid, every MCP tool a skill names registered), and the
checker catches what it is there to catch: a tool the plugin does not register, a name that differs from its folder,
a frontmatter field outside the Agent Skills spec, an over-long description or SKILL.md, and a broken link.
"""

import importlib.util
import os
import shutil

import pytest

from conftest import REPO_ROOT

SKILL_NAMES = {'blueprint-comprehension', 'blueprint-to-cpp', 'blueprint-migration'}


@pytest.fixture(scope='module')
def sync_skills():
    spec = importlib.util.spec_from_file_location('sync_skills', os.path.join(REPO_ROOT, 'Agents', 'sync_skills.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def skills_copy(sync_skills, tmp_path):
    """A writable copy of the real skills folder"""
    target = tmp_path / 'skills'
    shutil.copytree(sync_skills.SKILLS_DIR, target)
    return target


def _check(sync_skills, skills_dir):
    findings, _tools, _names = sync_skills.check_all(skills_dir=str(skills_dir))
    return findings


def _append(path, text):
    with open(path, 'a', encoding='utf-8', newline='\n') as f:
        f.write(text)


def test_repository_skills_pass_check(sync_skills):
    out_of_date = sync_skills.sync(check=True)
    findings, tools, names = sync_skills.check_all()
    assert out_of_date == []
    assert findings == []
    assert SKILL_NAMES <= set(names)


def test_registry_holds_the_tools_the_skills_rely_on(sync_skills):
    tools, unparsed = sync_skills.registered_tools()
    assert unparsed == []
    assert {'get-blueprint-n2cjson', 'list-blueprint-graphs', 'translate-focused-blueprint', 'open-blueprint-asset',
            'compile-blueprint', 'find-nodes-in-graph'} <= tools
    assert 'assess-needed-tools' not in tools  # removed by card 0165 (tool search: list_toolsets, describe_toolset, call_tool)
    assert len(tools) == 66  # every tool, all listed with the default Tool Profile (Source/Private/MCP/Tools/AGENTS.md)


@pytest.mark.parametrize('mention', ['`translate-blueprint-by-path`', 'mcp__nodetocode__batch-translate-blueprints',
                                     '`mcp__nodetocode__get-blueprint-json`', '`NodeToCode.export-n2c-json`'])
def test_unregistered_tool_fails(sync_skills, skills_copy, mention):
    _append(skills_copy / 'blueprint-to-cpp' / 'SKILL.md', '\nThen call %s.\n' % mention)
    findings = _check(sync_skills, skills_copy)
    assert len(findings) == 1 and 'does not register' in findings[0], findings


def test_unregistered_tool_in_a_reference_fails(sync_skills, skills_copy):
    _append(skills_copy / 'blueprint-to-cpp' / 'references' / 'cpp-translation-rules.md', '\nUse `save-cpp-files`.\n')
    assert any('save-cpp-files' in f for f in _check(sync_skills, skills_copy))


def _replace_in_skill(skills_copy, skill, old, new):
    path = skills_copy / skill / 'SKILL.md'
    text = path.read_text(encoding='utf-8')
    assert old in text
    path.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')


def test_name_must_match_folder(sync_skills, skills_copy):
    _replace_in_skill(skills_copy, 'blueprint-migration', 'name: blueprint-migration', 'name: blueprint-migrate')
    assert any('does not match its folder' in f for f in _check(sync_skills, skills_copy))


def test_field_outside_spec_fails(sync_skills, skills_copy):
    _replace_in_skill(skills_copy, 'blueprint-migration', 'license: Apache-2.0', 'license: Apache-2.0\nwhen_to_use: x')
    assert any('not in the Agent Skills spec' in f for f in _check(sync_skills, skills_copy))


def test_long_description_fails(sync_skills, skills_copy):
    _replace_in_skill(skills_copy, 'blueprint-migration', 'description: "Moves', 'description: "%s Moves' % ('x' * 1024))
    assert any('description is' in f for f in _check(sync_skills, skills_copy))


def test_long_skill_md_fails(sync_skills, skills_copy):
    _append(skills_copy / 'blueprint-comprehension' / 'SKILL.md', 'line\n' * 500)
    assert any('lines (keep it at most 500' in f for f in _check(sync_skills, skills_copy))


def test_broken_link_fails(sync_skills, skills_copy):
    _append(skills_copy / 'blueprint-comprehension' / 'SKILL.md', '\nSee [the spec](references/missing.md).\n')
    assert any('does not resolve' in f for f in _check(sync_skills, skills_copy))


# --- UE 5.8: the skills on the routes Epic's Unreal MCP serves, and their copies in the toolset registry (card 0177)

REGISTRY_CLASSES = {'blueprint-comprehension': 'NodeToCodeBlueprintComprehensionSkill',
                    'blueprint-to-cpp': 'NodeToCodeBlueprintToCppSkill',
                    'blueprint-migration': 'NodeToCodeBlueprintMigrationSkill'}


@pytest.fixture
def registry_copy(sync_skills, tmp_path):
    """A writable copy of Content/Python/nodetocode/skills"""
    target = tmp_path / 'registry'
    shutil.copytree(sync_skills.REGISTRY_SKILLS_DIR, target, ignore=shutil.ignore_patterns('__pycache__'))
    return target


@pytest.fixture
def registry_skills(monkeypatch):
    """nodetocode.skills imported with a stand-in agent_skill decorator: {class name: class}"""
    import sys
    import types
    from conftest import UNREAL
    registered = {}
    module = types.ModuleType('toolset_registry.agent_skill')

    def agent_skill(cls):
        registered[cls.__name__] = cls
        return cls
    module.agent_skill = agent_skill
    package = types.ModuleType('toolset_registry')
    package.__path__ = []
    package.agent_skill = module
    monkeypatch.setitem(sys.modules, 'toolset_registry', package)
    monkeypatch.setitem(sys.modules, 'toolset_registry.agent_skill', module)
    monkeypatch.setattr(UNREAL, 'AgentSkill', type('AgentSkill', (), {}), raising=False)
    import nodetocode
    monkeypatch.delitem(sys.modules, 'nodetocode.skills', raising=False)
    monkeypatch.delattr(nodetocode, 'skills', raising=False)
    import nodetocode.skills  # noqa: F401
    yield registered
    # Leave no imported copy behind for the init_unreal tests
    sys.modules.pop('nodetocode.skills', None)
    if hasattr(nodetocode, 'skills'):
        delattr(nodetocode, 'skills')


def test_registry_classes_serve_each_skill_from_its_source(sync_skills, registry_skills):
    assert set(registry_skills) == set(REGISTRY_CLASSES.values())
    for name, class_name in REGISTRY_CLASSES.items():
        cls = registry_skills[class_name]
        skill_dir = os.path.join(sync_skills.SKILLS_DIR, name)
        fields, body, _errors = sync_skills.parse_frontmatter(
            sync_skills.read_text(os.path.join(skill_dir, 'SKILL.md')).replace('\r\n', '\n'))
        assert cls.__doc__ == fields['description'] and cls.__doc__.strip()
        assert cls.instructions == sync_skills.registry_instructions(skill_dir)
        assert cls.instructions.startswith('# ') and '\n---\nname:' not in cls.instructions
        # Self-contained: no link to a file the registry cannot serve, the references appended
        assert '](references/' not in cls.instructions
        references = os.path.join(skill_dir, 'references')
        for ref in os.listdir(references) if os.path.isdir(references) else []:
            assert '# Appended: references/%s' % ref in cls.instructions
            assert '<!-- Generated by' not in cls.instructions


def test_skills_name_nodetocodetools_and_epic_tools_on_5_8(sync_skills):
    def text(name):
        return sync_skills.read_text(os.path.join(sync_skills.SKILLS_DIR, name, 'SKILL.md'))
    toolset = sync_skills.toolset_tools()
    assert {'get_n2c_json', 'translate_graph', 'translate_graphs_by_tag', 'get_batch_status', 'tag_graph'} <= toolset
    assert 'NodeToCodeTools.get_n2c_json' in text('blueprint-comprehension')
    for tool in ('translate_graph', 'translate_graphs_by_tag', 'get_batch_status', 'cancel_batch', 'tag_graph',
                 'list_tags', 'export_n2c_json_by_tag'):
        assert 'NodeToCodeTools.%s' % tool in text('blueprint-to-cpp'), tool
    assert 'BlueprintTools.compile_blueprint' in text('blueprint-migration')
    assert 'AssetTools.save_assets' in text('blueprint-migration')


def test_registry_copy_out_of_date_fails(sync_skills, registry_copy):
    assert sync_skills.sync_registry(True, registry_dir=str(registry_copy)) == []
    _append(registry_copy / 'blueprint-to-cpp.md', '\nEdited by hand.\n')
    findings = sync_skills.sync_registry(True, registry_dir=str(registry_copy))
    assert len(findings) == 1 and 'blueprint-to-cpp.md is out of date' in findings[0], findings


def test_registry_description_out_of_date_fails(sync_skills, registry_copy):
    path = registry_copy / 'skills.json'
    path.write_text(path.read_text(encoding='utf-8').replace('Reads an Unreal', 'Reads a'), encoding='utf-8')
    findings = sync_skills.sync_registry(True, registry_dir=str(registry_copy))
    assert any('skills.json is out of date' in f for f in findings), findings


def test_registry_extra_file_fails(sync_skills, registry_copy):
    _append(registry_copy / 'old-skill.md', 'gone\n')
    findings = sync_skills.sync_registry(True, registry_dir=str(registry_copy))
    assert any('old-skill.md is not generated' in f for f in findings), findings


def test_skill_without_registry_class_fails(sync_skills, registry_copy):
    path = registry_copy / '__init__.py'
    text = path.read_text(encoding='utf-8')
    assert "instructions = _instructions('blueprint-migration')" in text
    path.write_text(text.replace("instructions = _instructions('blueprint-migration')", "instructions = ''"),
                    encoding='utf-8')
    findings = sync_skills.check_registry_classes(registry_dir=str(registry_copy))
    assert findings and "no @agent_skill class reads skill 'blueprint-migration'" in findings[0], findings


def test_unknown_nodetocodetools_tool_fails(sync_skills, skills_copy):
    _append(skills_copy / 'blueprint-to-cpp' / 'SKILL.md', '\nThen call `NodeToCodeTools.translate_blueprint`.\n')
    findings = _check(sync_skills, skills_copy)
    assert len(findings) == 1 and 'not a tool of the NodeToCodeTools toolset' in findings[0], findings


def test_epic_tool_outside_the_mapping_fails(sync_skills, skills_copy):
    _append(skills_copy / 'blueprint-migration' / 'SKILL.md', '\nOr call `BlueprintTools.reparent_everything`.\n')
    findings = _check(sync_skills, skills_copy)
    assert len(findings) == 1 and 'mapping does not list' in findings[0], findings


def test_left_out_tool_without_epic_counterpart_fails(sync_skills, skills_copy):
    # compile-blueprint is left to Unreal MCP on UE 5.8: naming it without BlueprintTools.compile_blueprint fails
    _replace_in_skill(skills_copy, 'blueprint-migration', '; Epic: `BlueprintTools.compile_blueprint`', '')
    findings = _check(sync_skills, skills_copy)
    assert len(findings) == 1 and "'compile-blueprint', which UE 5.8 leaves to Unreal MCP" in findings[0], findings
