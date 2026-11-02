# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The per-language CodeGen prompts in Content/Prompting (card 0249): each names Unreal Engine 5.6 to 5.8, has no UE4-era
wording or old typos, and carries one worked example whose input is N2C JSON and whose output is a valid response in
the shape Content/Prompting/Shared/N2CResponseFormat.md asks for. An invalid example teaches the model invalid JSON.
"""

import glob
import json
import os
import re

import pytest

from conftest import REPO_ROOT

PROMPTS = sorted(glob.glob(os.path.join(REPO_ROOT, 'Content', 'Prompting', 'CodeGen_*.md')))
GRAPH_TYPES = {'Function', 'EventGraph', 'Macro', 'Composite', 'Construction', 'Struct', 'Enum'}


def read(path):
    with open(path, encoding='utf-8-sig') as fh:
        return fh.read().replace('\r\n', '\n')


def block(text, tag):
    found = re.findall(r'<%s>\n(.*?)\n\s*</%s>' % (tag, tag), text, re.S)
    assert len(found) == 1, 'expected one <%s> block, found %d' % (tag, len(found))
    return found[0]


def test_every_language_has_a_prompt():
    assert {os.path.basename(p) for p in PROMPTS} == {
        'CodeGen_CPP.md', 'CodeGen_CSharp.md', 'CodeGen_JavaScript.md', 'CodeGen_Pseudocode.md', 'CodeGen_Python.md',
        'CodeGen_Swift.md'}


@pytest.mark.parametrize('path', PROMPTS, ids=os.path.basename)
def test_prompt_names_the_supported_engines_and_no_old_wording(path):
    text = read(path)
    assert 'Unreal Engine 5.6 to 5.8' in text
    for old in ('Unreal Engine 4', 'UE4', 'assessible', 'DO NOT WRAP'):
        assert old not in text, old
    leftover = text.replace('{{N2C_JSON_SPEC}}', '').replace('{{N2C_RESPONSE_FORMAT}}', '')
    assert '{{' not in leftover, 'only the two shared placeholders may use {{'
    assert '### N2C JSON\n' not in text, 'the spec heading belongs to the shared fragment only'


@pytest.mark.parametrize('path', PROMPTS, ids=os.path.basename)
def test_worked_example_parses_and_has_the_response_shape(path):
    text = read(path)
    example_in = json.loads(block(text, 'exampleInput'))
    assert example_in['version'] == '2.0.0' and example_in['graphs'], 'the input is an N2C JSON document'
    out = json.loads(block(text, 'exampleOutput'))
    assert list(out) == ['graphs'] and out['graphs']
    for entry in out['graphs']:
        assert set(entry) == {'graph_name', 'graph_type', 'graph_class', 'code'}
        assert set(entry['code']) == {'graphDeclaration', 'graphImplementation', 'implementationNotes'}
        assert entry['graph_type'] in GRAPH_TYPES
        assert all(isinstance(v, str) for v in entry['code'].values())
        assert entry['code']['graphImplementation'], 'the example shows code'
    names = {g['name'] for g in example_in['graphs']}
    assert {e['graph_name'] for e in out['graphs']} <= names, 'every entry answers a graph of the input'
