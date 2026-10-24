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
│   ├── N2CLLMModels.h                    # Model enums, N2CDefaultModels, FN2CLLMModelUtils
│   ├── N2CLLMModelRegistry.h             # FN2CModelMetadata + FN2CLLMModelRegistry (model data)
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
    ├── N2CLLMModels.cpp                  # FN2CLLMModelUtils, forwards to the registry
    ├── N2CLLMModelRegistry.cpp           # InitializeRegistry: one entry per cloud model
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
*   **`N2CLLMRequestHandle.h` (`FN2CLLMRequestHandle`)**: `UN2CHttpHandlerBase::PostLLMRequest`, `IN2CLLMService::SendRequest`, `ProcessN2CJson` and `ProcessN2CJsonWithOverrides` return the same `FN2CLLMRequestHandleRef` for a request. `Cancel()` calls `IHttpRequest::CancelRequest()` and marks it cancelled; `OnComplete` runs at most once and never after a `Cancel()` that returned true (HTTP completions run on the game thread, so a game-thread `Cancel()` always wins against a pending request). Errors found before sending complete synchronously and return an already-completed handle. `OnCancelled()` lets a higher layer (the module resets its status to Idle; MCP tasks, batches and subsystem request ids later) attach its own cleanup. Callers that do not cancel can ignore the handle. Deliver results only through the handle (`Complete()` / `Wrap()`), never by executing `OnComplete` directly. Test: `NodeToCode.Robustness.LLMRequestCancel`.
*   **`N2CBaseLLMService.h/.cpp` (`UN2CBaseLLMService`)**: Abstract base class for LLM services, providing common initialization and request flow. Derived classes implement: `CreateResponseParser()`, `GetConfiguration()`, `GetProviderHeaders()`, `FormatRequestPayload()`, `GetDefaultEndpoint()`.
*   **Provider Implementations (`Source/Public/LLM/Providers/`):**
    *   Service classes inheriting `UN2CBaseLLMService`:
        *   `UN2COpenAIService` - OpenAI Chat Completions API (`/v1/chat/completions`), or the Responses API (`/v1/responses`, `FormatResponsesAPIPayload`) for a model whose registry entry sets `bRequiresResponsesAPI` (GPT-5.2 Pro today). An ID the registry does not know goes to Chat Completions.
        *   `UN2CAnthropicService` - Anthropic Claude API (`x-api-key`, plain string `system`)
        *   `UN2CGeminiService` - Google Gemini API (`generativelanguage.googleapis.com`, key in `x-goog-api-key`)
        *   `UN2CDeepSeekService` - DeepSeek API
        *   `UN2COllamaService` - Local Ollama API (with custom `FN2COllamaConfig`)
        *   `UN2CLMStudioService` - Local LM Studio API (OpenAI-compatible)
    *   Each implements provider-specific API endpoint, auth, headers, and payload formatting.
*   **`N2CLLMProviderRegistry.h/.cpp` (`UN2CLLMProviderRegistry`)**: Singleton factory for creating LLM service instances. Methods: `RegisterProvider()`, `CreateProvider()`, `IsProviderRegistered()`, `GetRegisteredProviders()`.
*   **`N2CLLMPayloadBuilder.h/.cpp` (`UN2CLLMPayloadBuilder`)**: Utility for building provider-specific JSON request payloads. Key methods:
    *   `Initialize()`, `SetModel()`, `SetTemperature()`, `SetMaxTokens()`
    *   `AddSystemMessage()`, `AddUserMessage()`
    *   `SetJsonResponseFormat()` / `SetStructuredOutput()` - Schema-based output
    *   `ConfigureForOpenAI()`, `ConfigureForAnthropic()`, `ConfigureForGemini()`, `ConfigureForDeepSeek()`, `ConfigureForOllama()`, `ConfigureForLMStudio()`
    *   `GetN2CResponseSchema()` - Static method for N2C translation format schema
