# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
The UE path scripts (card 0195), on plain CPython: Scripts/update_agents_ue_path.py writes UE_SOURCE_PATH to the
gitignored AGENTS.local.md and never to the tracked AGENTS.md, and Content/Python/mcp_bridge/ue_python_finder.py
reads LauncherInstalled.dat as JSON, so an InstallLocation with spaces ("C:\\Program Files\\Epic Games\\UE_5.8")
resolves to that folder (the old detect-ue-path.bat split it at the first colon and kept the drive letter).
"""

import importlib.util
import json
import os
import subprocess
import sys

import pytest

REPO = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
UPDATER = os.path.join(REPO, 'Scripts', 'update_agents_ue_path.py')
FINDER = os.path.join(REPO, 'Content', 'Python', 'mcp_bridge', 'ue_python_finder.py')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_the_scripts_moved_out_of_the_packaged_content_folder():
    assert not os.path.exists(os.path.join(REPO, 'Content', 'Scripts')), 'Content/Scripts is packaged; the scripts live in Scripts/'
    for name in ('detect-ue-path.bat', 'detect-ue-path.sh', 'update_agents_ue_path.py'):
        assert os.path.isfile(os.path.join(REPO, 'Scripts', name)), name
    with open(os.path.join(REPO, 'Config', 'FilterPlugin.ini'), encoding='utf-8') as f:
        assert 'Content/Scripts' not in f.read(), 'no exclusion left for a folder that no longer exists'
    with open(os.path.join(REPO, '.gitignore'), encoding='utf-8') as f:
        assert any(line.strip() in ('AGENTS.local.md', '/AGENTS.local.md') for line in f), 'AGENTS.local.md is ignored'


def test_the_updater_writes_agents_local_md_and_never_agents_md(tmp_path):
    updater = load(UPDATER, 'n2c_update_agents_ue_path_under_test')
    target = tmp_path / 'AGENTS.local.md'

    text = updater.update_local_file(r'C:\Program Files\Epic Games\UE_5.8\Engine\Source', str(target))
    assert 'UE_SOURCE_PATH: C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Source\n' in text
    assert text.startswith('# AGENTS.local.md')

    # Run again with another engine: the line is replaced in place, the rest kept
    target.write_text(text + '\nMy own note.\n', encoding='utf-8')
    text = updater.update_local_file('/opt/UE_5.7/Engine/Source', str(target))
    assert text.count('UE_SOURCE_PATH:') == 1 and 'UE_SOURCE_PATH: /opt/UE_5.7/Engine/Source' in text
    assert 'My own note.' in text

    # A file without the line gets it appended
    other = tmp_path / 'other.md'
    other.write_text('notes', encoding='utf-8')
    assert updater.update_local_file('X', str(other)) == 'notes\nUE_SOURCE_PATH: X\n'

    # The command line, with --source, leaves the tracked AGENTS.md as it was
    agents_md = os.path.join(REPO, 'AGENTS.md')
    with open(agents_md, 'rb') as f:
        before = f.read()
    out = tmp_path / 'cli.md'
    proc = subprocess.run([sys.executable, '-I', UPDATER, '--source', 'D:/UE/Engine/Source', '--output', str(out)],
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    assert 'UE_SOURCE_PATH: D:/UE/Engine/Source' in out.read_text(encoding='utf-8')
    with open(agents_md, 'rb') as f:
        assert f.read() == before


@pytest.mark.skipif(sys.platform not in ('win32', 'darwin'), reason='LauncherInstalled.dat exists on Windows and macOS only')
def test_launcher_installed_dat_with_spaces_in_the_install_location(tmp_path, monkeypatch):
    finder = load(FINDER, 'n2c_ue_python_finder_under_test')
    install = tmp_path / 'Program Files' / 'Epic Games' / 'UE_5.8'
    install.mkdir(parents=True)
    data = {'InstallationList': [{'InstallLocation': str(install), 'AppName': 'UE_5.8'}]}
    if sys.platform == 'win32':
        monkeypatch.setenv('PROGRAMDATA', str(tmp_path))
        dat = tmp_path / 'Epic' / 'UnrealEngineLauncher' / 'LauncherInstalled.dat'
    else:
        monkeypatch.setenv('HOME', str(tmp_path))
        dat = tmp_path / 'Library' / 'Application Support' / 'Epic' / 'UnrealEngineLauncher' / 'LauncherInstalled.dat'
    dat.parent.mkdir(parents=True)
    dat.write_text(json.dumps(data), encoding='utf-8')
    assert finder.get_ue_installs_from_launcher() == [('UE_5.8', str(install))]


def test_no_launcher_lists_versions_below_the_floor():
    for name in ('launch_bridge.bat', 'launch_bridge.sh'):
        with open(os.path.join(REPO, 'Content', 'Python', 'mcp_bridge', name), encoding='utf-8') as f:
            text = f.read()
        for old in ('5.0', '5.1', '5.2', '5.3', '5.4', '5.5'):
            assert 'UE_' + old not in text and '\\' + old not in text, f'{name} names UE {old}'
