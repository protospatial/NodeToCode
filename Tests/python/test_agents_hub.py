# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The root AGENTS.md is a short hub (card 0333): under 100 lines, no @-import (which would load a whole page into every
agent session), and a link to every Context page and every tracked AGENTS.md under Source/, so a new page cannot be
left out of it. Broken links and repository paths are Scripts/docs_lint.py's job.
"""

import os
import re
import subprocess

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))


def hub():
    with open(os.path.join(REPO, 'AGENTS.md'), encoding='utf-8') as f:
        return f.read()


def linked_paths(text):
    return {os.path.normpath(target) for target in re.findall(r'\]\(([^)#\s]+)', text)}


def test_the_hub_is_short_and_imports_nothing():
    text = hub()
    assert len(text.splitlines()) < 100
    assert not [line for line in text.splitlines() if line.lstrip().startswith('@')], 'no @-import'


def test_the_hub_links_every_context_page_and_every_tools_agents_md():
    links = linked_paths(hub())
    pages = []
    for root, _, files in os.walk(os.path.join(REPO, 'Context')):
        pages += [os.path.relpath(os.path.join(root, name), REPO) for name in files if name.endswith('.md')]
    try:
        tracked = subprocess.run(['git', 'ls-files', '*AGENTS.md'], cwd=REPO, capture_output=True, text=True,
                                 check=True).stdout.split()
    except (OSError, subprocess.CalledProcessError):
        tracked = []
    pages += [path for path in tracked if path != 'AGENTS.md']
    missing = sorted(os.path.normpath(page) for page in pages if os.path.normpath(page) not in links)
    assert not missing, 'AGENTS.md does not link: ' + ', '.join(missing)
