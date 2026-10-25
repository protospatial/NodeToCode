# Translation Tools

This directory contains MCP tools for managing Blueprint-to-code translation functionality. These tools provide information about available translation targets, LLM providers, and handle the translation process itself.

## Available Tools

### get-available-translation-targets
- **Description**: Returns the list of programming languages NodeToCode can translate to
- **Parameters**: None
- **Returns**: Language list with metadata and current selection
- **Use Case**: Discovering supported output languages

### get-available-llm-providers
- **Description**: Returns configured LLM providers available for translation
- **Parameters**: None
- **Returns**: Provider list with models and configuration status
- **Use Case**: Understanding which AI providers are available

### get-translation-output-directory
- **Description**: Returns the translation output directory configuration
- **Parameters**: None
- **Returns**: Directory paths and configuration status
- **Use Case**: Locating where translations are saved

### translate-focused-blueprint
- **Description**: Translates the currently focused Blueprint using an LLM provider
- **Parameters** (all optional):
  - `provider`: LLM provider ID as `get-available-llm-providers` lists it (case-insensitive)
  - `model`: Specific model ID
  - `language`: Target language ID as `get-available-translation-targets` lists it (case-insensitive)
- **Errors**: an unknown `provider` or `language` returns `isError` naming the valid IDs, without translating. A
  `model` the registry does not list for a cloud provider is still sent; the result JSON then has a `warnings` array.
- **Long-Running**: Yes. Answered on the same POST: on UE 5.8 with `Accept: text/event-stream` and a
  `_meta.progressToken`, as a stream of `notifications/progress` then the result; otherwise as one JSON response when
  the translation finishes
- **Returns**: A CallToolResult whose text is the translation JSON (`isError` with the reason on failure)
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
- **OpenAI**, **Anthropic**, **Google Gemini**, **DeepSeek**: the models in the plugin's model registry
  (`get-available-llm-providers` lists each provider's models)

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

Translations are saved in:
```
ProjectSaved/NodeToCode/Translations/
└── BlueprintName_YYYY-MM-DD-HH.MM.SS/
    ├── EventGraph.cpp (or .py, .js, etc.)
    ├── Function1.cpp
    ├── Function2.cpp
    └── translation_metadata.json
```

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
- Each provider has a default model configured
- Models can be overridden per translation
- Some models support system prompts, others don't

### Output Directory
- Default: `ProjectSaved/NodeToCode/Translations`
- Can be customized in plugin settings
- Directory is created automatically if missing

## Error Handling

Common error scenarios:
- No Blueprint currently focused
- Invalid or missing API keys
- LLM provider communication failures
- Insufficient tokens for large Blueprints
- Timeout during translation

All errors are reported with actionable messages and error codes.