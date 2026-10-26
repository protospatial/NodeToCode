# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Where the UI presentation tokens live (card 0187): FN2CUIColors and the N2C::UI helpers are in
Source/Public/Core/Style/N2CUIColors.h, not in Core/N2CSettings.h, so a settings edit does not recompile every widget.
A widget source that includes the settings header must use the settings class; one that only needs colors includes
the style header instead.
"""

import os
import re

from conftest import REPO_ROOT

SOURCE = os.path.join(REPO_ROOT, 'Source')
SETTINGS_H = os.path.join(SOURCE, 'Public', 'Core', 'N2CSettings.h')
COLORS_H = os.path.join(SOURCE, 'Public', 'Core', 'Style', 'N2CUIColors.h')
SETTINGS_INCLUDE = '#include "Core/N2CSettings.h"'

# Presentation code: widgets, the style set and the code editor's style
STYLE_DIRS = [
    os.path.join(SOURCE, 'Private', 'Core', 'Widgets'),
    os.path.join(SOURCE, 'Public', 'Core', 'Widgets'),
    os.path.join(SOURCE, 'Private', 'Core', 'Style'),
    os.path.join(SOURCE, 'Public', 'Core', 'Style'),
]


def _read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def _sources(dirs):
    for d in dirs:
        for root, _, names in os.walk(d):
            for name in names:
                if name.endswith(('.h', '.cpp')):
                    yield os.path.join(root, name)


def test_settings_header_has_no_ui_color_tokens_or_helpers():
    text = _read(SETTINGS_H)
    assert not re.search(r'\bstruct\s+FN2CUIColors\b', text)
    for helper in ('N2CUI', 'UIL', 'UILA', 'UIBind', 'UIBindAlpha'):
        assert not re.search(r'\b%s\s*\(' % helper, text), helper
    assert '#include "Core/Style/N2CUIColors.h"' in text  # the UIColors property still has its type


def test_color_tokens_and_helpers_live_in_the_style_header():
    text = _read(COLORS_H)
    assert re.search(r'USTRUCT\(BlueprintType\)\s*struct\s+FN2CUIColors\b', text)
    assert re.search(r'namespace\s+N2C::UI\b', text)
    for helper in ('Colors', 'Linear', 'LinearAlpha', 'Bind', 'BindAlpha'):
        assert re.search(r'\b%s\s*\(' % helper, text), helper
    assert SETTINGS_INCLUDE not in text


def test_style_code_includes_settings_only_when_it_uses_them():
    offenders = []
    for path in _sources(STYLE_DIRS):
        text = _read(path)
        if SETTINGS_INCLUDE in text and 'UN2CSettings' not in text:
            offenders.append(os.path.relpath(path, REPO_ROOT))
    assert not offenders, 'include Core/Style/N2CUIColors.h instead: %s' % offenders


def test_old_global_helpers_are_gone_from_the_source():
    pattern = re.compile(r'(?<![:\w])(N2CUI|UIL|UILA|UIBind|UIBindAlpha)\s*\(')
    hits = []
    for path in _sources([SOURCE]):
        for n, line in enumerate(_read(path).splitlines(), 1):
            if pattern.search(line):
                hits.append('%s:%d' % (os.path.relpath(path, REPO_ROOT), n))
    assert not hits, hits
