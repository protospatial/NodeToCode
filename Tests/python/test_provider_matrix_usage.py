# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Card 0506: Tests/mcp_probe/live_provider_matrix.py reads the token usage line the response parser logs.

#181 (card 0185) added the cache and reasoning counts to the line and the matrix's regex stopped matching, so every
case reported usage=[] without failing. The line is read here from UN2CResponseParserBase::LogUsage's own format
string, so a later change to it fails this test instead of emptying the live report again.
"""

import os
import re
import sys

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PROBE = os.path.join(REPO_ROOT, 'Tests', 'mcp_probe')
if PROBE not in sys.path:
    sys.path.insert(0, PROBE)

import live_provider_matrix as matrix  # noqa: E402

PARSER_CPP = os.path.join(REPO_ROOT, 'Source', 'Private', 'LLM', 'N2CResponseParserBase.cpp')
PREFIX = '[2026.10.27-09.12.44:101][512]LogNodeToCode: '


def log_usage_format():
    """The Printf format of UN2CResponseParserBase::LogUsage."""
    with open(PARSER_CPP, encoding='utf-8-sig') as f:
        source = f.read()
    found = re.findall(r'TEXT\("(LLM Token Usage[^"]*)"\)', source)
    assert len(found) == 1, found
    return found[0]


def logged(input_tokens, cache_read, cache_write, output_tokens, reasoning):
    """The line LogUsage writes, in its argument order (Input, cache read, cache write, Output, reasoning)."""
    return log_usage_format() % (input_tokens, cache_read, cache_write, output_tokens, reasoning)


def test_current_line_parses_every_count():
    text = PREFIX + logged(1834, 1024, 16, 912, 300) + '\n'
    assert matrix.parse_usage(text) == [{'input': 1834, 'output': 912, 'cache_read': 1024, 'cache_write': 16,
                                         'reasoning': 300}]


def test_every_line_in_a_slice_is_read():
    text = ''.join(PREFIX + logged(i, 0, 0, 10 * i, 0) + '\n' for i in (1, 2, 3))
    assert [u['input'] for u in matrix.parse_usage(text)] == [1, 2, 3]
    assert [u['output'] for u in matrix.parse_usage(text)] == [10, 20, 30]


def test_line_before_181_still_parses():
    text = PREFIX + 'LLM Token Usage - Input: 120 Output: 45\n'
    assert matrix.parse_usage(text) == [{'input': 120, 'output': 45}]


def test_translated_case_without_usage_is_not_verified():
    entry = matrix.usage_not_verified({'id': 'anthropic-default', 'translated': True, 'usage': []})
    assert entry and entry['case'] == 'anthropic-default'


def test_case_with_usage_or_without_translation_is_not_reported():
    assert matrix.usage_not_verified({'id': 'a', 'translated': True, 'usage': [{'input': 1, 'output': 1}]}) is None
    assert matrix.usage_not_verified({'id': 'b', 'translated': False, 'usage': []}) is None