*   **`N2CResponseParserBase.h/.cpp` (`UN2CResponseParserBase`)**: Abstract base for LLM response parsers. Handles common N2C JSON structure parsing, error handling (`HandleCommonErrorResponse()`), content extraction (`ExtractChatCompletionContent()` for OpenAI-compatible bodies), stop and finish reasons (`SetStopReasonError()`, `KindFromFinishReason()`: every provider parser checks its stop reason before the content and reports `Truncated`, `Refused` or `Blocked`), graph/code data extraction, and JSON code block marker processing. Reasoning fields are logged, never parsed as the answer.
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
    *   `EN2CDeepSeekModel`: DeepSeek_R1 (default: `deepseek-flash`, thinking high), DeepSeek_V3 (`deepseek-flash`, thinking off, `json_object`), DeepSeek_V4_Pro (`deepseek-v4-pro`, thinking high). The old IDs `deepseek-reasoner`/`deepseek-chat` were discontinued 2026-07-24; the enumerator names stay. R1 and V3 share a model ID and differ in `FN2CModelMetadata::ReasoningEffort`, which travels per request in `FN2CLLMConfig::ReasoningEffort` (filled by `UN2CSettings::ResolveRequestModel`); `GetModelMetadataById(Id, Effort)` picks the entry, and a bare ID gets the first entry registered (the API default, thinking on)
    *   Defaults live in `N2CDefaultModels` (same header). Enumerators are never renamed or removed, because settings save them by name: new models are appended, retired ones get `UMETA(Hidden)` plus `.Retired(date)` in the registry, deprecated-but-live ones get `.ShutsDown(date)` and the date in their display name. `UN2CSettings::PostInitProperties` moves a saved retired selection to the default and logs one warning. `NodeToCode.LLM.Registry` checks these rules.
    *   Note: Ollama and LM Studio use dynamic string-based model names instead of enums
    *   `FN2CLLMModelUtils`: Model string ID getters, pricing getters, system prompt support checks. Every function forwards to `FN2CLLMModelRegistry`; it holds no model data of its own.
*   **`N2CLLMModelRegistry.h/.cpp` (`FN2CLLMModelRegistry`, `FN2CModelMetadata`)**: The single source of model data for the four cloud providers. `FN2CLLMModelRegistry::Get()` builds it once in `InitializeRegistry()`, which adds one `FN2CModelMetadata` per enumerator to the provider's map (`OpenAIModels`, `AnthropicModels`, `GeminiModels`, `DeepSeekModels`) and then indexes every entry by model ID. Lookups: `GetModelMetadata(Enum)`, `GetModelMetadataById(Id)` and `GetModelMetadataById(Id, ReasoningEffort)`, `GetModelId`, `RequiresResponsesAPI`, the per-provider pricing getters and `GetAllModelsForProvider`. `FN2CModelMetadata` fields:
    *   Identity: `ModelId` (the API ID sent to the provider), `DisplayName`, `Provider`
    *   Pricing (USD per 1M tokens): `InputCostPer1M`, `OutputCostPer1M`
    *   Capabilities: `ContextWindowSize` (default 128000; the token estimator reads it), `MaxOutputTokens`, `bSupportsSystemPrompts` (false merges the system prompt into the user message), `bSupportsStructuredOutput` (OpenAI: false sends no `response_format`; DeepSeek: only true sends `json_object`; Gemini always gets the schema), `bIsReasoningModel` (descriptive only: request building reads the sampling and max-tokens flags instead), `bRequiresResponsesAPI`, `bAcceptsSamplingParams` (false sends no temperature/top_p/top_k), `bUsesMaxCompletionTokens` (OpenAI: `max_completion_tokens` instead of `max_tokens`), `ReasoningEffort` (per-request thinking control; entries may share a `ModelId` when it differs)
    *   Generation defaults: `DefaultTemperature` (sent only with `bAcceptsSamplingParams`), `DefaultMaxTokens` (at most `MaxOutputTokens`)
    *   Lifecycle: `bIsDeprecated`, `bIsRetired`, `ShutdownDate`, set with `.ShutsDown(date)` or `.Retired(date)`; `.WithReasoningEffort(effort)` sets `ReasoningEffort`
    *   An enumerator with no entry gets no metadata: `GetModelId` returns the provider default's ID, so the request silently goes to the default model. A model ID with no entry (for example an MCP model override) gets no sampling fields; on OpenAI it also gets `max_completion_tokens`, strict `json_schema` and Chat Completions, and on DeepSeek no `response_format`.
