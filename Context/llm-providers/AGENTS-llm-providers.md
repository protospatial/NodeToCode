# NodeToCode LLM Providers

LLM integration architecture and guide for adding/updating providers.

## Directory Structure

```
Source/
├── Public/LLM/                           # Header files
│   ├── N2CLLMModule.h                    # Main LLM orchestration
│   ├── IN2CLLMService.h                  # Service interface
│   ├── N2CBaseLLMService.h               # Base service class
│   ├── N2CLLMProviderRegistry.h          # Provider factory
│   ├── N2CLLMPayloadBuilder.h            # Request payload builder
│   ├── N2CResponseParserBase.h           # Response parser base
│   ├── N2CHttpHandlerBase.h              # HTTP handler base
│   ├── N2CHttpHandler.h                  # HTTP handler concrete
│   ├── N2CSystemPromptManager.h          # Prompt management
│   ├── N2CLLMTypes.h                     # Enums, delegates, config struct
│   ├── N2CLLMModels.h                    # Model enums and utilities
│   ├── N2CLLMPricing.h                   # Pricing structs
│   ├── N2COllamaConfig.h                 # Ollama config struct
│   ├── N2CBatchTranslationOrchestrator.h # Batch translation
│   └── Providers/                        # Provider-specific headers
│       ├── N2COpenAIService.h
│       ├── N2COpenAIResponseParser.h
│       ├── N2CAnthropicService.h
│       ├── N2CAnthropicResponseParser.h
│       ├── N2CGeminiService.h
│       ├── N2CGeminiResponseParser.h
│       ├── N2CDeepSeekService.h
│       ├── N2CDeepSeekResponseParser.h
│       ├── N2COllamaService.h
│       ├── N2COllamaResponseParser.h
│       ├── N2CLMStudioService.h
│       └── N2CLMStudioResponseParser.h
│
└── Private/LLM/                          # Implementation files
    ├── N2CLLMModule.cpp
    ├── N2CBaseLLMService.cpp
    ├── N2CLLMProviderRegistry.cpp
    ├── N2CLLMPayloadBuilder.cpp
    ├── N2CResponseParserBase.cpp
    ├── N2CHttpHandlerBase.cpp
    ├── N2CSystemPromptManager.cpp
    ├── N2CLLMModels.cpp
    ├── N2CBatchTranslationOrchestrator.cpp
    └── Providers/                        # Provider implementations
        ├── N2COpenAIService.cpp
        ├── N2COpenAIResponseParser.cpp
        ├── N2CAnthropicService.cpp
        ├── N2CAnthropicResponseParser.cpp
        ├── N2CGeminiService.cpp
        ├── N2CGeminiResponseParser.cpp
        ├── N2CDeepSeekService.cpp
        ├── N2CDeepSeekResponseParser.cpp
        ├── N2COllamaService.cpp
        ├── N2COllamaResponseParser.cpp
        ├── N2CLMStudioService.cpp
        └── N2CLMStudioResponseParser.cpp
```

## LLM Integration & Providers

Manages interactions with LLMs.

*   **`N2CLLMModule.h/.cpp` (`UN2CLLMModule`)**: Singleton orchestrating LLM requests. Initializes based on `UN2CSettings`, manages the active `IN2CLLMService`, processes JSON input via `ProcessN2CJson()` and `ProcessN2CJsonWithOverrides()`, uses `UN2CSystemPromptManager` for prompts, and saves translations. Provides Blueprint-accessible delegates (`OnTranslationResponseReceived`, `OnTranslationRequestSent`) and status tracking.
*   **`IN2CLLMService.h`**: Interface for LLM service providers defining: `Initialize()`, `SendRequest()`, `GetConfiguration()`, `GetProviderType()`, `IsInitialized()`, `GetProviderHeaders()`, `GetResponseParser()`.
*   **`N2CBaseLLMService.h/.cpp` (`UN2CBaseLLMService`)**: Abstract base class for LLM services, providing common initialization and request flow. Derived classes implement: `CreateResponseParser()`, `GetConfiguration()`, `GetProviderHeaders()`, `FormatRequestPayload()`, `GetDefaultEndpoint()`.
*   **Provider Implementations (`Source/Public/LLM/Providers/`):**
    *   Service classes inheriting `UN2CBaseLLMService`:
        *   `UN2COpenAIService` - OpenAI Chat Completions API
        *   `UN2CAnthropicService` - Anthropic Claude API (with OAuth support)
        *   `UN2CGeminiService` - Google Gemini API (with OAuth support)
        *   `UN2CDeepSeekService` - DeepSeek API
        *   `UN2COllamaService` - Local Ollama API (with custom `FN2COllamaConfig`)
        *   `UN2CLMStudioService` - Local LM Studio API (OpenAI-compatible)
    *   Each implements provider-specific API endpoint, auth, headers, and payload formatting.
