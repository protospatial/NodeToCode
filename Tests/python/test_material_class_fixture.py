# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The material class fixture (card 0452) is the expression table of Context/translation-pipeline/material-classes.md
(generated from the engine sources by gen_material_classes.py, card 0446) as JSON, so the in-editor coverage spec,
NodeToCode.Material.Capture.Identity, can read it from Source/ (the build hosts link only Source, Content, Resources and
ThirdParty). This test fails when the two drift apart. Regenerate the fixture after regenerating the page:

    python -I Tests/python/test_material_class_fixture.py --write
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGE = ROOT / 'Context' / 'translation-pipeline' / 'material-classes.md'
FIXTURE = ROOT / 'Source' / 'Private' / 'Tests' / 'Fixtures' / 'Material' / 'material-classes.json'
ENGINES = ('5.6', '5.7', '5.8')


def class_name(row_name):
    """The UClass name (no U prefix) of a table row: rows drop the MaterialExpression prefix, DEPRECATED_ rows keep theirs"""
    return row_name if row_name.startswith('DEPRECATED_') else 'MaterialExpression' + row_name


def table_from_page():
    """{engine: {class name: handling}} from the page's "Expression classes" table"""
    table = {engine: {} for engine in ENGINES}
    in_section = False
    for line in PAGE.read_text(encoding='utf-8').splitlines():
        if line.startswith('## Expression classes'):
            in_section = True
            continue
        if in_section and line.startswith('## '):
            break
        if not in_section or not line.startswith('| ') or line.startswith('| Class') or line.startswith('|---'):
            continue
        cells = [cell.strip() for cell in line.strip().strip('|').split('|')]
        name, handling = class_name(cells[0]), cells[6]
        for index, engine in enumerate(ENGINES):
            if cells[2 + index] == 'yes':
                table[engine][name] = handling
    return table


def test_the_page_has_a_table_for_every_engine():
    table = table_from_page()
    for engine in ENGINES:
        assert len(table[engine]) > 300, engine
        assert set(table[engine].values()) <= {'mapped', 'generic', 'refused', 'base'}


def test_the_fixture_is_the_page_table():
    assert json.loads(FIXTURE.read_text(encoding='utf-8')) == table_from_page(), (
        'material-classes.json differs from material-classes.md: run python -I Tests/python/test_material_class_fixture.py --write')


if __name__ == '__main__' and '--write' in sys.argv:
    FIXTURE.parent.mkdir(parents=True, exist_ok=True)
    FIXTURE.write_text(json.dumps(table_from_page(), indent=1, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print('wrote', FIXTURE.relative_to(ROOT))
