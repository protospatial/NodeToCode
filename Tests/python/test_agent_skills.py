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
            'compile-blueprint', 'assess-needed-tools'} <= tools
    assert len(tools) == 67  # 66 listed in the default mode plus assess-needed-tools (Source/Private/MCP/Tools/AGENTS.md)


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
