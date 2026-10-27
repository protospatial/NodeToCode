# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Scripts/docs_lint.py (card 0180), the CI step that fails when the agent docs drift from the MCP tool reference or the
reference from the tool sources. The checkout lints clean, and a copy with seeded drift (a renamed tool in an
AGENTS.md, a fake argument, a broken link, a broken wiki link, a wrong tool count, a renamed tool in its source)
fails with one finding per seed, each naming the file and line.
"""

import importlib.util
import os
import shutil

import pytest

from conftest import REPO_ROOT

COPIED = ('AGENTS.md', 'README.md', 'Context', 'Scripts', 'tools', 'wiki', 'Tests/smoke', 'Source/Public/MCP',
          'Source/Private/MCP')


@pytest.fixture(scope='module')
def docs_lint():
    spec = importlib.util.spec_from_file_location('docs_lint', os.path.join(REPO_ROOT, 'Scripts', 'docs_lint.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def substitute(root, path, old, new):
    full = os.path.join(root, path)
    with open(full, 'rb') as f:
        text = f.read().decode('utf-8')
    assert old in text, (path, old)
    with open(full, 'wb') as f:
        f.write(text.replace(old, new, 1).encode('utf-8'))


def line_of(root, path, needle):
    with open(os.path.join(root, path), encoding='utf-8') as f:
        return next(n for n, line in enumerate(f, start=1) if needle in line)


def test_checkout_lints_clean(docs_lint):
    findings, checked = docs_lint.lint(REPO_ROOT)
    assert not findings, '\n'.join(str(f) for f in findings)
    assert checked['tools'] > 0 and checked['sections'] > 0 and checked['arguments'] > 0


@pytest.mark.parametrize('style', ['default', 'posix-absolute', 'relative'])
def test_user_pages_resolve_with_any_path_style(docs_lint, monkeypatch, style):
    """docs_check strips a leading '/' from docsRoot, so handing it an absolute POSIX path (/home/runner/.../wiki, the
    GitHub runner) made every run fail with WIKI_DOCS_ROOT_MISSING; on Windows a drive path hid that. The lint must
    pass docs_check a bare folder name under the folder's parent, whatever path it was given."""
    real = docs_lint.load_docs_check
    seen = []

    def spying(root):
        module = real(root)
        check_tree = module.check_tree

        def check(tree_root, cfg):
            seen.append(cfg['docsRoot'])
            assert cfg['docsRoot'] and not cfg['docsRoot'].startswith('/') and not os.path.isabs(cfg['docsRoot'])
            assert '/' not in cfg['docsRoot'] and '\\' not in cfg['docsRoot'], cfg['docsRoot']
            return check_tree(tree_root, cfg)

        module.check_tree = check
        return module

    monkeypatch.setattr(docs_lint, 'load_docs_check', spying)
    monkeypatch.chdir(REPO_ROOT)  # a relative --wiki is relative to the working directory (build.ps1 runs from anywhere)
    wiki = {'default': None, 'posix-absolute': os.path.join(REPO_ROOT, 'wiki').replace('\\', '/'),
            'relative': 'wiki'}[style]
    findings, checked = docs_lint.lint(REPO_ROOT, wiki)
    assert seen == ['wiki']
    assert not [f for f in findings if f.code.startswith('WIKI_')], '\n'.join(map(str, findings))
    assert os.path.isabs(checked['wiki'])


def test_seeded_drift_fails_with_file_and_line(docs_lint, tmp_path):
    root = str(tmp_path / 'repo')
    for entry in COPIED:
        source, target = os.path.join(REPO_ROOT, entry), os.path.join(root, entry)
        if os.path.isdir(source):
            shutil.copytree(source, target, ignore=shutil.ignore_patterns('__pycache__'))
        else:
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(source, target)
    # The copy has no git, so its tracked files are the files on disk, and it holds only part of the repository (some
    # paths and allowlist entries resolve differently): count only what the seeds add to the copy's own findings.
    def keyed(findings):
        prefix = root.replace(os.sep, '/') + '/'
        return {(f.code, f.path.replace(os.sep, '/').replace(prefix, ''), f.line, f.message) for f in findings}

    before = keyed(docs_lint.lint(root)[0])
    graph = 'Source/Private/MCP/Tools/Implementations/Blueprint/Graph/AGENTS.md'
    compilation = 'Source/Private/MCP/Tools/Implementations/Blueprint/Compilation/AGENTS.md'
    server = 'Context/mcp-server/AGENTS-mcp-server.md'
    page = 'wiki/developers/NodeToCodeTools.md'
    substitute(root, graph, '`set-input-pin-value`, `delete-blueprint-node`', '`set-input-pin-values`, `delete-blueprint-node`')
    substitute(root, compilation, '  - `skipGarbageCollection` (optional)',
               '  - `forceRecompile` (optional): seeded\n  - `skipGarbageCollection` (optional)')
    substitute(root, server, '[threat-model.md](threat-model.md)', '[threat-model.md](threat-modle.md)')
    substitute(root, server, '67 tools by folder', '66 tools by folder')
    substitute(root, page, '(Agent-Skills)', '(Agent-Skillz)')
    substitute(root, 'Source/Private/MCP/Tools/Implementations/FileSystem/N2CMcpReadFileTool.cpp',
               'TEXT("read-file")', 'TEXT("read-files")')

    added = keyed(docs_lint.lint(root)[0]) - before
    got = {key[:3] for key in added}
    report = '\n'.join(map(str, sorted(added)))
    expected = {
        ('TOOL_UNKNOWN', graph, line_of(root, graph, 'set-input-pin-values')),
        ('ARG_UNKNOWN', compilation, line_of(root, compilation, 'forceRecompile')),
        ('LINK_BROKEN', server, line_of(root, server, 'threat-modle.md')),
        ('COUNT_DRIFT', server, line_of(root, server, '66 tools by folder')),
        ('WIKI_LINK_BROKEN', page, line_of(root, page, 'Agent-Skillz')),
    }
    assert expected <= got, report
    stale = [key for key in added if key[0] == 'REF_STALE']
    assert len(stale) == 2 and {'read-files' in key[3] for key in stale} == {True, False}, report
    assert len(added) == len(expected) + 2, report
