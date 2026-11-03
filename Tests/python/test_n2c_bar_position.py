# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Where the N2C bar is (card 0520): FN2CGraphOverlayInjector::InjectOverlay puts it at the bottom left of a graph
(HAlign_Left, VAlign_Bottom). A UI string, header comment or agent page that places the bar at the top right sends
users to the wrong corner.
"""

import os
import re

from conftest import REPO_ROOT

INJECTOR = os.path.join(REPO_ROOT, 'Source', 'Private', 'Core', 'N2CGraphOverlayInjector.cpp')
SEARCHED = [os.path.join(REPO_ROOT, 'Source'), os.path.join(REPO_ROOT, 'Context'), os.path.join(REPO_ROOT, 'wiki')]
# A sentence that names the bar (or its widget) and a top-right corner
WRONG = re.compile(r'(N2C bar|SN2CGraphOverlay|graph overlay|Overlay widget)[^.\n]*top[- ]right', re.I)


def test_the_injector_puts_the_bar_at_the_bottom_left():
    text = open(INJECTOR, encoding='utf-8').read()
    assert '.HAlign(HAlign_Left)' in text and '.VAlign(VAlign_Bottom)' in text


def test_no_text_puts_the_bar_at_the_top_right():
    hits = []
    for root in SEARCHED:
        for base, _, names in os.walk(root):
            for name in names:
                if not name.endswith(('.cpp', '.h', '.md')):
                    continue
                path = os.path.join(base, name)
                for number, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
                    if WRONG.search(line):
                        hits.append('%s:%d' % (os.path.relpath(path, REPO_ROOT), number))
    assert not hits, 'the N2C bar is at the bottom left of a graph: %s' % hits