*   **`N2CLLMProviderRegistry.h/.cpp` (`UN2CLLMProviderRegistry`)**: Singleton factory for creating LLM service instances. Methods: `RegisterProvider()`, `CreateProvider()`, `IsProviderRegistered()`, `GetRegisteredProviders()`.
*   **`N2CLLMPayloadBuilder.h/.cpp` (`UN2CLLMPayloadBuilder`)**: Utility for building provider-specific JSON request payloads. Key methods:
    *   `Initialize()`, `SetModel()`, `SetTemperature()`, `SetMaxTokens()`
    *   `AddSystemMessage()`, `AddUserMessage()`
    *   `AddAnthropicOAuthSystemMessages()` - OAuth-specific for Anthropic
    *   `SetJsonResponseFormat()` / `SetStructuredOutput()` - Schema-based output
    *   `ConfigureForOpenAI()`, `ConfigureForAnthropic()`, `ConfigureForGemini()`, `ConfigureForDeepSeek()`, `ConfigureForOllama()`, `ConfigureForLMStudio()`
    *   `GetN2CResponseSchema()` - Static method for N2C translation format schema
*   **`N2CResponseParserBase.h/.cpp` (`UN2CResponseParserBase`)**: Abstract base for LLM response parsers. Handles common N2C JSON structure parsing, error handling (`HandleCommonErrorResponse()`), content extraction (`ExtractStandardMessageContent()`), graph/code data extraction, and JSON code block marker processing.
*   **Parser Implementations (`Source/Public/LLM/Providers/`)**:
    *   `UN2COpenAIResponseParser`, `UN2CAnthropicResponseParser`, `UN2CGeminiResponseParser`, `UN2CDeepSeekResponseParser`, `UN2COllamaResponseParser`, `UN2CLMStudioResponseParser`
    *   Handle provider-specific JSON response structures, error formats, extract core message content and token usage.
*   **`N2CHttpHandlerBase.h/.cpp` & `N2CHttpHandler.h`**: `UN2CHttpHandlerBase` (abstract) and `UN2CHttpHandler` (concrete) for sending HTTP POST requests to LLM APIs. Features request timeout configuration and extra headers support.
*   **`N2CSystemPromptManager.h/.cpp` (`UN2CSystemPromptManager`)**: Loads and manages system prompts from `.md` files in `Content/Prompting/`. Methods: `GetSystemPrompt()`, `GetLanguageSpecificPrompt()`, `MergePrompts()`, `PrependSourceFilesToUserMessage()`.
*   **`Content/Prompting/CodeGen_[Language].md`**: Markdown files with detailed LLM instructions for each target language:
    *   `CodeGen_CPP.md`, `CodeGen_Python.md`, `CodeGen_JavaScript.md`
    *   `CodeGen_CSharp.md`, `CodeGen_Swift.md`, `CodeGen_Pseudocode.md`
