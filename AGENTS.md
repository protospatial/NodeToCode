# AGENTS.md

This file provides guidance to AI coding agents when working with code in this repository.

# NodeToCode Unreal Engine 5 Plugin

# CRITICAL - START

## Understanding NodeToCode's MCP Server Tools

@Source/Private/MCP/Tools/AGENTS.md

## Unreal Engine Source Path

Run `Content/Scripts/detect-ue-path.sh` (Mac/Linux) or `Content\Scripts\detect-ue-path.bat` (Windows) to detect your UE installation and update this file.

UE_SOURCE_PATH: <!-- RUN DETECT SCRIPT TO POPULATE -->

This is where source files for Unreal Engine 5 can be found and searched through for research purposes. NEVER try to modify files here.

## User docs

- **User docs move with the code.** A change that alters anything a plugin user or integrator sees or depends on
  updates the affected pages in `wiki/` (and, once the capture harness exists, retakes the affected screenshots
  through it, never by hand) in the same change, and the commit or PR body says `Docs: <pages>` or
  `Docs: none, <why>`. The writer contract is `.agents/contracts/docs.md`; `tools/docs_check.py --changed` checks it.

## TESTING COMPILATION

NodeToCode 2.0 supports UE 5.6 (the floor) through UE 5.8 (the ceiling). Any C++ change must build on 5.6, 5.7
and 5.8. Guard version-specific code with `ENGINE_MINOR_VERSION` (include `Runtime/Launch/Resources/Version.h`).

`build.ps1` at the repo root (tracked) builds, smoke-tests and runs the automation tests. Each engine gets a small
C++ host project, `<HostsRoot>/NodeToCodeHost_5_X` (default `..\Hosts`, next to the checkout), which the script
creates on first use. The host's `Plugins/NodeToCode/{Source,Content,Resources,ThirdParty}` are junctions (symbolic
links on Mac and Linux) back into this checkout, and `NodeToCode.uplugin` is copied in on every run, so there is one
copy of the source while `Binaries/` and `Intermediate/` stay per host and every engine builds incrementally.

