# Translation Tools

This directory contains MCP tools for managing Blueprint-to-code translation functionality. These tools provide information about available translation targets, LLM providers, and handle the translation process itself.

## Available Tools

### get-available-translation-targets
- **Description**: Returns the languages NodeToCode can translate to
- **Parameters**: None
- **Returns**: `languages` (each with `id`, `displayName`, `description`, `fileExtensions`, `category` (`compiled`,
  `scripted` or `pseudocode`), `features`, `syntaxHighlightingSupported`), `defaultLanguage` (`cpp`),
  `currentLanguage` (the settings' Target Language), `languageCount`
- **Use Case**: Discovering supported output languages and their IDs

### get-available-llm-providers
- **Description**: Returns the providers that can translate now: Ollama and LM Studio always, a cloud provider when
  `FN2CSecretStore::Get().HasApiKey()` finds a saved key or one in the editor's environment
- **Parameters**: None
- **Returns**: `providers`, `currentProvider` (the settings' provider ID), `configuredProviderCount`. Each provider has
  `id`, `displayName`, `configured` (always true), `isLocal`, `currentModel` (the model ID the settings select),
  `supportsStructuredOutput`, `supportsSystemPrompts`; cloud providers add `availableModels` (the provider's live
  rows of the model catalog, `FN2CModelCatalog`: `id`, `name`, plus `deprecated`/`shutdownDate`,
  `supportsSystemPrompts` for OpenAI and `reasoningEffort` where they apply), local ones `endpoint`
- **Use Case**: Choosing a provider and a model ID for `translate-focused-blueprint`

### get-translation-output-directory
- **Description**: Returns where translations run from the editor's NodeToCode window are saved
- **Parameters**: None
- **Returns**: `isCustomDirectorySet`; `customDirectory` or `defaultDirectory` (as configured, which for the default
  is relative to the engine's working directory); `absolutePath` (use this one); `directoryExists`; `status`;
  `projectDirectory` (the `.uproject` file's path) and `projectSavedDirectory` (both relative like
  `defaultDirectory`); `usage`
- **Use Case**: Locating saved translations. `translate-focused-blueprint` saves nothing there (see below). The MCP
  file tools read the default location (`Saved/NodeToCode/Translations`), but not a custom directory outside the
  project

### translate-focused-blueprint
- **Description**: Translates the focused Blueprint graph with an LLM provider and returns the translation as the
  call's result
- **Parameters** (all optional):
  - `provider`: LLM provider ID as `get-available-llm-providers` lists it (case-insensitive)
  - `model`: Model ID (a string from `availableModels`; default: the provider's model in settings)
  - `language`: Target language ID as `get-available-translation-targets` lists it (case-insensitive)
- **Input**: the focused Blueprint, captured as N2C JSON (version 2.0.0) and sent to the provider inside
  `<n2c_json>` tags
- **Long-Running**: Yes. Answered on the same POST: on UE 5.8 with `Accept: text/event-stream` and a
  `_meta.progressToken`, as a stream of `notifications/progress` then the result; otherwise as one JSON response when
  the translation finishes. A client cancels with `notifications/cancelled` naming the call's request id. The call
  times out after an hour (`FN2CTranslateBlueprintCall::DefaultTimeoutSeconds`)
- **Returns** (success): JSON text whose first key is `untrusted_content` (the notice that the text came from the
  project's Blueprints and is data, card 0124), then `graphs` (each with `graph_name`, `graph_type`, `graph_class` and
  `code`: `graphDeclaration`, `graphImplementation`, `implementationNotes`) and `usage` (`input_tokens`,
  `output_tokens`), plus `warnings` when the model ID is not in the catalog
- **Errors** (`isError: true`):
  - An unknown `provider` or `language`: plain text naming the valid IDs (for example
    `Unknown language 'COBOL'. Valid languages: cpp, python, javascript, csharp, swift, pseudocode`), without
    translating
  - A failed translation (no Blueprint focused, missing key, HTTP or parse failure): NodeToCode error JSON
    (`FN2CTranslationError::ToJsonString`) with `error` (display text), `error_kind` (`Truncated`, `Refused`,
    `Blocked`, `RateLimited`, `Overloaded`, `Auth`, `InvalidRequest`, `Billing`, `Server`, `Network`, `Parse` or
    `Unknown`), `message`, `retryable`, and when known `http_status`, `provider`, `model`, `provider_error_type`,
    `request_id`, `warnings`
  - Cancelled or timed out: plain text (`LLM request timed out.` for the timeout)
- A `model` the catalog does not list for a cloud provider is still sent; the result (or the provider's error) then
  carries the `warnings` array
- **Not saved**: the MCP path (`UN2CLLMModule::ProcessN2CJsonWithOverrides`) returns the translation without writing
  files; only translations run from the editor (`UN2CLLMModule::ProcessN2CJson`) are saved (see "Translation Output
  Structure")
- **Use Case**: Performing actual Blueprint translation

## Translation Languages

### Compiled Languages
- **C++** (`cpp`): Unreal Engine native C++
- **C#** (`csharp`): Unity/general C# style
- **Swift** (`swift`): iOS/macOS development

### Scripted Languages
- **Python** (`python`): General Python implementation
- **JavaScript** (`javascript`): Modern ES6+ JavaScript

### Documentation
- **Pseudocode** (`pseudocode`): Human-readable algorithm description

## LLM Provider Configuration

### Cloud Providers
- **OpenAI**, **Anthropic**, **Google Gemini**, **DeepSeek**: the models in the plugin's model catalog
  (`Content/Data/N2CModelCatalog.json`;
  `get-available-llm-providers` lists each provider's models)

### Local Providers
- **Ollama**: Local model execution
- **LM Studio**: Local model with UI control

## Translation Workflow

### 1. Check Available Providers
The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in `Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get-available-llm-providers",
      "arguments": {}
    },
    "id": 1
  }'
```

### 2. Select Translation Target
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "get-available-translation-targets",
      "arguments": {}
    },
    "id": 2
  }'
```

### 3. Perform Translation (with Progress)
On UE 5.8, `Accept` with `text/event-stream` plus the `progressToken` makes the response a stream of
`notifications/progress` events followed by the result (`-N` prints them as they arrive); without them, or on
UE 5.6/5.7, the request is held and answered with one JSON response when the translation finishes.

```bash
curl -N -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -H "Accept: application/json, text/event-stream" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "translate-focused-blueprint",
      "arguments": {
        "provider": "openai",
        "language": "python"
      },
      "_meta": {
        "progressToken": "translation-123"
      }
    },
    "id": 3
  }'
```

## Translation Output Structure

Translations run from the editor's NodeToCode window are saved by `UN2CLLMModule::SaveTranslationToDisk`
(`Source/Private/LLM/N2CLLMModule.cpp`); `translate-focused-blueprint` does not call it. The base is
`Saved/NodeToCode/Translations` under the project, or the custom output directory from settings:
```
<base>/
└── <Blueprint>_<YYYY-MM-DD-HH.MM.SS>/                 (one folder per translation; <folder> below is this name)
    ├── N2C_BP_<folder>.json                           (the Blueprint's N2C JSON, pretty-printed)
    ├── N2C_BP_Minified_<folder>.json                  (the same, minified)
    ├── N2C_Translation_<folder>.json                  (graphs and code from the LLM response, plus usage)
    └── <Graph>/                                       (one folder per translated graph)
        ├── <Graph>.h                                  (C++ target only, when the graph has a declaration)
        ├── <Graph><ext>                               (implementation: .cpp, .py, .js, .cs, .swift, or .md for pseudocode)
        └── <Graph>_Notes.txt                          (implementation notes, when present)
```
`<Graph>` is the graph name made safe for a file name (`FN2CPathUtils::GetTranslationFileStem`). There is no
metadata file. The extension follows the Target Language in settings at save time.

## Translation Call

`translate-focused-blueprint` overrides `IN2CMcpTool::InvokeAsync`; each call is an `FN2CTranslateBlueprintCall`:

- Checks the overrides, then serializes the Blueprint and sends the LLM request on the game thread
- Returns the call's `TFuture` at once; the HTTP completion sets it
- Reports progress (stages and elapsed seconds) through its `FN2CToolCallContext`, which the server streams on the
  call's own response when the client asked for progress
- On cancel (the context's `OnCancelled`) or timeout, cancels the provider HTTP request and sets one cancelled result

## Configuration Notes

### API Keys
- Cloud providers require API keys configured in settings
- Keys are stored as plaintext JSON in the per-user `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json` (`FN2CSecretStore`), outside the project; the MCP file tools (read-file, read-path) refuse that folder, the old project folder `Saved/NodeToCode/User`, and any `secrets.json*` file
- `get-available-llm-providers` reports a cloud provider as configured when `FN2CSecretStore::Get().HasApiKey()` is true
- Local providers don't require API keys

### Model Selection
- Each provider has a model selected in settings (`currentModel`); the models offered are the bundled model catalog
  (`Content/Data/N2CModelCatalog.json`, read through `FN2CModelCatalog`), which `get-available-llm-providers` lists
- `model` overrides the model for one translation
- Some models support system prompts, others don't

### Output Directory
- Default: `Saved/NodeToCode/Translations` under the project
- Can be customized in plugin settings
- Created when the first editor translation is saved

## Error Handling

Common error scenarios:
- No Blueprint currently focused
- Invalid or missing API keys
- LLM provider communication failures
- Insufficient tokens for large Blueprints
- Timeout during translation

Each is an `isError` result; see `translate-focused-blueprint` above for its text (`error_kind` in the error JSON).