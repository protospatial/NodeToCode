# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Tests/DocsShots/capture.py's manifest rules (card 0519): a retake stamps every shot of its group with the commit
checked out, also when the picture comes out byte-identical, and the retired --restamp leaves shots.json alone.

#262 retook the settings shots byte-identical and then restamped them to each image's last commit, an older commit
than the code change, so docs_check.py --stale called them stale again. Nothing here starts an editor: record_shots is
what capture.py runs after the automation test wrote its PNGs.
"""

import importlib.util
import io
import json
import os
import sys
from contextlib import redirect_stdout

import pytest

from conftest import REPO_ROOT

OLD = '1' * 40
NEW = '2' * 40


@pytest.fixture(scope='module')
def capture():
    spec = importlib.util.spec_from_file_location('docs_capture_0519', os.path.join(REPO_ROOT, 'Tests', 'DocsShots', 'capture.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as fh:
        fh.write(data)


@pytest.fixture
def docs(tmp_path):
    """A wiki/images folder with one group of two shots stamped OLD, and the test's output folder"""
    images = tmp_path / 'images'
    out = tmp_path / 'out'
    write(str(images / 'g' / 'a.png'), b'picture a')
    write(str(images / 'g' / 'b.png'), b'picture b')
    manifest = images / 'shots.json'
    manifest.write_text(json.dumps({'shots': {'g/a.png': {'commit': OLD}, 'g/b.png': {'commit': OLD}}}), encoding='utf-8')
    return images, out, manifest


def stamps(manifest):
    return {k: v['commit'] for k, v in json.loads(manifest.read_text(encoding='utf-8'))['shots'].items()}


def test_a_byte_identical_retake_takes_the_new_stamp(capture, docs):
    images, out, manifest = docs
    write(str(out / 'g' / 'a.png'), b'picture a')  # the same bytes as committed
    write(str(out / 'g' / 'b.png'), b'picture b changed')
    ok, written, errors = capture.record_shots(['g'], str(out), NEW, images=str(images), manifest_path=str(manifest))
    assert ok and not errors
    assert sorted(written) == ['wiki/images/g/a.png', 'wiki/images/g/b.png']
    assert stamps(manifest) == {'g/a.png': NEW, 'g/b.png': NEW}, 'an unchanged picture still records the retake commit'
    assert (images / 'g' / 'a.png').read_bytes() == b'picture a'


def test_a_shot_the_group_no_longer_writes_leaves_the_manifest(capture, docs):
    images, out, manifest = docs
    write(str(out / 'g' / 'a.png'), b'picture a')
    capture.record_shots(['g'], str(out), NEW, images=str(images), manifest_path=str(manifest))
    assert stamps(manifest) == {'g/a.png': NEW}
    assert not (images / 'g' / 'b.png').exists()


def test_restamp_is_retired_and_changes_nothing(capture, tmp_path, monkeypatch):
    # A real shot of this checkout with a stamp newer than its image's last commit: the old --restamp moved it back
    real = 'translate-entry/graph-overlay.png'
    assert os.path.isfile(os.path.join(REPO_ROOT, 'wiki', 'images', *real.split('/')))
    manifest = tmp_path / 'shots.json'
    before = json.dumps({'shots': {real: {'commit': NEW}}}, indent=2) + '\n'
    manifest.write_text(before, encoding='utf-8')
    monkeypatch.setattr(capture, 'MANIFEST', str(manifest))
    monkeypatch.setattr(sys, 'argv', ['capture.py', '--restamp'])
    buf = io.StringIO()
    with redirect_stdout(buf):
        assert capture.main() == 0
    assert manifest.read_text(encoding='utf-8') == before, '--restamp must leave shots.json as it is'
    note = json.loads(buf.getvalue())['note']
    assert '0519' in note and 'retake' in note


def test_differing_shots_names_changed_missing_and_extra_pictures(capture, tmp_path):
    # --repeat-check (card 0522) compares two capture folders byte for byte
    first, second = tmp_path / 'first', tmp_path / 'second'
    write(str(first / 'g' / 'same.png'), b'same')
    write(str(second / 'g' / 'same.png'), b'same')
    write(str(first / 'g' / 'ring.png'), b'arc 1')
    write(str(second / 'g' / 'ring.png'), b'arc 2')
    write(str(first / 'g' / 'gone.png'), b'only here')
    write(str(second / 'g' / 'new.png'), b'only there')
    write(str(second / 'g' / 'notes.txt'), b'not a picture')
    assert capture.differing_shots(str(first), str(second), ['g']) == ['g/gone.png', 'g/new.png', 'g/ring.png']
    assert capture.differing_shots(str(first), str(first), ['g', 'absent']) == []
