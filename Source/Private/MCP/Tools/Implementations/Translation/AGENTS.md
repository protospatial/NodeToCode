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
  - `provider`: LLM provider ID
  - `model`: Specific model ID
  - `language`: Target language ID
- **Long-Running**: Yes - requires progress token for SSE streaming
- **Returns**: Translation results via SSE events
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
- **OpenAI**: GPT-4 and GPT-3.5 models
- **Anthropic**: Claude models
- **Google Gemini**: Gemini Pro models
- **DeepSeek**: DeepSeek Coder models

### Local Providers
- **Ollama**: Local model execution
- **LM Studio**: Local model with UI control

## Translation Workflow

### 1. Check Available Providers
```bash
curl -X POST http://localhost:27000/mcp \
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
```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
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

## Async Translation Task

The `translate-focused-blueprint` tool uses the `FN2CTranslateBlueprintAsyncTask` for long-running operations:

- Handles Blueprint serialization on Game Thread
- Performs LLM communication asynchronously
- Reports progress via SSE streaming
- Supports cancellation during execution
- Saves results to configured output directory

## Configuration Notes

### API Keys
- Cloud providers require API keys configured in settings
- Keys are stored as plaintext JSON at `Saved/NodeToCode/User/secrets.json`; the MCP file tools (read-file, read-path) refuse that folder
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