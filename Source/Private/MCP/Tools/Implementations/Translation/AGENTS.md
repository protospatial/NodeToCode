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
  the credential source requests resolve their key from (`IN2CCredentialSource::Get()->Has()`) finds a saved key or
  one in the editor's environment
- **Parameters**: None
- **Returns**: `providers`, `currentProvider` (the settings' provider ID), `configuredProviderCount`. Each provider has
  `id`, `displayName`, `configured` (always true), `isLocal`, `keySource` (`saved`, `environment` with
  `keyVariable` naming the variable, or `none`; never the key), `currentModel` (the model ID the settings select),
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
  file tools read the default location (`Saved/NodeToCode/Translations`) by its project-relative path, and a custom
  directory outside the project by its `absolutePath` (a second read root, see `../FileSystem/AGENTS.md`)

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
  `output_tokens`), plus `warnings` when the model ID is not in the catalog. The same object is the result's
  `structuredContent`, declared by the tool's `outputSchema` (`FN2CTranslateBlueprintCall::BuildOutputSchema`, card
  0207). Over `FN2CTranslateBlueprintCall::MaxInlineChars` (200,000, the tool's `anthropic/maxResultSizeChars`) the
  whole translation is written to `Saved/NodeToCode/Exports/Translations/Translation__<time>__<id>.json` and the result
  is a summary (`graphs` without `code`, `usage`, `warnings`, `savedTo`, `bytes`) plus a `resource_link` to the file
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
  - Every error carries `_meta["nodetocode/error"].code`: `TRANSLATION_<KIND>` from `error_kind` (`RateLimited` ->
    `TRANSLATION_RATE_LIMITED`), `TRANSLATION_FAILED` without one, `INVALID_ARGUMENT` or `CANCELLED`
- A `model` the catalog does not list for a cloud provider is still sent; the result (or the provider's error) then
  carries the `warnings` array
- **Not saved** to the translation output directory: the MCP path (`UN2CLLMModule::ProcessN2CJsonWithOverrides`)
  returns the translation, writing a file only under Saved/NodeToCode/Exports when it is over the result limit (above);
  only translations run from the editor (`UN2CLLMModule::ProcessN2CJson`) are saved there (see "Translation Output
  Structure")
- **Use Case**: Performing actual Blueprint translation

### translate-batch
- **Description**: Translates many graphs in one call, by asset path or by tag, with no Blueprint editor open (card
  0169). The graphs go through `UN2CBatchTranslationOrchestrator` as one batch, one LLM request per graph, one after
  another, every request with the call's provider, model and language
  (`FN2CBatchTranslationOptions::bUseRequestConfig`, `UN2CLLMModule::ProcessN2CJsonWithOverrides`)
- **Parameters** (all optional except the selection):
  - `targets` or `tag`/`category`: the selection export-n2cjson-batch takes (see `Blueprint/Analysis/AGENTS.md`),
    at most 50 graphs per call
  - `language`, `provider`, `model`: as translate-focused-blueprint (unknown IDs are an `INVALID_ARGUMENT` error
    before anything is sent; an unlisted model is sent with a `warnings` entry)
- **Long-Running**: Yes, answered on the same POST as translate-focused-blueprint (card 0107): progress for each
  graph the batch starts ("Translating BP_Door.EventGraph (2 of 3)") and a heartbeat every 0.5 s with the seconds waited; on UE
  5.8 streamed when the client asked for progress, otherwise one held JSON response. `notifications/cancelled`
  cancels the batch: the in-flight HTTP request is cancelled at once, the rest are skipped, and no response is sent
  (the deprecated `nodetocode/cancelTask` gets the cancelled error result). The call cancels the batch after an hour
  (`FN2CTranslateBatchCall::DefaultTimeoutSeconds`). Only one batch runs at a time, from the editor or MCP: a second
  call while one runs is refused with `BATCH_IN_PROGRESS`, and a translation in flight with `LLM_BUSY`
- **Saved**: unlike translate-focused-blueprint, every translation is saved where the editor's batch saves:
  `<translation output directory>/BatchTranslation_<time>/<folder>/` (`<Graph>.h` for C++, `<Graph><ext>`,
  `<Graph>_Notes.txt`, `<Graph>.json` with the N2C JSON sent) plus `BatchSummary.json`, and recorded for the
  translation viewer (`UN2CGraphStateManager::RecordSavedTranslation` with the call's language, provider and model,
  so `LoadTranslation` reads it back). `<folder>` is the graph's file stem, or `<Blueprint>__<Graph>` (then `_2`,
  ...) when another graph of the batch already has that stem, so two Blueprints' EventGraphs keep their own files
- **Returns** (at least one graph translated): JSON whose first key is `untrusted_content`, then `provider`, `model`,
  `language`, `batchOutputPath`, `summaryPath`, `total`, `succeeded`, `failed`, `skipped`, `durationSeconds`,
  `usage`, `items` (each `blueprintPath`, `graphName`, `graphGuid`, `kind`, `tag` for a tag filter, `status`
  (`completed`, `failed`, `skipped`); a completed one `outputDirectory`, `files`, `graphs` (`graph_name`,
  `graph_type`, `graph_class`, `code` as translate-focused-blueprint returns them) and `usage`; a failed one `error`,
  the typed error object (`error_kind`, `http_status`, `retryable`, `message`, ...); a skipped one `reason`),
  `failures` (targets that resolved to nothing, with codes as export-n2cjson-batch reports them, including
  `UNSUPPORTED_ASSET_TYPE` for a non-Blueprint asset), `skippedGraphs` (empty graphs), `warnings` and `codeInline`.
  When the result with the code would be over 20000 estimated tokens, `graphs` is left out of every item,
  `codeInline` is false and `codeOmitted` says to read the files
- **Errors** (`isError: true`): `INVALID_ARGUMENT`, `NO_TAGGED_GRAPHS`, `TOO_MANY_GRAPHS` as plain text; no graph
  left to translate (`NO_VALID_ITEMS`), `BATCH_IN_PROGRESS`, `LLM_BUSY` as JSON `{error, code, failures}`; a batch in
  which no graph was translated returns its full result JSON as the error; cancelled or timed out: plain text
- **Use Case**: translating a set of graphs (a tag, a folder's Blueprints) without opening them

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
- Keys are stored by `FN2CSecretStore` in Windows Credential Manager (`NodeToCode/<ProviderId>`; plaintext JSON in the per-user `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json` on Mac and Linux), outside the project; no tool returns a key; the MCP file tools (read-file, read-path) refuse that folder, the old project folder `Saved/NodeToCode/User`, and any `secrets.json*` file
- Every request resolves its key when it is sent, from `IN2CCredentialSource::Get()` (the saved key, else the
  environment variable), so a changed key applies to the next request and no config holds a key
- `get-available-llm-providers` reports a cloud provider as configured when that source `Has()` a key, and where it
  comes from in `keySource`
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

Each is an `isError` result; see `translate-focused-blueprint` above for its text (`error_kind` in the error JSON),
and `translate-batch` for its per-graph errors.