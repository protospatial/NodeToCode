# NodeToCode Development Workflow

`build.ps1` (hosts, switches, smoke and tests), release packaging, UE path detection, IDE setup, and which files are
tracked. The hub `AGENTS.md` keeps only the everyday commands.

## Build Script

`build.ps1` at the plugin root (tracked) is the build, smoke and test runner for UE 5.6, 5.7 and 5.8 (Windows
PowerShell 5.1 or PowerShell 7). "Switches, tests and smoke" below lists every switch; the short form:

```powershell
.\build.ps1 -WhatIf                  # detected engines and host projects
.\build.ps1 -All -Smoke              # build every engine, then the headless smoke check and MCP probes
.\build.ps1 -UEVersion 5.8 -Test     # build, then run Automation RunTests NodeToCode and report pass/fail
```

**What it does:**
- Finds each engine from `-Engine '<ver>=<path>'`, the `N2C_UE_5_X` environment variables, `build.local.ps1`, the
  registry, the Epic launcher's `LauncherInstalled.dat`, registered source builds or the default install folder, and
  checks each against `Engine/Build/Build.version`.
- Creates (on first use) a minimal C++ host project per engine, `<HostsRoot>/NodeToCodeHost_5_X` (default `../Hosts`
  next to the checkout), whose `Plugins/NodeToCode/{Source,Content,Resources,ThirdParty}` are junctions (symbolic
  links on Mac and Linux) back into the checkout; `Binaries/` and `Intermediate/` stay per host. The host's
  `Plugins/NodeToCodeTestContent` links to `Tests/NodeToCodeTestContent`, the content-only plugin with the automation
  tests' assets (the perf fixtures, `NodeToCode.Perf.Fixtures`), which is never packaged.
- Builds the host's editor target through `Engine/Build/BatchFiles/Build.bat` (UBT), then optionally runs
  `Tests/smoke/smoke_check.py` (`-Smoke`: one headless editor per MCP registration mode, log, config, tool-count and
  probe checks against `Tests/smoke/expected.json` and `Tests/mcp_probe/expected.json`) or the automation tests
  with an exported report (`-Test`).
- Writes timestamped logs to `BuildLogs/` (`-LogRoot` to move them) and exits non-zero on any failure.
- Skips a version only when an UnrealEditor is open on this run's own host (its full `.uproject` path, slashes and
  case normalized, is on the editor's command line; Live Coding blocks an external build). An editor on another
  checkout's host of the same version does not count.
- Safe to run from several checkouts at once: UBT logs to `BuildLogs/UBT_<ver>_<stamp>.txt` (`-Log=`) instead of
  the shared per-user `UnrealBuildTool/Log.txt`, and a build that fails at once because another UBT holds a shared
  file (a sharing-violation `IOException`, e.g. on UE 5.8's `UnrealBuildTool/Trace.uba` or a
  `BuildRules/*Rules.dll`) is retried up to 20 times after a random 5-30 s wait. Installed 5.6, 5.7 and 5.8 share
  one per-user `UnrealEngine/Intermediate/Build/BuildRules/MarketplaceRules.dll` and each recompiles the others' copy,
  which a running UBT keeps loaded for its whole build, so two checkouts building different versions at once wait
  for each other here.
- The smoke and test editors (`Tests/mcp_probe/editor_session.py` and `Invoke-Editor`) start with
  `-ddc=InstalledNoZenLocalFallback` and `UE_SKIP_UBT_SDK_SETUP=1` (card 0495). Otherwise 5.6, 5.7 and 5.8 editors
  shut down each other's shared local Zen server, which can stop an editor at startup ("no writable nodes"), and a 5.8
  editor waits at startup on the machine-wide `Build.bat` lock until any other 5.8 build ends.

Machine-specific settings (engine paths, hosts root) go in `build.local.ps1` next to the script; it is gitignored and
dot-sourced when present.

### Switches, tests and smoke

