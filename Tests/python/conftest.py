# Copyright (c) 2026 Nick McClure (Protospatial)
# SPDX-License-Identifier: Apache-2.0
"""
Plain-CPython test setup for the NodeToCode Python package (Content/Python/nodetocode).

The package imports `unreal`, which only exists inside the editor. This file installs a small stand-in module in
sys.modules before anything imports the package, so the logic (script library paths, registry, search, run_script)
runs on any CPython without UE installed. The stub has only what the tested code uses: unreal.Paths
(project_content_dir, project_plugins_dir, project_dir) and unreal.log / log_warning / log_error. Any other
attribute raises AttributeError, so a new editor dependency shows up as a test failure instead of passing silently.

Run (from the repo root; 3.11 is the Python UE 5.6-5.8 embed):
    uv run --no-project --python 3.11 --with-requirements Tests/python/requirements.txt -- python -m pytest Tests/python
"""

import os
import shutil
import sys
import types

import pytest

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CONTENT_PYTHON = os.path.join(REPO_ROOT, 'Content', 'Python')
PLUGIN_SCRIPTS = os.path.join(CONTENT_PYTHON, 'scripts')

# Content/Python is junctioned into the host projects and packaged (minus __pycache__): never write .pyc files there
sys.dont_write_bytecode = True
if CONTENT_PYTHON not in sys.path:
    sys.path.insert(0, CONTENT_PYTHON)


def _ue_dir(path):
    """UE's Paths functions return forward slashes with a trailing slash."""
    return path.replace('\\', '/').rstrip('/') + '/'


class _UnrealStub(types.ModuleType):
    """Stand-in for the editor's `unreal` module. Tests point the paths at pytest tmp folders through `ue`."""

    def __init__(self):
        super().__init__('unreal')
        self.__file__ = __file__
        self.logs = []  # (level, message)
        self._content_dir = ''
        self._plugins_dir = ''
        self._project_dir = ''
        stub = self

        class Paths:
            @staticmethod
            def project_content_dir():
                return stub._content_dir

            @staticmethod
            def project_plugins_dir():
                return stub._plugins_dir

            @staticmethod
            def project_dir():
                return stub._project_dir

        self.Paths = Paths

    def log(self, message):
        self.logs.append(('info', str(message)))

    def log_warning(self, message):
        self.logs.append(('warning', str(message)))

    def log_error(self, message):
        self.logs.append(('error', str(message)))

    def messages(self, level):
        return [m for lvl, m in self.logs if lvl == level]


UNREAL = _UnrealStub()
sys.modules['unreal'] = UNREAL

import nodetocode.scripts  # noqa: E402  (needs the stub above)

REAL_PLUGIN_SCRIPTS_LOOKUP = nodetocode.scripts._get_plugin_scripts_dir


class UEProject:
    """A fake UE project in a tmp folder: <root>/Project/{Content,Plugins} that the stub's Paths point at."""

    def __init__(self, root):
        self.root = str(root)
        self.project_dir = os.path.join(self.root, 'Project')
        self.content_dir = os.path.join(self.project_dir, 'Content')
        self.plugins_dir = os.path.join(self.project_dir, 'Plugins')
        self.content_python = os.path.join(self.content_dir, 'Python')
        self.scripts_dir = os.path.join(self.content_python, 'scripts')
        self.registry_path = os.path.join(self.scripts_dir, 'script_registry.json')
        # Where the bundled scripts are read from in these tests; empty until install_plugin()
        self.plugin_scripts_dir = os.path.join(self.plugins_dir, 'NodeToCode', 'Content', 'Python', 'scripts')
        for d in (self.content_dir, self.plugins_dir):
            os.makedirs(d)
        UNREAL._project_dir = _ue_dir(self.project_dir)
        UNREAL._content_dir = _ue_dir(self.content_dir)
        UNREAL._plugins_dir = _ue_dir(self.plugins_dir)

    unreal = UNREAL  # the stub, for its captured log lines: ue.unreal.messages('error')

    def install_plugin(self, folder='NodeToCode'):
        """Copy the repo's bundled scripts to <Project>/Plugins/<folder>/Content/Python/scripts and return that dir."""
        target = os.path.join(self.plugins_dir, folder, 'Content', 'Python', 'scripts')
        shutil.copytree(PLUGIN_SCRIPTS, target, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        return target

    def write_registry(self, text):
        os.makedirs(self.scripts_dir, exist_ok=True)
        with open(self.registry_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)

    def read_registry(self):
        with open(self.registry_path, 'r', encoding='utf-8') as f:
            return f.read()

    def files(self):
        """Every file under the tmp root, as '/'-separated paths relative to it."""
        found = []
        for folder, _, names in os.walk(self.root):
            for name in names:
                found.append(os.path.relpath(os.path.join(folder, name), self.root).replace('\\', '/'))
        return sorted(found)


@pytest.fixture
def ue(tmp_path, monkeypatch):
    """
    A fresh fake project with no bundled scripts and an empty log.

    The bundled-scripts folder is pinned to ue.plugin_scripts_dir through scripts._get_plugin_scripts_dir, so these
    tests do not depend on how the real lookup finds the plugin (that lookup is broken for Fab installs,
    mcp-tools-python-translation-resources-19, and its own tests use real_plugin_lookup). If the lookup moves or is
    renamed, update this seam.
    """
    import nodetocode.scripts as scripts
    UNREAL.logs.clear()
    project = UEProject(tmp_path)
    monkeypatch.setattr(scripts, '_get_plugin_scripts_dir', lambda: project.plugin_scripts_dir)
    yield project
    UNREAL._content_dir = UNREAL._plugins_dir = UNREAL._project_dir = ''


@pytest.fixture
def ue_with_plugin(ue):
    """The fake project with a copy of the bundled scripts at <Project>/Plugins/NodeToCode."""
    ue.install_plugin()
    return ue


@pytest.fixture
def real_plugin_lookup(ue, monkeypatch):
    """The fake project, with the module's own bundled-scripts lookup instead of the pinned folder."""
    import nodetocode.scripts as scripts
    monkeypatch.setattr(scripts, '_get_plugin_scripts_dir', REAL_PLUGIN_SCRIPTS_LOOKUP)
    return ue


@pytest.fixture
def scripts_module():
    import nodetocode.scripts as scripts
    return scripts
