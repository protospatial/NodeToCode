# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
tools/docs_check.py --stale when a squash merge drops the commit a shot was stamped with (card 0517).

capture.py stamps each shot in wiki/images/shots.json with the commit checked out. A card branch retakes shots, the
squash merge into dev-2.0 drops that branch commit, and the stamp points at nothing in dev-2.0's history (#240, #254).
The stale check then uses the commit that wrote the stamp into the manifest (the squash commit). Each test builds a
throwaway git repository: a branch changes a surface and captures, the branch is squash-merged and garbage-collected
so the stamped commit no longer exists, and the check must call the shot fresh, then stale after the surface changes.
"""

import importlib.util
import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

from conftest import REPO_ROOT

pytestmark = pytest.mark.skipif(shutil.which('git') is None, reason='needs git on PATH')

CFG = {
    'ignore': [],
    'images': {'dir': 'wiki/images', 'manifest': 'wiki/images/shots.json'},
    'surfaces': [
        {'name': 'ui', 'code': ['src/ui/**'], 'shots': ['wiki/images/ui/**'], 'group': 'ui'},
        {'name': 'menu', 'code': ['src/menu/**'], 'shots': ['wiki/images/menu/**'], 'group': 'menu'},
    ],
}


@pytest.fixture(scope='module')
def docs_check():
    spec = importlib.util.spec_from_file_location('docs_check_0517', os.path.join(REPO_ROOT, 'tools', 'docs_check.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Repo:
    def __init__(self, root):
        self.root = Path(root)
        self.root.mkdir(parents=True)
        self.env = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@example.invalid', GIT_COMMITTER_NAME='t',
                        GIT_COMMITTER_EMAIL='t@example.invalid', GIT_CONFIG_NOSYSTEM='1')
        self.git('init', '-q', '-b', 'main')
        self.git('config', 'core.autocrlf', 'false')

    def git(self, *args):
        r = subprocess.run(['git', '-C', str(self.root), *args], capture_output=True, text=True, env=self.env)
        assert r.returncode == 0, r.stderr
        return r.stdout.strip()

    def write(self, rel, text):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(text.encode('utf-8'))

    def commit(self, message):
        self.git('add', '-A')
        self.git('commit', '-q', '-m', message)
        return self.git('rev-parse', 'HEAD')

    def capture(self, groups):
        """What capture.py does: write each group's image and stamp it with the commit checked out."""
        head = self.git('rev-parse', 'HEAD')
        path = self.root / 'wiki/images/shots.json'
        manifest = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {'shots': {}}
        for g in groups:
            self.write(f'wiki/images/{g}/shot.png', f'{g} as of {head}')
            manifest['shots'][f'{g}/shot.png'] = {'commit': head}
        self.write('wiki/images/shots.json', json.dumps(manifest, indent=2, sort_keys=True) + '\n')
        return head

    def squash_merge(self, branch, message):
        """dev-2.0's merge style: one squash commit, the branch deleted and its commits garbage-collected."""
        self.git('switch', '-q', 'main')
        self.git('merge', '-q', '--squash', branch)
        squash = self.commit(message)
        self.git('branch', '-q', '-D', branch)
        self.git('reflog', 'expire', '--expire=now', '--all')
        self.git('gc', '-q', '--prune=now')
        return squash

    def exists(self, sha):
        return subprocess.run(['git', '-C', str(self.root), 'cat-file', '-e', sha + '^{commit}'],
                              capture_output=True, env=self.env).returncode == 0


def stale(docs_check, repo):
    return {f.path: f.message for f in docs_check.check_stale(repo.root, CFG)}


@pytest.fixture
def repo(tmp_path):
    r = Repo(tmp_path / 'repo')
    r.write('src/ui/widget.txt', 'v1')
    r.write('src/menu/menu.txt', 'v1')
    r.commit('base')
    return r


def test_stamp_from_a_squashed_away_commit_is_judged_by_the_squash_commit(docs_check, repo):
    repo.git('switch', '-q', '-c', 'card')
    repo.write('src/ui/widget.txt', 'v2')
    stamped = repo.commit('change the ui')
    assert repo.capture(['ui']) == stamped
    repo.commit('retake ui')
    squash = repo.squash_merge('card', 'card (#1)')
    assert not repo.exists(stamped), 'the stamped branch commit must be gone, as in a fresh CI clone of dev-2.0'

    assert stale(docs_check, repo) == {}, 'fresh shots from a squash merge must not be stale'

    repo.write('src/ui/widget.txt', 'v3')
    repo.commit('change the ui again')
    found = stale(docs_check, repo)
    assert list(found) == ['wiki/images/ui/shot.png']
    assert f'captured at {squash[:8]}' in found['wiki/images/ui/shot.png']
    assert f'stamp {stamped[:8]} is not in this history' in found['wiki/images/ui/shot.png']


def test_stamp_in_history_is_used_as_it_is(docs_check, repo):
    repo.git('switch', '-q', '-c', 'card')
    stamped = repo.capture(['ui'])
    repo.commit('capture ui')
    assert stale(docs_check, repo) == {}
    repo.write('src/ui/widget.txt', 'v2')
    repo.commit('change the ui')
    found = stale(docs_check, repo)
    assert list(found) == ['wiki/images/ui/shot.png']
    assert found['wiki/images/ui/shot.png'].startswith(f'captured at {stamped[:8]}, ')


def test_a_later_retake_of_another_group_does_not_refresh_a_shared_squashed_stamp(docs_check, repo):
    # Both groups captured on one branch share a stamp. After the squash merge the menu code changes, then a later
    # card retakes only ui: that commit also edits the manifest (and removes one copy of the old stamp), but menu's
    # shot is still from the squash commit and must stay stale.
    repo.git('switch', '-q', '-c', 'card')
    repo.write('src/ui/widget.txt', 'v2')
    repo.commit('change the ui')
    stamped = repo.capture(['ui', 'menu'])
    repo.commit('capture both')
    repo.squash_merge('card', 'card (#1)')
    assert not repo.exists(stamped)
    repo.write('src/menu/menu.txt', 'v2')
    repo.commit('change the menu')
    repo.git('switch', '-q', '-c', 'card2')
    repo.capture(['ui'])
    repo.commit('retake ui')
    repo.squash_merge('card2', 'card2 (#2)')

    found = stale(docs_check, repo)
    assert list(found) == ['wiki/images/menu/shot.png']