Requirements: the UE 5.6, 5.7 and 5.8 engines, and for `-Smoke` Node.js 22.19 or newer with `npx` on `PATH` (it
fetches the pinned MCP conformance suite and MCP Inspector from npm; checked with Node 24.11.1 and npm 11.6.2). For
the Python step of `-Test`, [uv](https://docs.astral.sh/uv/) on `PATH` (checked with uv 0.12.5); without it that
step is reported as `pytest WARN (uv not found)` and skipped.

```powershell
.\build.ps1 -UEVersion 5.7           # incremental editor build for one version (5.6, 5.7 or 5.8)
.\build.ps1 -All                     # build every detected version in turn
.\build.ps1 -All -Smoke              # build, then the headless smoke check and MCP probes (Tests/smoke/smoke_check.py)
.\build.ps1 -All -Test               # build, then Tests/python (pytest) and Automation RunTests NodeToCode headless
.\build.ps1 -UEVersion 5.8 -Clean    # remove the host's plugin Binaries/Intermediate first
.\build.ps1 -UEVersion 5.6 -Launch   # build, then open the host editor
.\build.ps1 -All -Package            # RunUAT BuildPlugin into -PackagesRoot (a quick check, not a release)
.\build.ps1 -WhatIf                  # list detected engines and host projects; change nothing
.\build.ps1 -WhatIf -HostsRoot D:\Scratch\Hosts   # the same, with the hosts in another folder
```

- Engines are found per version from `-Engine '5.8=<path>'`, then the `N2C_UE_5_6`, `N2C_UE_5_7` and `N2C_UE_5_8`
  environment variables, then `$Engines` in `build.local.ps1`, then the registry, the Epic launcher's
  `LauncherInstalled.dat`, registered source builds and `%ProgramFiles%\Epic Games\UE_5.X`. Each candidate is
  checked against its `Engine/Build/Build.version`; an explicit setting that points at the wrong engine is reported,
  not skipped.
- Machine-specific settings go in `build.local.ps1` next to `build.ps1` (gitignored, dot-sourced when present). It
  may set `$Engines = @{ '5.8' = '<path>' }`, `$HostsRoot`, `$PackagesRoot` and `$LogRoot`; parameters given on the
  command line win.
- `-HostsRoot`, `-PackagesRoot` and `-LogRoot` move the hosts, the `-Package` output and the logs (default
  `BuildLogs/`). `-Config` picks the build configuration (default `Development`).
- `-Test` runs `UnrealEditor-Cmd <host> -ExecCmds="Automation RunTests NodeToCode"
  -TestExit="Automation Test Queue Empty" -ReportExportPath=<dir>` with `-unattended -nullrhi -NoLiveCoding
  -culture=en`, reads the report's `index.json`, fails on any failed or unfinished test and warns when none ran.
  `-TestFilter` narrows the run and `-TestTimeoutMinutes` (default 60) stops a hung editor. `-UpdateGoldens` passes
  `-N2CUpdateGoldens`, so the golden specs rewrite `Source/Private/Tests/Goldens/` instead of comparing, and
  `-TestCulture de` replaces `-culture=en` for a locale check (see "Capture goldens" in the tests README). If
  `-ExecCmds` ever holds more than one command, separate them with commas: a semicolon hangs the editor.
- On UE 5.8 and later the `-Test` editor also gets `-EnablePlugins=ToolsetRegistry` (the host `.uproject` stays as
  it is), so `Content/Python/init_unreal.py` registers NodeToCodeTools and its in-editor Python tests
  (`Content/Python/nodetocode/toolsets/tests/`, 60 s each) as `NodeToCode.Toolsets.PythonTests.*`. They run in the
  same pass, any failure fails it, the summary counts them apart (`tests PASS (855, 36 toolset)`), and when the
  filter covers them but none ran (the toolset or its runner did not register) the step fails. 5.6 and 5.7 run as
  before.
- Before that, `-Test` runs the CPython tests in `Tests/python` on each engine's own Python (3.11.8 in UE 5.6, 5.7
  and 5.8): `uv run --no-project --python <engine python> --with-requirements Tests/python/requirements.txt --
  python -m pytest -q Tests/python`, in a throwaway environment, so the engine install is not changed. They test
  `Content/Python/nodetocode` (the script library: name and category checks, registry load, merge and corruption
  handling, search scoring, `run_script`, path containment, import resolution) and check that
  `Content/Python/scripts/script_registry.json` and the `.py` files beside it match. `Tests/python/conftest.py`
  puts a stub `unreal` module in `sys.modules` (`Paths.project_content_dir`/`project_plugins_dir`/`project_dir`
  pointed at pytest tmp folders, and the log functions), so no editor or UE install is needed. The counts land in
  the summary (`pytest PASS (109 passed, 10 xfailed)`), the output in `BuildLogs/PyTest_<ver>_<stamp>.log`. Without
  an engine, from the repo root: `uv run --no-project --python 3.11 --with-requirements Tests/python/requirements.txt
  -- python -m pytest Tests/python`. pytest and its dependencies are pinned in `requirements.txt` and are never
  added to the plugin. A known defect is a test marked `pytest.mark.xfail(strict=True, reason='<finding id>')`:
  the card that fixes it removes the marker, since a strict xfail that passes fails the run. A new stub attribute
  goes in the conftest only when the code under test needs it; any other `unreal.X` raises `AttributeError`.
  Tests that need the real editor go in `Tests/python/editor` (pytest skips it) and run with
  `Tests/python/editor/run_editor_tests.py` through `-ExecutePythonScript`; `Tests/python/README.md` has both commands.
