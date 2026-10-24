# NodeToCode Development Workflow

Build scripts, UE path detection, git submodule setup, and IDE configuration for multi-version UE plugin development.

## Build Script

`build.ps1` at the plugin root (tracked) is the build, smoke and test runner for UE 5.6, 5.7 and 5.8 (Windows
PowerShell 5.1 or PowerShell 7). `AGENTS.md` (TESTING COMPILATION) lists every switch; the short form:

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
  links on Mac and Linux) back into the checkout; `Binaries/` and `Intermediate/` stay per host.
- Builds the host's editor target through `Engine/Build/BatchFiles/Build.bat` (UBT), then optionally runs
  `Tests/smoke/smoke_check.py` (`-Smoke`: one headless editor per MCP registration mode, log, config, tool-count and
  probe checks against `Tests/smoke/expected.json` and `Tests/mcp_probe/expected.json`) or the automation tests
  with an exported report (`-Test`).
- Writes timestamped logs to `BuildLogs/` (`-LogRoot` to move them) and exits non-zero on any failure.

Machine-specific settings (engine paths, hosts root) go in `build.local.ps1` next to the script; it is gitignored and
dot-sourced when present.

### BuildLogs Directory

Logs are named `Build_<ver>_<stamp>.log`, `Smoke_<ver>_<stamp>.log` (editor logs `Smoke_<ver>_<stamp>_<mode>.log`),
`Tests_<ver>_<stamp>.log` (with the automation
report in `Tests_<ver>_<stamp>/`) and `Package_<ver>_<stamp>.log`. The directory is git-ignored and accumulates logs
over time.

---

## UE Path Detection Scripts

These scripts automatically detect the UE installation and update `AGENTS.md` with the `UE_SOURCE_PATH` for AI coding agents to reference.

### Running the Detection

**Windows:**
```batch
Content\Scripts\detect-ue-path.bat
```

**macOS/Linux:**
```bash
Content/Scripts/detect-ue-path.sh
```

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

4. **Python script** (`Content/Scripts/update_agents_ue_path.py`):
   - Uses `ue_python_finder.py` from the MCP bridge
   - Updates the `UE_SOURCE_PATH:` line in `AGENTS.md`
   - Prefers newest UE version when multiple are found

### Source Files

| File | Description |
|------|-------------|
| `Content/Scripts/detect-ue-path.sh` | macOS/Linux shell wrapper |
| `Content/Scripts/detect-ue-path.bat` | Windows batch wrapper |
| `Content/Scripts/update_agents_ue_path.py` | Python script that updates AGENTS.md |

---

## Git Submodule Development Workflow

### Setting Up Multi-Version UE Plugin Development with Git Submodules

This workflow allows you to develop the NodeToCode plugin in a single location while testing it across multiple Unreal Engine project versions using git submodules.

### Initial Setup

1. **Initialize Git in UE Test Projects**
   ```bash
   cd "<path-to-your-ue-project>"
   git init
   git add .
   git commit -m "Initial project setup"
   ```

2. **Add Plugin as Git Submodule**
   ```bash
   # Using relative path (recommended for same filesystem)
   git submodule add ../../NodeToCode Plugins/NodeToCode

   # Or using absolute path
   git submodule add "<path-to-nodetocode-repo>" Plugins/NodeToCode
   ```

3. **Handle File Transport Errors**
   If you encounter "fatal: transport 'file' not allowed":
   ```bash
   # Allow file protocol globally
   git config --global protocol.file.allow always

   # Or one-time allow
   git -c protocol.file.allow=always submodule add "/path/to/plugin" Plugins/NodeToCode
   ```

4. **Commit Submodule Addition**
   ```bash
   git add .gitmodules Plugins/NodeToCode
   git commit -m "Add NodeToCode plugin as submodule"
   ```

### Development Workflow

1. **Update Submodule to Latest Changes**
   ```bash
   # Method 1: Update from parent project
   cd "<path-to-your-ue-project>"
   git submodule update --remote --merge Plugins/NodeToCode

   # Method 2: Pull directly in submodule
   cd Plugins/NodeToCode
   git pull origin main  # or your working branch
   ```

2. **Make Changes in Submodule**
   ```bash
   cd Plugins/NodeToCode
   # Edit files, then commit
   git add .
   git commit -m "Your changes"
   git push origin your-branch
   ```

3. **Update Parent Project Reference**
   ```bash
   cd ../..  # Back to UE project root
   git add Plugins/NodeToCode
   git commit -m "Update NodeToCode plugin to latest"
   ```

4. **Track Specific Branch**
   ```bash
   git config -f .gitmodules submodule.Plugins/NodeToCode.branch your-branch-name
   git submodule update --remote
   ```

### Working with Local Branches

When developing on a local branch that hasn't been pushed to GitHub:

1. **Use local file paths** instead of GitHub URLs
2. **Changes are immediately reflected** in all projects using the submodule
3. **No need to push** to remote before testing

### Advantages of Submodule Approach

- Single plugin codebase for all UE versions
- Version control tracks specific plugin commits per project
- Can use different branches/versions per UE version
- Better for team collaboration
- Works seamlessly across machines

---

## IDE Configuration

### Rider Configuration with Submodules

1. **Open the UE test project** (not just the plugin)
2. Rider will recognize the submodule structure
3. **Enable VCS integration** for submodules:
   - `Settings` → `Version Control` → `Git`
   - Check "Update submodules"
4. Changes made in Rider are reflected in the submodule's git repository

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
