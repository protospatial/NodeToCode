# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""NodeToCodeTools translation (card 0176): translate_graph, translate_graphs_by_tag, get_batch_status and cancel_batch
against a local stub provider (stub_provider.StubProvider; no network). Run in the editor as
NodeToCode.Toolsets.PythonTests.*; the async tests await on the editor's event loop, so the editor ticks meanwhile."""

import asyncio
import json
import os
import time
import unittest
import uuid
from contextlib import ExitStack

import unreal

from toolset_registry.tests.toolset_testcase import ToolCallTestCase
from toolset_registry.tests.automation import create_temp_folder

from nodetocode.toolsets.n2c_toolset import NodeToCodeTools
from nodetocode.toolsets.tests.stub_provider import StubProvider, inside

FUNCTION_GRAPH = 'N2CToolsetFunction'
SECOND_GRAPH = 'N2CToolsetFunction2'
TIMEOUT = 20.0


class NodeToCodeToolsTranslateTestCase(ToolCallTestCase, unittest.IsolatedAsyncioTestCase):
    """Translation tools on a new Actor Blueprint with two function graphs."""

    def setUp(self):
        super().setUp()
        self._stack = ExitStack()
        folder = self._stack.enter_context(create_temp_folder())
        factory = unreal.BlueprintFactory()
        factory.set_editor_property('parent_class', unreal.Actor)
        self.blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            'BP_N2CToolset', folder.rstrip('/'), unreal.Blueprint, factory)
        self.assertIsNotNone(self.blueprint)
        unreal.BlueprintEditorLibrary.add_function_graph(self.blueprint, FUNCTION_GRAPH)
        unreal.BlueprintEditorLibrary.add_function_graph(self.blueprint, SECOND_GRAPH)
        unreal.BlueprintEditorLibrary.compile_blueprint(self.blueprint)
        self.path = self.blueprint.get_path_name()
        self.tag = 'n2c_translate_' + uuid.uuid4().hex[:8]

    def tearDown(self):
        # A batch a failed test left running must not hold the orchestrator for the next test
        if unreal.N2CPythonTranslation.get_batch_status('').state == 'running':
            unreal.N2CPythonTranslation.cancel_batch('')
        for entry in NodeToCodeTools.list_tags(tag=self.tag).tags:
            unreal.N2CTagBlueprintLibrary.remove_tag(entry.graph_guid, entry.tag, entry.category)
        unreal.N2CTagBlueprintLibrary.save_tags()
        self.blueprint = None
        self._stack.close()
        super().tearDown()

    async def _until(self, predicate, timeout=TIMEOUT, what='condition'):
        """Awaits predicate() while the editor ticks; fails after timeout seconds."""
        end = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > end:
                self.fail(f'timed out waiting for {what}')
            await asyncio.sleep(0.02)

    # translate_graph

    async def test_translate_graph_succeeds(self):
        with StubProvider(delay=1.0) as stub:
            result = NodeToCodeTools.translate_graph(self.path, FUNCTION_GRAPH, 'python')
            self.assertIsInstance(result, unreal.ToolCallAsyncResultString)
            self.assertFalse(result.is_complete)
            unreal.SystemLibrary.collect_garbage()  # the completion's listener must survive a collection
            await asyncio.sleep(0.05)
            await self._until(lambda: result.is_complete, what='the translation')
            self.assertEqual(result.error, '')
            value = json.loads(result.value)
            self.assertEqual(next(iter(value)), 'untrusted_content')
            self.assertEqual(value['untrusted_content'], unreal.N2CPythonBridge.get_untrusted_content_notice())
            self.assertEqual(value['provider'], 'ollama')
            self.assertEqual(value['language'], 'python')
            self.assertEqual(value['graph_name'], FUNCTION_GRAPH)
            self.assertEqual(value['graphs'][0]['code']['graphImplementation'], '# translated by the stub')
            self.assertTrue(value['files'])
            self.assertTrue(all(os.path.isfile(f) and inside(f, stub.output_dir) for f in value['files']))
            self.assertTrue(any(f.endswith('.py') for f in value['files']))
            self.assertEqual(stub.hits, 1)

            status = NodeToCodeTools.get_batch_status(value['batch_id'])
            self.assertEqual(status.state, 'succeeded')
            self.assertEqual(status.progress, 1.0)

    async def test_translate_graph_keeps_the_game_thread_ticking(self):
        with StubProvider(delay=1.0):
            first_frame = unreal.SystemLibrary.get_frame_count()
            result = NodeToCodeTools.translate_graph(self.path, FUNCTION_GRAPH)
            self.assertFalse(result.is_complete, 'the call returned before the provider answered')
            pending_frames = 0
            end = time.monotonic() + TIMEOUT
            while not result.is_complete and time.monotonic() < end:
                await asyncio.sleep(0.01)
                if not result.is_complete:
                    pending_frames = unreal.SystemLibrary.get_frame_count() - first_frame
            self.assertTrue(result.is_complete)
            self.assertEqual(result.error, '')
            self.assertGreater(pending_frames, 5, 'the editor ticked while the translation was pending')

    async def test_translate_graph_reports_a_provider_failure(self):
        # NodeToCode logs the failed request as errors; they are this test's expected outcome
        unreal.AutomationLibrary.add_expected_log_error('stub provider failure', 0, False, False)
        unreal.AutomationLibrary.add_expected_log_error('HTTP 500 error from Ollama', 0, False, False)
        with StubProvider(mode='error') as stub:
            result = NodeToCodeTools.translate_graph(self.path, FUNCTION_GRAPH)
            await self._until(lambda: result.is_complete, what='the failure')
            self.assertTrue(result.error)
            self.assertIn('stub provider failure', result.error)
            self.assertEqual(stub.hits, 1)
            status = NodeToCodeTools.get_batch_status('')
            self.assertEqual(status.state, 'failed')
            self.assertEqual(status.items[0].status, 'failed')
            self.assertIn('stub provider failure', status.items[0].error)

    async def test_cancel_batch_stops_translate_graph(self):
        with StubProvider(mode='stall') as stub:
            result = NodeToCodeTools.translate_graph(self.path, FUNCTION_GRAPH)
            await self._until(lambda: stub.hits == 1, what='the request')
            self.assertEqual(NodeToCodeTools.get_batch_status('').state, 'running')
            cancelled_at = time.monotonic()
            NodeToCodeTools.cancel_batch('')
            await self._until(lambda: result.is_complete, timeout=1.0, what='the cancel')
            self.assertLess(time.monotonic() - cancelled_at, 1.0)
            self.assertIn('Cancelled', result.error)
            status = NodeToCodeTools.get_batch_status('')
            self.assertEqual(status.state, 'cancelled')
            self.assertEqual(status.items[0].status if status.items else 'skipped', 'skipped')
            await asyncio.sleep(0.2)
            self.assertEqual(stub.hits, 1)

    async def test_translate_graph_fails_on_a_missing_graph(self):
        with StubProvider() as stub:
            result = NodeToCodeTools.translate_graph(self.path, 'NoSuchGraph')
            self.assertTrue(result.is_complete)
            self.assertIn('NoSuchGraph', result.error)
            self.assertEqual(stub.hits, 0)

    async def test_translate_graph_fails_on_an_empty_path(self):
        result = NodeToCodeTools.translate_graph('', FUNCTION_GRAPH)
        self.assertTrue(result.is_complete)
        self.assertIn('blueprint_path', result.error)

    async def test_translate_graph_fails_on_an_unknown_provider(self):
        result = NodeToCodeTools.translate_graph(self.path, FUNCTION_GRAPH, '', 'acme')
        self.assertTrue(result.is_complete)
        self.assertIn('acme', result.error)

    # translate_graphs_by_tag and get_batch_status

    async def test_translate_graphs_by_tag_reports_progress_to_completion(self):
        NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, self.tag, 'N2CTranslateTests')
        NodeToCodeTools.tag_graph(self.path, SECOND_GRAPH, self.tag, 'N2CTranslateTests')
        with StubProvider(delay=1.0) as stub:
            batch_id = NodeToCodeTools.translate_graphs_by_tag(self.tag)
            self.assertTrue(batch_id)
            seen = []

            def finished():
                status = NodeToCodeTools.get_batch_status(batch_id)
                seen.append((status.state, status.progress, status.message))
                return status.state != 'running'

            await self._until(finished, what='the batch')
            running = [s for s in seen if s[0] == 'running']
            self.assertTrue(running, 'the batch was seen running')
            progress = [p for _, p, _ in running]
            self.assertEqual(progress, sorted(progress), 'progress never goes down')
            self.assertTrue(any(0.0 < p < 1.0 for p in progress))
            self.assertTrue(any('2 of 2' in m for _, _, m in running), 'the second graph was reported')

            status = NodeToCodeTools.get_batch_status(batch_id)
            self.assertEqual(status.batch_id, batch_id)
            self.assertEqual(status.state, 'succeeded')
            self.assertEqual(status.progress, 1.0)
            self.assertEqual(status.untrusted_content, unreal.N2CPythonBridge.get_untrusted_content_notice())
            self.assertEqual(status.provider, 'ollama')
            self.assertEqual(sorted(i.graph_name for i in status.items), [FUNCTION_GRAPH, SECOND_GRAPH])
            self.assertTrue(all(i.status == 'completed' and i.files for i in status.items))
            self.assertTrue(inside(status.output_path, stub.output_dir))
            self.assertEqual(stub.hits, 2)

    async def test_a_second_batch_does_not_start_while_one_runs(self):
        NodeToCodeTools.tag_graph(self.path, FUNCTION_GRAPH, self.tag)
        with StubProvider(mode='stall') as stub:
            batch_id = NodeToCodeTools.translate_graphs_by_tag(self.tag)
            await self._until(lambda: stub.hits == 1, what='the request')
            with self.assertToolRaisesRuntimeError() as cm:
                NodeToCodeTools.translate_graphs_by_tag(self.tag)
            self.assertIn('BATCH_IN_PROGRESS', str(cm.exception))
            second = NodeToCodeTools.translate_graph(self.path, SECOND_GRAPH)
            self.assertTrue(second.is_complete)
            self.assertIn('BATCH_IN_PROGRESS', second.error)
            NodeToCodeTools.cancel_batch(batch_id)
            self.assertEqual(NodeToCodeTools.get_batch_status(batch_id).state, 'cancelled')

    def test_translate_graphs_by_tag_raises_on_an_unknown_tag(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.translate_graphs_by_tag(self.tag)
        self.assertIn(self.tag, str(cm.exception))

    def test_translate_graphs_by_tag_raises_without_tag_or_category(self):
        with self.assertToolRaisesRuntimeError():
            NodeToCodeTools.translate_graphs_by_tag('', '')

    def test_get_batch_status_raises_on_an_unknown_id(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.get_batch_status('no-such-batch')
        self.assertIn('BATCH_NOT_FOUND', str(cm.exception))

    def test_cancel_batch_raises_on_an_unknown_id(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.cancel_batch('no-such-batch')
        self.assertIn('BATCH_NOT_FOUND', str(cm.exception))

    def test_cancel_batch_raises_when_none_runs(self):
        with self.assertToolRaisesRuntimeError() as cm:
            NodeToCodeTools.cancel_batch('')
        self.assertIn('BATCH_NOT_FOUND', str(cm.exception))