- `-Smoke` runs `Tests/smoke/smoke_check.py` with the engine's own Python: one headless `-N2CMcp` editor per MCP
  tool-registration mode (default, dynamic discovery, Python script-only, set in the host's
  `Config/DefaultNodeToCode.ini` and put back), plus the `Tests/mcp_probe` probes in the default session. It fails on
  an error, ensure, assertion or crash line in the editor log (the allowlist in the script names each benign
  exception and why), a missing MCP start line or `/mcp/health` answer from that editor, a `tools/list` count other
  than `Tests/smoke/expected.json`, any change to the host's `Config/*.ini` during a session, a failed
  `get_focused_blueprint_json` on an engine Blueprint, or a probe status other than `Tests/mcp_probe/expected.json`.
  A card that adds or removes an MCP tool updates `Tests/smoke/expected.json`; failures already present are listed
  there under `known_failures` with their finding ids. About 1.5 minutes per engine, with the conformance step below.
- In the default session `-Smoke` also runs `Tests/conformance/run_conformance.py`: the official MCP conformance
  suite (`npx @modelcontextprotocol/conformance server --requirements 2025-11-25`) and the MCP Inspector CLI
  (`--cli <url> --method tools/list`), at the exact versions pinned in `Tests/conformance/baseline.json`, about 4 s.
  The baseline lists every scenario once as `pass`, `known_failures` (owning card, failing checks, why) or
  `not_applicable` (why). The smoke step fails when a `pass` scenario fails or a check outside an entry fails, and
  prints `NOW PASSES` when a listed failure passes: the owning card then moves it to `pass` in its own commit. The
  JSON results land in `BuildLogs/Smoke_<ver>_<stamp>_conformance/`. The rejected-Origin case is probe P1's.
- Logs: `BuildLogs/Build_<version>_<timestamp>.log`, `Smoke_*.log`, `Tests_*.log` (report in `Tests_*/`) and
  `Package_*.log`. The script exits non-zero when any build, smoke check or test run fails.
- If a host editor is open, that version is skipped (Live Coding blocks UBT): use Ctrl+Alt+F11 in the editor, or
  close it.

Automation tests live in `Source/Private/Tests/`; its `README.md` gives the naming (`NodeToCode.<Area>.<Thing>`),
layout, shared helpers, the known-defect rule and how to run them. Live MCP probes and the headless editor helper
live in `Tests/mcp_probe/`.

Release packages come from `Scripts/package-release.ps1` (tracked), never from `build.ps1 -Package`. It exports a
commit with `git archive` (so nothing uncommitted or ignored can ship), runs RunUAT BuildPlugin for UE 5.6, 5.7 and
5.8, writes `NodeToCode-<VersionName>-UE<ver>.zip` plus a SHA256SUMS file to `..\Packages`, verifies each zip
(required and forbidden files, `.uplugin` fields, no secrets), rebuilds it from source in a scratch C++ host and
loads it in a headless editor to check the MCP server answers. It never tags or uploads.

```powershell
.\Scripts\package-release.ps1                              # HEAD, all three engines
.\Scripts\package-release.ps1 -Ref <tag-or-sha> -UEVersion 5.8
```

### BuildLogs Directory

Logs are named `Build_<ver>_<stamp>.log` (UBT's own log `UBT_<ver>_<stamp>.txt`, a retried attempt's build log `Build_<ver>_<stamp>_try<n>.log`), `Smoke_<ver>_<stamp>.log` (editor logs `Smoke_<ver>_<stamp>_<mode>.log`),
`Tests_<ver>_<stamp>.log` (with the automation
report in `Tests_<ver>_<stamp>/`) and `Package_<ver>_<stamp>.log`. The directory is git-ignored and accumulates logs
over time.

---

## UE Path Detection Scripts

These scripts detect the newest UE installation and write its `Engine/Source` folder as the `UE_SOURCE_PATH` line of
`AGENTS.local.md` at the repository root, for AI coding agents to read. That file is gitignored and machine-specific;
the tracked `AGENTS.md` only points to it and is never rewritten (card 0195). The scripts are developer tools in
`Scripts/`, outside every plugin folder, so no package contains them.

### Running the Detection

