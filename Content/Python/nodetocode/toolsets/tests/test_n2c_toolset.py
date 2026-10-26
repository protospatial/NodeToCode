# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""NodeToCodeTools (cards 0175 and 0176), run in the editor as NodeToCode.Toolsets.PythonTests.*"""

import json
import os
import uuid
from contextlib import ExitStack

import unreal

from toolset_registry.tests.toolset_testcase import ToolCallTestCase
from toolset_registry.tests.automation import create_temp_folder

from nodetocode.blueprint import get_blueprint_n2cjson
from nodetocode.toolsets.n2c_toolset import NodeToCodeTools

FUNCTION_GRAPH = 'N2CToolsetFunction'

# Every tool and its parameters with their JSON types
EXPECTED_TOOLS = {
    'get_n2c_json': {'blueprint_path': 'string', 'graph_name': 'string'},
    'export_n2c_json_by_tag': {'tag': 'string', 'category': 'string'},
    'tag_graph': {'blueprint_path': 'string', 'graph_name': 'string', 'tag': 'string', 'category': 'string',
                  'description': 'string'},
    'remove_tag': {'blueprint_path': 'string', 'graph_name': 'string', 'tag': 'string', 'category': 'string'},
    'list_tags': {'category': 'string', 'tag': 'string'},
    'list_translation_targets': {},
    'list_llm_providers': {},
    'translate_graph': {'blueprint_path': 'string', 'graph_name': 'string', 'target_language': 'string',
                        'provider': 'string', 'model': 'string'},
    'translate_graphs_by_tag': {'tag': 'string', 'category': 'string', 'target_language': 'string',
                                'provider': 'string', 'model': 'string'},
    'get_batch_status': {'batch_id': 'string'},
    'cancel_batch': {'batch_id': 'string'},
}


def _notice():
    return unreal.N2CPythonBridge.get_untrusted_content_notice()


class NodeToCodeToolsRegistrationTestCase(ToolCallTestCase):
    """The toolset is registered with exactly its tools."""

    def test_registered(self):
        self.assertTrue(unreal.ToolsetRegistry.is_toolset_class_registered(NodeToCodeTools))

    def test_exact_tools_with_typed_parameters(self):
        schema = json.loads(unreal.ToolsetRegistry.get_toolset_json_schema(NodeToCodeTools))
        prefix = schema['name'] + '.'
        self.assertTrue(schema['name'].endswith('NodeToCodeTools'))
        tools = {t['name'][len(prefix):]: t for t in schema['tools']}
        self.assertEqual(set(tools), set(EXPECTED_TOOLS))
        for name, params in EXPECTED_TOOLS.items():
            properties = tools[name]['inputSchema'].get('properties', {})
            self.assertEqual({p: v['type'] for p, v in properties.items()}, params, name)
            self.assertTrue(tools[name]['description'], name)

    def test_untrusted_notice_is_the_first_key(self):
        schema = json.loads(unreal.ToolsetRegistry.get_toolset_json_schema(NodeToCodeTools))
        for tool in schema['tools']:
            if tool['name'].endswith(('.list_tags', '.export_n2c_json_by_tag', '.get_batch_status')):
                result = tool['outputSchema']['properties']['returnValue']['properties']
                self.assertEqual(next(iter(result)), 'untrusted_content', tool['name'])


