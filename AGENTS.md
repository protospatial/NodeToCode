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

`build.ps1` is local-only for now: it is not in the repository (`.gitignore` ignores `*.ps1`) because it holds
machine-specific paths, so a fresh clone has no build script. Where a local copy exists, it takes these switches:

```powershell
.\build.ps1 -UEVersion 5.7           # incremental editor build for one version (5.6, 5.7 or 5.8)
.\build.ps1 -All                     # build every detected version in turn
.\build.ps1 -All -Smoke              # build, then load the plugin in a headless editor
.\build.ps1 -UEVersion 5.8 -Clean    # remove the host's plugin Binaries/Intermediate first
.\build.ps1 -UEVersion 5.6 -Launch   # build, then open the host editor
.\build.ps1 -All -Package            # RunUAT BuildPlugin instead of the incremental host build
.\build.ps1 -WhatIf                  # list detected engines and host projects
```

`-Config` picks the build configuration (default `Development`). Logs go to
`BuildLogs/Build_<version>_<timestamp>.log` (and `Smoke_*` / `Package_*` for those runs). Running it with no
arguments prints the examples and the detected engines.

Automation tests live in `Source/Private/Tests/` and run in the editor with `Automation RunTests NodeToCode`.

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