**Windows:**
```batch
Scripts\detect-ue-path.bat
```

**macOS/Linux:**
```bash
Scripts/detect-ue-path.sh
```

Or with any Python 3: `python Scripts/update_agents_ue_path.py [--source <UE>/Engine/Source] [--output <file>]`.

### How Detection Works

1. **Shell scripts** (`detect-ue-path.sh`/`.bat`) locate a Python interpreter the same way as the bridge
   launchers: they run `Content/Python/mcp_bridge/ue_python_finder.py` (with `py -3`, `python3` or `python`), which
   prints UE's bundled Python (`Engine/Binaries/ThirdParty/Python3/`) of the newest install, else the Python that
   ran it. Without a Python on PATH, a registered (Windows) or common-folder (macOS/Linux) UE install's Python runs it.

2. **Windows-specific detection methods** (in `ue_python_finder.py`, any UE version):
   - Registry: `HKEY_LOCAL_MACHINE\SOFTWARE\EpicGames\Unreal Engine\<version>`
   - Registry: `HKEY_CURRENT_USER\SOFTWARE\Epic Games\Unreal Engine\Builds` (custom builds)
   - LauncherInstalled.dat: `%PROGRAMDATA%\Epic\UnrealEngineLauncher\LauncherInstalled.dat`
   - Common paths: every `UE_*` folder in `C:\Program Files\Epic Games`, `C:\UE`, `D:\UE`, etc.

3. **macOS-specific detection paths:**
   - `/Users/Shared/Epic Games`
   - `$HOME/Epic Games`
   - `/Applications/Epic Games`

4. **Python script** (`Scripts/update_agents_ue_path.py`):
   - Uses `ue_python_finder.py` from the MCP bridge (registry, `LauncherInstalled.dat` read as JSON, common folders)
   - Writes the `UE_SOURCE_PATH:` line of `AGENTS.local.md`: replaces it when the file has one, appends it otherwise,
     and creates the file with a short header the first time; anything else in the file is kept
   - Prefers newest UE version when multiple are found; `--source` skips detection
   - Tests: `Tests/python/test_ue_path_scripts.py`

### Source Files

| File | Description |
|------|-------------|
| `Scripts/detect-ue-path.sh` | macOS/Linux shell wrapper |
| `Scripts/detect-ue-path.bat` | Windows batch wrapper |
| `Scripts/update_agents_ue_path.py` | Python script that writes `AGENTS.local.md` |

---

## One checkout, every engine (replaces the git-submodule setup)

The hosts `build.ps1` creates are the multi-version setup: each `NodeToCodeHost_5_X` links
`Plugins/NodeToCode/{Source,Content,Resources,ThirdParty}` back into this checkout (junctions on Windows, symbolic
links on macOS and Linux), so one checkout builds and tests on UE 5.6, 5.7 and 5.8 with no copies to keep in sync. The
1.x workflow of a git submodule per test project is no longer used. To try a change in your own project instead, copy
the plugin into its `Plugins/` folder as `wiki/Plugin-Installation.md` describes.

## IDE Configuration

Open a host's project in Rider or Visual Studio (`.\build.ps1 -UEVersion 5.8 -Launch` builds and opens it; or open
`<HostsRoot>/NodeToCodeHost_5_8/NodeToCodeHost_5_8.uproject` in Rider): the plugin's sources are under its
`Plugins/NodeToCode` link, and edits land in this checkout. Generate project files from that `.uproject` when a new
source file is added.

---

## Development File Conventions

### Git-Tracked vs Local-Only Files

The `.gitignore` defines which development files are tracked:

**Tracked (committed to repo):**
- `AGENTS.md` - AI coding agent instructions (shared)
- `Context/` - Documentation hub files
- Source code and content assets
- `build.ps1`, `Scripts/`, `Tests/` - Build, release and test tooling

**Ignored (local-only):**
- `CLAUDE.md` - Local AI agent instructions (personal)
- `.claude/` - Claude Code conversation data
- `Development/` - Local development planning/notes
- `BuildLogs/` - Build output logs
- `build.local.ps1` - Machine-specific settings for `build.ps1` (engine paths, hosts root)
- `.aider*` - Aider AI assistant files
- `.mcp.json` - MCP server configuration
- `.env` - Environment variables

### Rationale

- **build.ps1 tracked, build.local.ps1 ignored**: the script finds engines and creates hosts itself; only per-machine overrides stay local
- **CLAUDE.md ignored**: Allows personal customization without polluting shared repo
- **AGENTS.md tracked**: Shared AI agent instructions that should be consistent
