# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""NodeToCode's Agent Skills in UE 5.8's toolset registry (card 0177).

Each class is a UAgentSkill that Epic's AgentSkillToolset lists (list_skills) and serves (get_skills) to Unreal MCP
clients and the in-editor assistant. The skills are written once, in Agents/claude-code/nodetocode/skills/<name>/
SKILL.md; Agents/sync_skills.py copies each body (frontmatter stripped, references/ appended) to <name>.md here and the
descriptions to skills.json, and its --check fails when a copy is out of date or a skill has no class below. Do not
edit the copies.

Imported only by Content/Python/init_unreal.py once it has found the registry: importing it anywhere else fails on
5.6 and 5.7.
"""

import json
import os

import unreal

from toolset_registry.agent_skill import agent_skill

_HERE = os.path.dirname(os.path.abspath(__file__))

with open(os.path.join(_HERE, 'skills.json'), encoding='utf-8') as _f:
    _DESCRIPTIONS = json.load(_f)


def _description(name):
    return _DESCRIPTIONS[name]['description']


def _instructions(name):
    with open(os.path.join(_HERE, name + '.md'), encoding='utf-8') as f:
        return f.read()


@agent_skill
class NodeToCodeBlueprintComprehensionSkill(unreal.AgentSkill):
    __doc__ = _description('blueprint-comprehension')
    instructions = _instructions('blueprint-comprehension')


@agent_skill
class NodeToCodeBlueprintToCppSkill(unreal.AgentSkill):
    __doc__ = _description('blueprint-to-cpp')
    instructions = _instructions('blueprint-to-cpp')


@agent_skill
class NodeToCodeBlueprintMigrationSkill(unreal.AgentSkill):
    __doc__ = _description('blueprint-migration')
    instructions = _instructions('blueprint-migration')


# Skill name (the SKILL.md folder) -> registered class
SKILLS = {
    'blueprint-comprehension': NodeToCodeBlueprintComprehensionSkill,
    'blueprint-to-cpp': NodeToCodeBlueprintToCppSkill,
    'blueprint-migration': NodeToCodeBlueprintMigrationSkill,
}