*   **`N2CLLMTypes.h`**: Defines `EN2CLLMProvider` (OpenAI, Anthropic, Gemini, DeepSeek, Ollama, LMStudio), `EN2CSystemStatus`, `FN2CLLMConfig` struct, and LLM-related delegates (`FOnLLMResponseReceived`, `FOnTranslationResponseReceived`, `FOnTranslationRequestSent`).
*   **`N2CLLMModels.h/.cpp`**: Provider-specific model enums and `FN2CLLMModelUtils`:
    *   `EN2COpenAIModel`: GPT-6.1 Sol (default), GPT-6 Astra, GPT-6 Luna, GPT-5.2, GPT-5.2 Pro; deprecated but live: GPT-5/Mini/Nano and o3 (shut down 2026-12-11), GPT-4.1, GPT-4o, GPT-4o Mini (2027-01-14); retired (hidden): o4-mini, o3-mini, o1, o1-preview, o1-mini
    *   `EN2CAnthropicModel`: Claude Sonnet 5.5 (default), Opus 5.5, Haiku 5.5, Opus 4.5; deprecated but live: Sonnet 4.5 (retires 2026-11-30); retired (hidden): Opus 4.1, Opus 4, Sonnet 4, 3.7 Sonnet, 3.5 Sonnet, 3.5 Haiku
    *   `EN2CGeminiModel`: Gemini 3.8 Flash (default), Gemini 4 Argon, Gemini 3.5 Flash-Lite, Gemini 3 Flash Preview; retired (hidden): 3 Pro Preview, 2.5 Pro/Flash previews, 2.0 Flash/Flash-Lite, 1.5 Flash/Pro, 2.0 experimental models
    *   `EN2CDeepSeekModel`: DeepSeek R1, DeepSeek V3
    *   Defaults live in `N2CDefaultModels` (same header). Enumerators are never renamed or removed, because settings save them by name: new models are appended, retired ones get `UMETA(Hidden)` plus `.Retired(date)` in the registry, deprecated-but-live ones get `.ShutsDown(date)` and the date in their display name. `UN2CSettings::PostInitProperties` moves a saved retired selection to the default and logs one warning. `NodeToCode.LLM.Registry` checks these rules.
    *   Note: Ollama and LM Studio use dynamic string-based model names instead of enums
    *   `FN2CLLMModelUtils`: Model string ID getters, pricing getters, system prompt support checks
*   **`N2CLLMPricing.h`**: Pricing structs per provider - `FN2COpenAIPricing`, `FN2CAnthropicPricing`, `FN2CGeminiPricing`, `FN2CDeepSeekPricing`. Each has `InputCost` and `OutputCost` (per 1M tokens).
*   **`N2COllamaConfig.h`**: `FN2COllamaConfig` USTRUCT with extensive Ollama-specific settings:
    *   General: `OllamaEndpoint`, `bUseSystemPrompts`, `PrependedModelCommand`, `KeepAlive`
    *   Generation: `Temperature`, `NumPredict` (max tokens), `TopP`, `TopK`, `MinP`, `RepeatPenalty`
    *   Context: `NumCtx` (context window size)
    *   Advanced: `Mirostat`, `MirostatEta`, `MirostatTau`, `Seed`

## OAuth Authentication Support

Anthropic and Gemini services support OAuth authentication in addition to API keys:

*   **`UN2CAnthropicService::IsUsingOAuth()`**: Returns true if using OAuth authentication
*   **`UN2CGeminiService::IsUsingOAuth()`**: Returns true if using Google OAuth
*   **OAuth Payload Handling**: `UN2CLLMPayloadBuilder::AddAnthropicOAuthSystemMessages()` handles OAuth-specific system message formatting with content blocks
*   **OAuth Token Storage**: Handled by `UN2CUserSecrets` (see `Context/architecture/AGENTS-architecture.md`)

## Structured Output Support

Comprehensive structured output support across providers:

*   **OpenAI**: `json_schema` type with strict schema validation
*   **LM Studio**: OpenAI-compatible with `strict: true` parameter
*   **Gemini**: `responseMimeType` and `responseSchema` in generation config
*   **DeepSeek**: `json_object` type format
*   **Ollama**: Direct schema in `format` field
*   **Schema Definition**: `GetN2CResponseSchema()` provides consistent N2C translation format

## Prepended Model Commands

Support for model-specific command prefixes for local providers:

*   **Ollama Configuration** (`N2COllamaConfig.h`):
    *   `PrependedModelCommand`: Text prepended to user messages
    *   Use cases: `/no_think`, `/think`, reasoning control
*   **LM Studio Configuration** (`N2CSettings.h`):
    *   `LMStudioPrependedModelCommand`: Similar functionality for LM Studio
*   **Implementation**: Commands are prepended with `\n\n` separator before N2C JSON payload in both services.

## System Prompt Support by Model

Some models have limited or no system prompt support:

*   **OpenAI o1 models** (`o1`, `o1-preview`, `o1-mini`): No system prompt support (merged into user message)
*   **OpenAI o3/o4 and later**: Support system/developer prompts
*   **All Anthropic models**: Full system prompt support
*   **Gemini models**: Full system prompt support
*   **Local models (Ollama/LM Studio)**: Configurable via settings

---

