# Tests/python

Python tests for `Content/Python` (the `nodetocode` package and the bundled scripts) and for the repo's Python tools.
There are two kinds.

## CPython tests (no editor)

Every `test_*.py` in this folder runs under pytest on plain CPython 3.11, the Python that UE 5.6 to 5.8 embed.
`conftest.py` puts `Content/Python` on `sys.path` and installs a stub `unreal` module before anything imports the
package:

- `unreal.Paths.project_content_dir`, `project_plugins_dir` and `project_dir` point at a fake project in a pytest tmp
  folder (the `ue` fixture);
- `unreal.log`, `log_warning` and `log_error` are recorded (`ue.unreal.messages('error')`);
- any other `unreal.X` raises an `AttributeError` that names the stub. Add a stub attribute only when the code under
  test needs it; code that needs the real editor gets an in-editor test instead.

From the repo root, with [uv](https://docs.astral.sh/uv/):

```
uv run --no-project --python 3.11 --with-requirements Tests/python/requirements.txt -- python -m pytest Tests/python
```

`.\build.ps1 -Test` runs the same tests on each engine's own Python. pytest and its dependencies are pinned in
`requirements.txt` and never ship with the plugin. A known defect is a test marked
`xfail(strict=True, reason='<finding id>')`; the change that fixes it removes the marker.

## In-editor tests (`editor/`)

`editor/test_*.py` need the real `unreal` module and the loaded plugin. pytest does not collect them
(`collect_ignore` in `conftest.py`). `editor/run_editor_tests.py` runs them inside the editor: each module-level
`test_*` function is called with no arguments, and a test fails when it raises. It logs one line per test, each
failure's traceback, and then one of:

```
N2C_EDITOR_TESTS PASS <count>
N2C_EDITOR_TESTS FAIL <failed>/<count>: <file>::<test>, ...
```

The editor's `-ExecutePythonScript` runner quits the editor when the script returns. Headless, from the repo root
(`..\Hosts` are the junctioned host projects `build.ps1` makes; use 5_6 or 5_7 for the other engines):

```
G:\UE\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe ..\Hosts\NodeToCodeHost_5_8\NodeToCodeHost_5_8.uproject -ExecutePythonScript=<repo>/Tests/python/editor/run_editor_tests.py -unattended -nullrhi -nosplash -nosound -stdout -AllowStdOutLogVerbosity
```

`<repo>` is the absolute path of this checkout, with forward slashes. `-AllowStdOutLogVerbosity` puts the result lines
(log verbosity `Log`) on stdout as well; they are always in the host's `Saved/Logs/<host>.log`. Set
`N2C_PORT_OFFSET` first when another editor may be running, so the MCP server does not collide with it. The exit
code does not reflect the result: grep for the `N2C_EDITOR_TESTS` line.

The host projects junction the plugin into `Plugins/NodeToCode`, so they cannot show install-location bugs (a Fab
install under `Engine/Plugins/Marketplace`, a renamed plugin folder). Test those with the CPython tests, which can put
a copy of the package anywhere (`test_scripts.py`, `installed_package`).
