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

The build scripts support UE 5.4, 5.5, 5.6, and 5.7 with auto-detection of installations.

**Windows (build.ps1):**
```powershell
.\build.ps1 -UEVersion 5.5       # Build for specific version
.\build.ps1 -All                  # Build all detected versions
.\build.ps1 -WhatIf               # Preview detected UE installations
```

**Mac/Linux (build.sh):**
```bash
./build.sh -v 5.5                 # Build for specific version
./build.sh --all                  # Build all detected versions
./build.sh --whatif               # Preview detected UE installations
```

Logs are saved to `BuildLogs/Build_<version>_<timestamp>.log`. Running without arguments shows help and detected UE versions.

# CRITICAL - END

## Overview

NodeToCode is an Unreal Engine 5 plugin for translating Blueprints into various programming languages (C++, Python, JavaScript, C#, Swift, Pseudocode) using Large Language Models (LLMs). It integrates into the Unreal Editor, providing tools to collect Blueprint nodes, manage LLM interactions, and display generated code with syntax highlighting.

**Key Features:**
*   **Blueprint-to-Code Translation:** Converts selected Blueprint graphs/entire Blueprints.
*   **Multi-LLM Support:** Integrates with OpenAI, Anthropic, Google Gemini, DeepSeek, local Ollama instances, and LM Studio.
*   **Editor Integration:** Adds a "Node to Code" toolbar button in the Blueprint Editor for translation, JSON export, and a dedicated plugin window.
*   **Interactive Code Editor:** Displays generated code with syntax highlighting and theme customization.
*   **Configuration:** Plugin settings for LLM provider selection, API keys (stored securely), target language, and logging.
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