```powershell
.\build.ps1 -UEVersion 5.7           # incremental editor build for one version (5.6, 5.7 or 5.8)
.\build.ps1 -All                     # build every detected version in turn
.\build.ps1 -All -Smoke              # build, then the headless smoke check and MCP probes (Tests/smoke/smoke_check.py)
.\build.ps1 -All -Test               # build, then run Automation RunTests NodeToCode headless and report pass/fail
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
  `-TestFilter` narrows the run and `-TestTimeoutMinutes` (default 60) stops a hung editor. If `-ExecCmds` ever holds
  more than one command, separate them with commas: a semicolon hangs the editor.
- `-Smoke` runs `Tests/smoke/smoke_check.py` with the engine's own Python: one headless `-N2CMcp` editor per MCP
  tool-registration mode (default, dynamic discovery, Python script-only, set in the host's
  `Config/DefaultNodeToCode.ini` and put back), plus the `Tests/mcp_probe` probes in the default session. It fails on
  an error, ensure, assertion or crash line in the editor log (the allowlist in the script names each benign
  exception and why), a missing MCP start line or `/mcp/health` answer from that editor, a `tools/list` count other
  than `Tests/smoke/expected.json`, any change to the host's `Config/*.ini` during a session, a failed
  `get_focused_blueprint_json` on an engine Blueprint, or a probe status other than `Tests/mcp_probe/expected.json`.
  A card that adds or removes an MCP tool updates `Tests/smoke/expected.json`; failures already present are listed
  there under `known_failures` with their finding ids. About 1 minute per engine.
- Logs: `BuildLogs/Build_<version>_<timestamp>.log`, `Smoke_*.log`, `Tests_*.log` (report in `Tests_*/`) and
  `Package_*.log`. The script exits non-zero when any build, smoke check or test run fails.
- If a host editor is open, that version is skipped (Live Coding blocks UBT): use Ctrl+Alt+F11 in the editor, or
  close it.

Automation tests live in `Source/Private/Tests/`. Live MCP probes and the headless editor helper live in
`Tests/mcp_probe/`.

Release packages come from `Scripts/package-release.ps1` (tracked), never from `build.ps1 -Package`. It exports a
commit with `git archive` (so nothing uncommitted or ignored can ship), runs RunUAT BuildPlugin for UE 5.6, 5.7 and
5.8, writes `NodeToCode-<VersionName>-UE<ver>.zip` plus a SHA256SUMS file to `..\Packages`, verifies each zip
(required and forbidden files, `.uplugin` fields, no secrets), rebuilds it from source in a scratch C++ host and
loads it in a headless editor to check the MCP server answers. It never tags or uploads.

```powershell
.\Scripts\package-release.ps1                              # HEAD, all three engines
.\Scripts\package-release.ps1 -Ref <tag-or-sha> -UEVersion 5.8
```

# CRITICAL - END

## Overview

NodeToCode is an Unreal Engine 5 plugin for translating Blueprints into various programming languages (C++, Python, JavaScript, C#, Swift, Pseudocode) using Large Language Models (LLMs). It integrates into the Unreal Editor, providing tools to collect Blueprint nodes, manage LLM interactions, and display generated code with syntax highlighting.

**Key Features:**
*   **Blueprint-to-Code Translation:** Converts selected Blueprint graphs/entire Blueprints.
*   **Multi-LLM Support:** Integrates with OpenAI, Anthropic, Google Gemini, DeepSeek, local Ollama instances, and LM Studio.
*   **Editor Integration:** Adds a "Node to Code" toolbar button in the Blueprint Editor for translation, JSON export, and a dedicated plugin window.
*   **Interactive Code Editor:** Displays generated code with syntax highlighting and theme customization.
*   **Configuration:** Plugin settings for LLM provider selection, API keys (entered in Project Settings, not written to config `.ini` files; `FN2CSecretStore` keeps them as plain, unencrypted JSON in the per-user `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json`, outside the project; a project's old `Saved/NodeToCode/User/secrets.json` is moved there once; when no key is saved, `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY` or `DEEPSEEK_API_KEY`/`DEEPSEEK_API` from the editor's environment is used, never saved or shown), target language, and logging.
*   **Reasoning Model Support:** Prepended model commands for controlling reasoning models and feature toggles.

## Context Navigation

For detailed information on specific areas of the codebase, navigate to the relevant context file:

```
Context/
├── architecture/
│   └── AGENTS-architecture.md          → Core plugin setup, editor integration, settings
│
├── translation-pipeline/
│   └── AGENTS-translation-pipeline.md  → Blueprint node → code conversion, batch translation
│
├── llm-providers/
│   └── AGENTS-llm-providers.md         → LLM services, adding/updating providers
│
├── mcp-server/
│   └── AGENTS-mcp-server.md            → MCP server implementation, tools, resources
│
├── python-scripting/
│   └── AGENTS-python-scripting.md      → Python bridge, script management system
│
├── code-editor/
│   └── AGENTS-code-editor.md           → Syntax highlighting, themes, code display
│
├── development-workflow/
│   └── AGENTS-development-workflow.md  → Git submodules, IDE setup, build process
```

### Quick Reference

| Task | Context File |
|------|--------------|
| Plugin initialization, settings | `Context/architecture/` |
| Node collection, translation | `Context/translation-pipeline/` |
| Adding LLM providers/models | `Context/llm-providers/` |
| MCP tools development | `Context/mcp-server/` + `@Source/Private/MCP/Tools/AGENTS.md` |
| Python automation | `Context/python-scripting/` |
| Code editor styling | `Context/code-editor/` |
| Git workflow, IDE config | `Context/development-workflow/` |
