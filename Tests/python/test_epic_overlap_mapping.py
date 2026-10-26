# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The wiki's NodeToCode-and-Epic tool mapping against the source of truth, N2CMcpEpicOverlap::Tools in
Source/Public/MCP/Tools/N2CMcpEpicOverlap.h (card 0174): the page lists every tool the MCP server leaves to Unreal MCP
on UE 5.8, with the same Epic counterpart, and nothing else, and the wiki sidebar links it.
"""

import os
import re

from conftest import REPO_ROOT

HEADER = os.path.join(REPO_ROOT, 'Source', 'Public', 'MCP', 'Tools', 'N2CMcpEpicOverlap.h')
PAGE = os.path.join(REPO_ROOT, 'wiki', 'developers', 'NodeToCode-and-Epic-Tool-Mapping.md')
SIDEBAR = os.path.join(REPO_ROOT, 'wiki', '_Sidebar.md')

ENTRY = re.compile(r'\{\s*TEXT\("([a-z0-9-]+)"\)\s*,\s*TEXT\("([^"]+)"\)\s*\}')
ROW = re.compile(r'^\|\s*`([a-z0-9-]+)`\s*\|\s*(.+?)\s*\|\s*$')


def read(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()


def header_table():
    """{tool: counterpart} from the Tools[] initializer"""
    text = read(HEADER)
    body = text[text.index('inline constexpr FOverlap Tools[]'):]
    body = body[:body.index('};')]
    return {tool: counterpart for tool, counterpart in ENTRY.findall(body)}


def page_table():
    """{tool: counterpart} from the mapping table; `BlueprintTools.a`, `b` becomes 'BlueprintTools.a, b'"""
    rows = {}
    for line in read(PAGE).splitlines():
        m = ROW.match(line)
        if m:
            rows[m.group(1)] = ', '.join(part.strip().strip('`') for part in m.group(2).split(','))
    return rows


def test_header_lists_34_tools():
    assert len(header_table()) == 34


def test_page_lists_every_overlapping_tool_with_its_counterpart():
    header, page = header_table(), page_table()
    assert sorted(set(header) - set(page)) == [], 'tools left to Unreal MCP but missing from the wiki mapping'
    assert sorted(set(page) - set(header)) == [], 'tools in the wiki mapping that the server does not leave out'
    differ = {tool: (header[tool], page[tool]) for tool in header if header[tool] != page[tool]}
    assert differ == {}, 'Epic counterparts differ between N2CMcpEpicOverlap.h and the wiki page'


def test_sidebar_links_the_page():
    assert '(NodeToCode-and-Epic-Tool-Mapping)' in read(SIDEBAR)
