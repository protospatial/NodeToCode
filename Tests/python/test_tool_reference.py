# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The generated MCP tool reference (card 0179), Context/mcp-server/tools.generated.json and tools.generated.md, without
an editor: it lists exactly the tools the source registers (one per REGISTER_MCP_TOOL use, names as
Agents/sync_skills.py reads them), every tool once with its toolset, arguments, annotations, long-running flag and
default enablement, and the Markdown has a section for each. A tool added, removed or renamed without running
Scripts/generate_tool_reference.py fails here; a changed schema or description fails NodeToCode.MCP.ToolReference
(build.ps1 -Test), and `generate_tool_reference.py --check` catches both.
"""

import importlib.util
import json
import os
import re

import pytest

from conftest import REPO_ROOT

REFERENCE_DIR = os.path.join(REPO_ROOT, 'Context', 'mcp-server')
SOURCE_DIR = os.path.join(REPO_ROOT, 'Source')
PYTHON_EXECUTION_TOOLS = {'run-python', 'save-python-script', 'delete-python-script'}


@pytest.fixture(scope='module')
def reference():
    with open(os.path.join(REFERENCE_DIR, 'tools.generated.json'), encoding='utf-8') as f:
        return json.load(f)


@pytest.fixture(scope='module')
def markdown():
    with open(os.path.join(REFERENCE_DIR, 'tools.generated.md'), encoding='utf-8') as f:
        return f.read()


@pytest.fixture(scope='module')
def sync_skills():
    spec = importlib.util.spec_from_file_location('sync_skills', os.path.join(REPO_ROOT, 'Agents', 'sync_skills.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def register_uses():
    """REGISTER_MCP_TOOL(...) uses in the C++ sources (the macro's own definition excluded)"""
    count = 0
    for root, _dirs, files in os.walk(SOURCE_DIR):
        for name in files:
            if name.endswith(('.cpp', '.h')):
                with open(os.path.join(root, name), encoding='utf-8', errors='replace') as f:
                    count += len(re.findall(r'^\s*REGISTER_MCP_TOOL\(\w+\)', f.read(), re.MULTILINE))
    return count


def test_tool_count_equals_register_mcp_tool_uses(reference):
    assert reference['toolCount'] == len(reference['tools']) == register_uses()


def test_tool_names_are_the_registered_tools(reference, sync_skills):
    registered, unparsed = sync_skills.registered_tools()
    assert not unparsed
    names = [tool['name'] for tool in reference['tools']]
    assert names == sorted(names), 'tools are sorted by name'
    assert set(names) == registered, 'regenerate with Scripts/generate_tool_reference.py'


def test_every_tool_has_the_reference_fields(reference):
    toolsets = {toolset['name']: toolset for toolset in reference['toolsets']}
    for tool in reference['tools']:
        assert tool['toolset'] in toolsets, tool['name']
        assert tool['name'] in toolsets[tool['toolset']]['tools']
        assert tool['description'], tool['name']
        assert isinstance(tool['longRunning'], bool)
        assert isinstance(tool['worksDuringPlaySession'], bool)
        assert isinstance(tool['annotations'], dict)
        assert tool['defaultEnabled'] == (toolsets[tool['toolset']]['defaultEnabled']
                                          and tool['name'] not in PYTHON_EXECUTION_TOOLS), tool['name']
        properties = tool['inputSchema'].get('properties', {})
        required = set(tool['inputSchema'].get('required', []))
        assert [a['name'] for a in tool['arguments']] == list(properties), tool['name']
        for argument in tool['arguments']:
            assert argument['type'], (tool['name'], argument['name'])
            assert argument['required'] == (argument['name'] in required), (tool['name'], argument['name'])
            if 'default' in properties[argument['name']]:
                assert argument['default'] == properties[argument['name']]['default']
    assert reference['defaultEnabledCount'] == sum(tool['defaultEnabled'] for tool in reference['tools'])
    assert reference['unrealMcpOverlapCount'] == sum('unrealMcpCounterpart' in tool for tool in reference['tools'])
    assert sorted(t['name'] for t in reference['toolSearchTools']) == ['call_tool', 'describe_toolset', 'list_toolsets']


def test_markdown_has_a_section_per_tool(reference, markdown):
    headings = re.findall(r'^### (\S+)$', markdown, re.MULTILINE)
    expected = [tool['name'] for tool in reference['tools']] + [t['name'] for t in reference['toolSearchTools']]
    assert sorted(headings) == sorted(expected)
    assert '%d MCP tools' % reference['toolCount'] in markdown


def test_files_are_deterministic_text(reference):
    for name in ('tools.generated.json', 'tools.generated.md'):
        with open(os.path.join(REFERENCE_DIR, name), 'rb') as f:
            data = f.read()
        assert b'\r' not in data, '%s has LF line endings' % name
        assert data.endswith(b'\n') and not data.endswith(b'\n\n')
        assert not re.search(rb'20\d\d-\d\d-\d\d|[A-Za-z]:[\\/]', data), '%s has no dates or machine paths' % name