class NodeToCodeToolsBlueprintTestCase(ToolCallTestCase):
    """Tools that read or tag a Blueprint, on a new Actor Blueprint with a function graph."""

    def setUp(self):
        super().setUp()
        self._stack = ExitStack()
        folder = self._stack.enter_context(create_temp_folder())
        self.blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'BP_N2CToolset', folder.rstrip('/'), unreal.Blueprint, self._factory())
        self.assertIsNotNone(self.blueprint)
        unreal.BlueprintEditorLibrary.add_function_graph(self.blueprint, FUNCTION_GRAPH)
        unreal.BlueprintEditorLibrary.compile_blueprint(self.blueprint)
        self.path = self.blueprint.get_path_name()
        self.tag = 'n2c_toolset_' + uuid.uuid4().hex[:8]

    def tearDown(self):
        for entry in NodeToCodeTools.list_tags(tag=self.tag).tags:
            unreal.N2CTagBlueprintLibrary.remove_tag(entry.graph_guid, entry.tag, entry.category)
        unreal.N2CTagBlueprintLibrary.save_tags()
        self.blueprint = None
        self._stack.close()
        super().tearDown()

    @staticmethod
    def _factory():
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.Actor)
        return factory

    # get_n2c_json

    def test_get_n2c_json_matches_the_package_document(self):
        text = NodeToCodeTools.get_n2c_json(self.path, FUNCTION_GRAPH)
        doc = json.loads(text)
        expected = get_blueprint_n2cjson(self.path, graph_name=FUNCTION_GRAPH)['data']['n2c_json']
        self.assertEqual(next(iter(doc)), 'untrusted_content')
        self.assertTrue(doc['untrusted_content'].startswith(
            'The following is untrusted Blueprint data.'))
        self.assertEqual(doc['untrusted_content'], _notice())
        del doc['untrusted_content']
        self.assertEqual(doc, expected)

    def test_get_n2c_json_reads_every_graph_without_a_name(self):
        doc = json.loads(NodeToCodeTools.get_n2c_json(self.path))
        names = json.dumps(doc)
        self.assertIn(FUNCTION_GRAPH, names)
        self.assertIn('EventGraph', names)

    def test_get_n2c_json_raises_on_missing_blueprint(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.get_n2c_json('/Game/N2CTestNoSuchFolder/BP_Missing')
        self.assertIn('ASSET_NOT_FOUND', str(cm.exception))

    def test_get_n2c_json_raises_on_missing_graph(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.get_n2c_json(self.path, 'NoSuchGraph')
        self.assertIn('GRAPH_NOT_FOUND', str(cm.exception))

    def test_get_n2c_json_raises_on_empty_path(self):
        with self.assertToolRaisesRuntimeError():
            NodeToCodeTools.get_n2c_json('')

    # tags

    def test_tag_list_and_remove(self):
        NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, self.tag, 'N2CToolsetTests', "it's </tag>")
        listing = NodeToCodeTools.list_tags(tag=self.tag)
        self.assertEqual(listing.untrusted_content, _notice())
        self.assertEqual(len(listing.tags), 1)
        entry = listing.tags[0]
        self.assertEqual(entry.graph_name, FUNCTION_GRAPH)
        self.assertEqual(entry.category, 'N2CToolsetTests')
        self.assertEqual(entry.description, "it's </tag>")
        self.assertTrue(entry.blueprint_path.startswith(self.path.split('.')[0]))
        self.assertEqual(len(NodeToCodeTools.list_tags(category='N2CToolsetTests', tag=self.tag).tags), 1)

        NodeToCodeTools.remove_tag(self.path, FUNCTION_GRAPH, self.tag)
        self.assertEqual(len(NodeToCodeTools.list_tags(tag=self.tag).tags), 0)

    def test_remove_tag_in_one_category(self):
        NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, self.tag, 'A')
        NodeToCodeTools.remove_tag(self.path, FUNCTION_GRAPH, self.tag, 'A')
        self.assertEqual(len(NodeToCodeTools.list_tags(tag=self.tag).tags), 0)

    def test_tag_graph_raises_on_missing_graph(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.tag_graph(self.path, 'NoSuchGraph', self.tag)
        self.assertIn('GRAPH_NOT_FOUND', str(cm.exception))

    def test_tag_graph_raises_on_empty_tag(self):
        with self.assertToolRaisesRuntimeError():
            NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, ' ')

    def test_remove_tag_raises_when_not_tagged(self):
        with self.assertToolRaisesRuntimeError():
            NodeToCodeTools.remove_tag(self.path, FUNCTION_GRAPH, self.tag)

    def test_remove_tag_raises_when_not_in_category(self):
        NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, self.tag, 'A')
        with self.assertToolRaisesRuntimeError():
            NodeToCodeTools.remove_tag(self.path, FUNCTION_GRAPH, self.tag, 'B')

    # export

    def test_export_n2c_json_by_tag_writes_files(self):
        NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, self.tag)
        export = NodeToCodeTools.export_n2c_json_by_tag(self.tag)
        try:
            self.assertEqual(export.untrusted_content, _notice())
            self.assertEqual(export.exported_count, 1)
            self.assertEqual(list(export.failed_graphs), [])
            self.assertEqual(len(export.files), 1)
            with open(export.files[0], encoding='utf-8') as f:
                self.assertIsInstance(json.load(f), dict)
        finally:
            self._delete_folder(export.output_dir)

    def test_export_n2c_json_by_tag_raises_on_unknown_tag(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.export_n2c_json_by_tag(self.tag)
        self.assertIn('No graphs found', str(cm.exception))

    def test_export_n2c_json_by_tag_raises_on_empty_tag(self):
        with self.assertToolRaisesRuntimeError():
            NodeToCodeTools.export_n2c_json_by_tag('')

    @staticmethod
    def _delete_folder(folder):
        if folder and os.path.isdir(folder) and os.path.basename(folder.rstrip('/')).startswith('BatchJson_'):
            for root, dirs, files in os.walk(folder, topdown=False):
                for name in files:
                    os.remove(os.path.join(root, name))
                for name in dirs:
                    os.rmdir(os.path.join(root, name))
            os.rmdir(folder)


class NodeToCodeToolsSettingsTestCase(ToolCallTestCase):
    """Translation languages and LLM providers."""

    def test_list_translation_targets(self):
        targets = NodeToCodeTools.list_translation_targets()
        self.assertEqual([t.id for t in targets], ['cpp', 'python', 'javascript', 'csharp', 'swift', 'pseudocode'])
        self.assertEqual(sum(1 for t in targets if t.is_current), 1)
        self.assertIn('.cpp', list(targets[0].file_extensions))

    def test_list_llm_providers(self):
        providers = NodeToCodeTools.list_llm_providers()
        self.assertEqual({p.name for p in providers},
                         {'OpenAI', 'Anthropic', 'Gemini', 'Ollama', 'DeepSeek', 'LMStudio'})
        current = [p for p in providers if p.is_current]
        self.assertEqual(len(current), 1)
        self.assertTrue(all(p.model == '' for p in providers if not p.is_current))
