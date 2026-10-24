# NodeToCode Development Workflow

Build scripts, UE path detection, git submodule setup, and IDE configuration for multi-version UE plugin development.

## Build Scripts

The plugin includes platform-specific build scripts that compile the UE project and output timestamped logs.

### Windows: `build.ps1`

Located in the plugin root directory. Run from PowerShell:

```powershell
.\build.ps1
```

**What it does:**
- Invokes `Engine\Build\BatchFiles\Build.bat` via UBT (Unreal Build Tool)
- Builds target: `NodeToCodeHost_5_4Editor` (Development, Win64)
- Creates timestamped logs in `BuildLogs/Build_YYYYMMDD_HHMMSS.log`
- Displays colored output (success/warning/error)
- Shows last 10 error lines on failure

**Configuration variables** (edit in script if paths differ):
- `$ProjectDir` - Path to UE project
- `$UEPath` - Path to UE installation

### macOS/Linux: `build.sh`

Located in the plugin root directory. Run from terminal:

```bash
./build.sh
```

**What it does:**
- Invokes `Engine/Build/BatchFiles/Mac/Build.sh` via UBT
- Builds target: `NodeToCodeHost_5_4Editor` (Development, Mac)
- Creates timestamped logs in `BuildLogs/Build_YYYYMMDD_HHMMSS.log`
- Uses `tee` to display output while logging
- Shows warning count and error lines on failure

**Configuration variables** (edit in script if paths differ):
- `PROJECT_DIR` - Path to UE project
- `UE_PATH` - Path to UE installation

### BuildLogs Directory

Build logs are stored in `Plugins/NodeToCode/BuildLogs/` with naming format:
```
Build_YYYYMMDD_HHMMSS.log
```

This directory is git-ignored and accumulates logs over time. Logs include full compiler output for debugging build failures.

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

**Ignored (local-only):**
- `CLAUDE.md` - Local AI agent instructions (personal)
- `.claude/` - Claude Code conversation data
- `Development/` - Local development planning/notes
- `BuildLogs/` - Build output logs
- `build.sh`, `*.ps1` - Build scripts (paths are machine-specific)
- `.aider*` - Aider AI assistant files
- `.mcp.json` - MCP server configuration
- `.env` - Environment variables
- `Config/` - Local UE configuration

### Rationale

- **Build scripts ignored**: They contain machine-specific paths (`$UEPath`, `$ProjectDir`)
- **CLAUDE.md ignored**: Allows personal customization without polluting shared repo
- **AGENTS.md tracked**: Shared AI agent instructions that should be consistent
