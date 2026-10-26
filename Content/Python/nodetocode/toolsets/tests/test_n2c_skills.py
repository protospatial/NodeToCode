# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""NodeToCode's Agent Skills in the toolset registry (card 0177), run in the editor as
NodeToCode.Toolsets.PythonTests.*: Epic's AgentSkillToolset lists the three skills with their descriptions and serves
their instructions, the text Agents/sync_skills.py generated from the SKILL.md files."""

import json
import os
import unittest

import unreal

from toolset_registry._registry_interface import execute_tool

from nodetocode import skills

SKILL_FILES = os.path.dirname(os.path.abspath(skills.__file__))
EXPECTED = {'blueprint-comprehension', 'blueprint-to-cpp', 'blueprint-migration'}


def _skill_toolset():
    """(registry name, {lower-case tool name without '_': (tool name, its parameter names)}) of AgentSkillToolset"""
    schema = json.loads(unreal.ToolsetRegistry.get_toolset_json_schema(unreal.AgentSkillToolset))
    prefix = schema['name'] + '.'
    tools = {}
    for tool in schema['tools']:
        name = tool['name'][len(prefix):] if tool['name'].startswith(prefix) else tool['name']
        tools[name.replace('_', '').lower()] = (name, list(tool['inputSchema'].get('properties', {})))
    return schema['name'], tools


def _read(name):
    with open(os.path.join(SKILL_FILES, name + '.md'), encoding='utf-8') as f:
        return f.read()


class NodeToCodeSkillsTestCase(unittest.IsolatedAsyncioTestCase):
    """The three skills are AgentSkill classes the registry lists and serves."""

    def test_three_skills_with_description_and_instructions(self):
        self.assertEqual(set(skills.SKILLS), EXPECTED)
        with open(os.path.join(SKILL_FILES, 'skills.json'), encoding='utf-8') as f:
            descriptions = json.load(f)
        for name, cls in skills.SKILLS.items():
            self.assertTrue(issubclass(cls, unreal.AgentSkill), name)
            cdo = unreal.get_default_object(cls)
            self.assertEqual(cdo.get_editor_property('description'), descriptions[name]['description'], name)
            self.assertEqual(cdo.get_editor_property('instructions'), _read(name), name)
            self.assertTrue(cdo.get_editor_property('description').strip(), name)
            self.assertTrue(cdo.get_editor_property('instructions').strip(), name)

    async def test_list_skills_finds_the_three(self):
        toolset, tools = _skill_toolset()
        list_name, _params = tools['listskills']
        listed = (await execute_tool(toolset, list_name, {}))['returnValue']
        for name, cls in skills.SKILLS.items():
            path = cls.static_class().get_path_name()
            self.assertIn(path, listed, name)
            self.assertEqual(listed[path], unreal.get_default_object(cls).get_editor_property('description'), name)
            self.assertTrue(listed[path].strip(), name)

    async def test_get_skills_serves_the_instructions(self):
        toolset, tools = _skill_toolset()
        get_name, params = tools['getskills']
        self.assertEqual(len(params), 1)
        paths = {name: cls.static_class().get_path_name() for name, cls in skills.SKILLS.items()}
        served = (await execute_tool(toolset, get_name, {params[0]: list(paths.values())}))['returnValue']
        instructions = {}
        for name, path in paths.items():
            instructions[name] = next(v for k, v in served[path].items() if k.lower() == 'instructions')
            self.assertEqual(instructions[name], _read(name), name)
            self.assertNotIn('](references/', instructions[name], name)  # self-contained: references appended
        # On UE 5.8 the instructions name NodeToCodeTools and Epic's tools
        self.assertIn('NodeToCodeTools.get_n2c_json', instructions['blueprint-comprehension'])
        self.assertIn('NodeToCodeTools.translate_graphs_by_tag', instructions['blueprint-to-cpp'])
        self.assertIn('BlueprintTools.compile_blueprint', instructions['blueprint-migration'])