## Adding/Updating LLM Providers/Models (Guide)

1.  **Define Core Types (`N2CLLMTypes.h`, `N2CLLMModels.h/.cpp`, `N2CLLMPricing.h`):**
    *   Add to `EN2CLLMProvider` enum (if new provider). Current providers: OpenAI, Anthropic, Gemini, DeepSeek, Ollama, LMStudio.
    *   Define/update model enum (e.g., `EN2CNewProviderModel`). Note: Ollama and LMStudio use dynamic string-based model names instead of enums.
    *   Update `FN2CLLMModelUtils`: Add `Get[ProviderName]ModelValue()`, `Get[ProviderName]Pricing()`, and static pricing map entry.
    *   If new pricing structure, define `FN2CNewProviderPricing` struct.

2.  **Plugin Settings (`N2CSettings.h/.cpp`):**
    *   Add UPROPERTY for new provider's model enum, API Key UI string, and pricing map (if new).
    *   Update `GetActiveApiKey()`, `GetActiveModel()`, and pricing getters.
    *   In constructor/`InitializePricing()`, set default model and pricing.
    *   In `PostEditChangeProperty()`, handle saving the new API key UI string to `UserSecrets`.

3.  **User Secrets (`N2CUserSecrets.h/.cpp`):**
    *   Add `FString` UPROPERTY for the new provider's API key.
    *   Update `LoadSecrets()` and `SaveSecrets()` to handle the new key.

4.  **Service Implementation (`Source/Public/LLM/Providers/` and `Source/Private/LLM/Providers/`):**
    *   Create `N2CNewProviderService.h` (in Public) and `.cpp` (in Private) inheriting `UN2CBaseLLMService`.
    *   Implement:
        *   `CreateResponseParser()`: Return new `UN2CNewProviderResponseParser`.
        *   `GetConfiguration()`: Provide endpoint, auth token, system prompt support.
        *   `GetProviderHeaders()`: Define HTTP headers.
        *   `FormatRequestPayload()`: Use `UN2CLLMPayloadBuilder` with `ConfigureForNewProvider()`.
        *   `GetDefaultEndpoint()`.
    *   If OAuth support needed, add `IsUsingOAuth()` method.

5.  **Response Parser (`Source/Public/LLM/Providers/` and `Source/Private/LLM/Providers/`):**
    *   Create `N2CNewProviderResponseParser.h` (in Public) and `.cpp` (in Private) inheriting `UN2CResponseParserBase`.
    *   Implement `ParseLLMResponse()`: Deserialize provider JSON, handle errors, extract core N2C JSON content and token usage, then call `Super::ParseLLMResponse()`.

6.  **Payload Builder (`N2CLLMPayloadBuilder.h/.cpp`):**
    *   Add `ConfigureForNewProvider()` method declaration (in header) and implementation (in cpp).
    *   Update setters (`SetTemperature`, `SetMaxTokens`, etc.) with a `case EN2CLLMProvider::NewProvider:` to handle provider-specific payload structures.

7.  **LLM Module Registration (`N2CLLMModule.cpp`):**
    *   In `InitializeProviderRegistry()`, call `Registry->RegisterProvider(EN2CLLMProvider::NewProvider, UN2CNewProviderService::StaticClass());`.

8.  **System Prompts (`Content/Prompting/`, `N2CSystemPromptManager.cpp` - Optional):**
    *   If needed, add new `.md` prompt files and update `LoadPrompts()` in `UN2CSystemPromptManager`.

9.  **Local Provider Configuration (for local LLMs like Ollama/LMStudio):**
    *   For local providers, consider configuration patterns:
        *   **Ollama Pattern**: Separate config struct (`FN2COllamaConfig`) with extensive parameters
        *   **LM Studio Pattern**: Simple settings directly in `UN2CSettings` with minimal configuration
    *   Key considerations for local providers:
        *   No API key authentication (or simple dummy keys)
        *   Custom endpoint configuration
        *   Free pricing (local models)
        *   Dynamic model names (strings vs enums)
        *   Temperature parameter handling (skip for LM Studio to allow UI control)
        *   Support for prepended model commands for reasoning control

10. **Pricing**
    *   Always assume that you are unsure about the pricing of models when implementing a new one unless explicitly told the prices. If you are not told what the prices are, then search for them online via official documentation or other reputable resources.