*   **`N2CLLMPricing.h`**: Pricing structs per provider - `FN2COpenAIPricing`, `FN2CAnthropicPricing`, `FN2CGeminiPricing`, `FN2CDeepSeekPricing`. Each has `InputCost` and `OutputCost` (per 1M tokens).
*   **`N2COllamaConfig.h`**: `FN2COllamaConfig` USTRUCT with Ollama-specific settings. Only what the user sets is sent (`ConfigureForOllama`):
    *   General: `OllamaEndpoint`, `bUseSystemPrompts` (default true), `PrependedModelCommand`, `Think` (`EN2COllamaThink`: Default omits `think`, On/Off send true/false, Level sends `ThinkLevel`), `KeepAlive` (seconds, top-level `keep_alive`)
    *   Generation: `NumPredict` (max tokens, sent only when > 0), and `Temperature`, `TopP`, `TopK`, `MinP`, `RepeatPenalty`, each sent only when its `bOverride_*` toggle is on
    *   Context: `NumCtx` (sent only when > 0, otherwise the server's context length applies; the token estimator uses it and `WarnIfPromptExceedsContext` warns when a prompt does not fit)
    *   Advanced: `Mirostat`, `MirostatEta`, `MirostatTau`, `Seed`, each behind a `bOverride_*` toggle
    *   Saved configs from before 2.0 are migrated by `UN2CSettings::MigrateLegacyOllamaDefaults` while `SettingsVersion` < 1: values at the old always-sent defaults are cleared, other saved sampler values get their override on

## Authentication

Every cloud provider authenticates with its API key from `FN2CSecretStore` (see `Context/architecture/AGENTS-architecture.md`); Ollama and LM Studio need none. `UN2CBaseLLMService::SendRequest` fails a cloud request whose key is empty before anything is sent, with an error that names the key (`GetMissingApiKeyError`). NodeToCode 2.0 removed Claude Pro/Max and Google account sign-in; do not add provider sign-in flows or reuse another application's client credentials. Users with a Claude, ChatGPT or Google AI subscription connect that vendor's own agent (Claude Code, Antigravity CLI, Codex CLI) to the MCP server instead; the user guide is `wiki/developers/Using-a-Claude-or-Gemini-Subscription.md`, and a change to the MCP endpoint, its port setting or the stdio bridge updates that page.

## Structured Output Support

Comprehensive structured output support across providers:

*   **OpenAI**: `json_schema` type with strict schema validation
*   **LM Studio**: OpenAI-compatible with `strict: true` parameter
*   **Gemini**: `responseMimeType` and `responseSchema` in generation config
*   **DeepSeek**: `json_object` type format, sent only for registry entries with `bSupportsStructuredOutput` (the non-thinking mode); thinking is `thinking.type` plus a top-level `reasoning_effort`, and no temperature is sent with thinking on
*   **Ollama**: Direct schema in `format` field
*   **Schema Definition**: `GetN2CResponseSchema()` provides consistent N2C translation format

## Prepended Model Commands

Support for model-specific command prefixes for local providers:

*   **Ollama Configuration** (`N2COllamaConfig.h`):
    *   `PrependedModelCommand`: Text prepended to user messages
    *   Use cases: model-specific commands; thinking is controlled with `Think` instead
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

1.  **Add the model to the registry (`N2CLLMModels.h`, `N2CLLMModelRegistry.cpp`).** This is the only step for a new or changed model of an existing cloud provider:
    *   Append an enumerator at the end of the provider's enum in `N2CLLMModels.h` (never rename, reorder or remove one: settings save them by name).
    *   Add its entry in `FN2CLLMModelRegistry::InitializeRegistry()` (`N2CLLMModelRegistry.cpp`) with every `FN2CModelMetadata` field set from the provider's model page: ID, display name, prices, context window, max output, and the capability flags above. Set `bRequiresResponsesAPI` for an OpenAI model that the Chat Completions endpoint does not serve; leave `bAcceptsSamplingParams` false unless the provider documents that the model accepts temperature. Nothing else holds model data: `FN2CLLMModelUtils` forwards to the registry and the `UN2CSettings` pricing `TMap`s (`OpenAIModelPricing` and the others) hold only user overrides.
    *   Retiring a model: keep the enumerator, add `UMETA(Hidden)` and `.Retired(date)`. Deprecated but still served: `.ShutsDown(date)` and the date in the display name. Changing a default: update `N2CDefaultModels` and the default checks in `Source/Private/Tests/LLM/N2CModelRegistry.spec.cpp`.
    *   Run the `NodeToCode.LLM.Registry` automation tests, which fail when an enumerator has no entry, when `UMETA(Hidden)` and `.Retired` disagree, or when a live model ID with the same `ReasoningEffort` appears twice.

    **New provider only (`N2CLLMTypes.h`, `N2CLLMModels.h/.cpp`, `N2CLLMModelRegistry.h/.cpp`, `N2CLLMPricing.h`):**
    *   Add to `EN2CLLMProvider` enum. Current providers: OpenAI, Anthropic, Gemini, DeepSeek, Ollama, LMStudio.
    *   Define the model enum (e.g., `EN2CNewProviderModel`) and its `N2CDefaultModels` entry. Ollama and LMStudio use dynamic string-based model names instead of enums and have no registry entries.
    *   In `FN2CLLMModelRegistry`, add a `TMap<EN2CNewProviderModel, FN2CModelMetadata>`, its `GetModelMetadata`/`GetModelId`/pricing overloads, its entries in `InitializeRegistry()` and its loop in the model ID index at the end of `InitializeRegistry()`.
    *   Add `FN2CLLMModelUtils` forwarders only if callers need them, and a `FN2CNewProviderPricing` struct if the pricing getters need one.

2.  **Plugin Settings (`N2CSettings.h/.cpp`):**
    *   Add UPROPERTY for new provider's model enum (defaulting to its `N2CDefaultModels` value), API Key UI string, and pricing override `TMap` (user overrides; the getters fall back to the registry).
    *   Update `GetActiveApiKey()`, `GetActiveModel()`, the `*ForProvider` variants, and pricing getters.
    *   Add the provider's model property to `MigrateRetiredModelSelections()`. `InitializePricing()` is kept but empty: default prices come from the registry.
    *   In `PostEditChangeProperty()`, save the new API key UI string with `FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::NewProvider, ...)`, and fill the UI string from `GetApiKey` in the constructor.

3.  **Secret store (`N2CSecretStore.h/.cpp`):**
    *   Add the provider to `FN2CSecretStore::GetProviderId()`. Its key is stored as `<ProviderId>_API_Key`; nothing else in the store changes.

4.  **Service Implementation (`Source/Public/LLM/Providers/` and `Source/Private/LLM/Providers/`):**
    *   Create `N2CNewProviderService.h` (in Public) and `.cpp` (in Private) inheriting `UN2CBaseLLMService`.
    *   Implement:
        *   `CreateResponseParser()`: Return new `UN2CNewProviderResponseParser`.
        *   `GetConfiguration()`: Provide endpoint, auth token, system prompt support.
        *   `GetProviderHeaders()`: Define HTTP headers.
        *   `FormatRequestPayload()`: Use `UN2CLLMPayloadBuilder` with `ConfigureForNewProvider()`.
        *   `GetDefaultEndpoint()`.
    *   Add the provider to `UN2CBaseLLMService::GetMissingApiKeyError` if it needs an API key.

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
