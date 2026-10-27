# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Where the per-user switches are (card 0486, found by gate 0481): UN2CUserSettings is Config=EditorPerProjectUserSettings,
so Unreal shows it in Editor Preferences > Plugins > Node to Code (User), not in Project Settings. No tracked doc,
message, skill or hook may send users to "Project Settings > Plugins > Node to Code (User)" (CHANGELOG.md keeps its
history as written).
"""

import os
import re
import subprocess

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
WRONG = re.compile(r'Project\s+Settings\s*(?:>|→|,)\s*"?\s*"?\s*Plugins\s*(?:>|→|,)\s*Node to Code \(User\)')


def test_nothing_names_project_settings_for_the_user_section():
    try:
        files = subprocess.run(['git', 'ls-files'], cwd=REPO, capture_output=True, text=True, check=True).stdout.split('\n')
    except (OSError, subprocess.CalledProcessError):
        files = []
    found = []
    for rel in files:
        if not rel or rel in ('CHANGELOG.md', 'Tests/python/test_user_settings_location.py') or not rel.endswith(('.md', '.cpp', '.h', '.py', '.sh', '.json')):
            continue
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path):
            continue
        with open(path, encoding='utf-8', errors='replace') as f:
            # Join C++ string literals split across lines ("... > "\n\t\t"Plugins ...") before matching
            text = re.sub(r'"\s*\n\s*"', '', f.read())
        if WRONG.search(text):
            found.append(rel)
    assert not found, 'Node to Code (User) is in Editor Preferences, not Project Settings: ' + ', '.join(found)


def test_the_user_settings_class_is_an_editor_preferences_class():
    with open(os.path.join(REPO, 'Source', 'Public', 'Core', 'N2CUserSettings.h'), encoding='utf-8') as f:
        text = f.read()
    # UDeveloperSettings::GetContainerName puts EditorPerProjectUserSettings classes in the Editor container
    assert 'UCLASS(Config = EditorPerProjectUserSettings' in text
