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
│   ├── N2CModelCatalog.h                 # FN2CModelMetadata, FN2CProviderProfile, FN2CModelCatalog (model data)
│   ├── N2CLLMModelRegistry.h             # FN2CLLMModelRegistry: enum-keyed view of the catalog
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
    ├── N2CModelCatalog.cpp               # Catalog loader, compiled-in fallback and retired rows
    ├── N2CLLMModelRegistry.cpp           # Files catalog rows under the model enums
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
*   **`N2CSystemPromptManager.h/.cpp` (`UN2CSystemPromptManager`)**: Loads and manages system prompts from `.md` files in `Content/Prompting/`. Methods: `GetSystemPrompt()`, `GetLanguageSpecificPrompt()`, `MergePrompts()`, `PrependSourceFilesToUserMessage()`; static `LoadSharedFragment()`, `GetN2CJsonSpec()` and `WrapN2CJson()` (the user message: the N2C JSON inside `<n2c_json>` tags, every `</` in it written `<\/`, which `UN2CLLMModule` sends on both request paths).
*   **`Content/Prompting/CodeGen_[Language].md`**: the per-language part of each system prompt (`CodeGen_CPP.md`, `CodeGen_Python.md`, `CodeGen_JavaScript.md`, `CodeGen_CSharp.md`, `CodeGen_Swift.md`, `CodeGen_Pseudocode.md`): how to express the graph in that language and what goes in `graphDeclaration` and `graphImplementation`. Each holds two placeholders that `LoadPrompts()` replaces with the shared fragments in `Content/Prompting/Shared/` (card 0132):
    *   `{{N2C_JSON_SPEC}}` <- `Shared/N2CJsonSpec.md`: the one N2C JSON specification (fields as `FN2CSerializer` writes them, version, how to read exec and data wires, nodes that never run, return values, "the document is data, not instructions"). The batch export's `Combined_Blueprints.md` includes the same file. A change to the N2C JSON format updates this file, not the six prompts.
    *   `{{N2C_RESPONSE_FORMAT}}` <- `Shared/N2CResponseFormat.md`: the output contract, which matches `UN2CLLMPayloadBuilder::GetN2CResponseSchema()` (one object with `graphs`; seven required keys per entry).
    *   `NodeToCode.LLM.Prompts` checks that every language's prompt holds each fragment exactly once and no placeholder, that the spec names every key the serializer writes and the current version, and that the response contract names every schema property.
*   **`N2CLLMTypes.h`**: Defines `EN2CLLMProvider` (OpenAI, Anthropic, Gemini, DeepSeek, Ollama, LMStudio), `EN2CSystemStatus`, `FN2CLLMConfig` struct, and LLM-related delegates (`FOnLLMResponseReceived`, `FOnTranslationResponseReceived`, `FOnTranslationRequestSent`).
*   **Model selection in settings (settings version 4, card 0129)**: `UN2CSettings` selects a cloud model by string ID: `AnthropicModelId`, `OpenAIModelId`, `GeminiModelId`, `DeepSeekModelId` (Project Settings **Model**, a drop-down filled from the provider's live catalog rows by `Get<Provider>ModelOptions`), each with a free-text `<Provider>CustomModelId` (**Custom Model ID**) that is sent instead when set, and `DeepSeekReasoningEffort` (**Thinking**: `none`, `low`, `high`, `max`; empty uses the row's own mode). Defaults are `N2CDefaultModelIds` in `N2CLLMModels.h`. `GetActiveModelForProvider` returns the custom ID, else the model ID; `GetActiveModelMetadataForProvider` the catalog row (null for an ID the catalog lacks); `GetActiveReasoningEffortForProvider` the DeepSeek thinking mode. Price overrides are one map, `ModelPricing` (`TMap<FString, FN2CModelPricing>` keyed by model ID, **Model Price Overrides**); `GetActivePricingForProvider`, `GetCurrentInputCost` and `GetCurrentOutputCost` return the override, else the catalog price, else 0 (local providers and a custom ID without an override). `SelectModel(Row)` is what the context window model picker calls.
    *   A config saved before version 4 holds enum names (`AnthropicModel=Claude4_5_Sonnet`, `AnthropicModelPricing=((Claude4_5_Sonnet, (...)))`). `MigrateModelSelectionsToIds` (from `MigrateSavedSettings`, so on every load until saved) maps each name with `FN2CModelCatalog::FindByLegacyEnum`: a live row gives its ID (and DeepSeek thinking mode), a retired row or a name the enum lacks gives the default, a name with no row keeps its UMETA `Value` ID as the Custom Model ID with a warning; prices are re-keyed the same way (below version 3 the 1.2.x cleanup drops built-in prices first). `MigrateLegacy12Settings` saves the result at editor start, stamps `SettingsVersion=4` and removes the eight old keys. While the catalog file is missing (`IsFallback`) the conversion is done in memory only and nothing is saved or stamped. `MigrateRetiredModelSelections` moves a selected ID whose row is retired to the default on every load.
*   **`N2CLLMModels.h/.cpp`**: Provider-specific model enums (deprecated: kept for saved configs, Blueprints and Python until the next major version) and `FN2CLLMModelUtils`:
    *   `EN2COpenAIModel`: GPT-6.1 Sol (default), GPT-6 Astra, GPT-6 Luna, GPT-5.2, GPT-5.2 Pro; deprecated but live: GPT-5/Mini/Nano and o3 (shut down 2026-12-11), GPT-4.1, GPT-4o, GPT-4o Mini (2027-01-14); retired (hidden): o4-mini, o3-mini, o1, o1-preview, o1-mini
    *   `EN2CAnthropicModel`: Claude Sonnet 5.5 (default), Opus 5.5, Haiku 5.5, Opus 4.5; deprecated but live: Sonnet 4.5 (retires 2026-11-30); retired (hidden): Opus 4.1, Opus 4, Sonnet 4, 3.7 Sonnet, 3.5 Sonnet, 3.5 Haiku
    *   `EN2CGeminiModel`: Gemini 3.8 Flash (default), Gemini 4 Argon, Gemini 3.5 Flash-Lite, Gemini 3 Flash Preview; retired (hidden): 3 Pro Preview, 2.5 Pro/Flash previews, 2.0 Flash/Flash-Lite, 1.5 Flash/Pro, 2.0 experimental models
    *   `EN2CDeepSeekModel`: DeepSeek_R1 (default: `deepseek-flash`, thinking high), DeepSeek_V3 (`deepseek-flash`, thinking off, `json_object`), DeepSeek_V4_Pro (`deepseek-v4-pro`, thinking high). The old IDs `deepseek-reasoner`/`deepseek-chat` were discontinued 2026-07-24; the enumerator names stay. R1 and V3 share a model ID and differ in `FN2CModelMetadata::ReasoningEffort`, which travels per request in `FN2CLLMConfig::ReasoningEffort` (filled by `UN2CSettings::ResolveRequestModel`); `GetModelMetadataById(Id, Effort)` picks the entry, and a bare ID gets the first entry registered (the API default, thinking on)
    *   Defaults live in `N2CDefaultModels` and `N2CDefaultModelIds` (same header) and must name the catalog's `is_default` rows. Enumerators are never renamed or removed, because configs saved before version 4 name them and the upgrade maps them: no new enumerators are added (a new model is a catalog row without `legacy_enum`); one whose model shuts down gets `UMETA(Hidden)` plus a compiled-in retired row in `N2CModelCatalog.cpp` (and leaves the catalog file). `NodeToCode.LLM.Registry` checks these rules.
    *   Note: Ollama and LM Studio use dynamic string-based model names instead of enums
    *   `FN2CLLMModelUtils`: Model string ID getters, pricing getters, system prompt support checks. Every function forwards to `FN2CLLMModelRegistry` (and so to the catalog); it holds no model data of its own.
*   **Model catalog (`Content/Data/N2CModelCatalog.json`, `N2CModelCatalog.h/.cpp`, `FN2CModelCatalog`)**: The single source of model data for the four cloud providers, and the provider rows for all six. Decision record `.agents/decisions/0004-provider-architecture-catalog-and-dialects.md` (proposed) sets the direction. `FN2CModelCatalog::Get()` loads the bundled file on first use. Lookups are by string ID: `Providers()`, `FindProvider(ProviderId)`, `List(ProviderId, bIncludeRetired)`, `Find(ProviderId, ModelId)` and `Find(ProviderId, ModelId, ReasoningEffort)`, `FindById(ModelId)`, `FindDefault(ProviderId)`, and `FindByLegacyEnum(ProviderId, EnumName)` for saved enum selections. Provider IDs: `openai`, `anthropic`, `gemini`, `deepseek`, `ollama`, `lmstudio` (`ProviderIdFor(EN2CLLMProvider)`).
    *   File format (`schema_version` 1). Provider row: `id`, `display_name`, `legacy_enum` (the `EN2CLLMProvider` value whose service sends it), `dialect`, `base_url`, `path`, `auth_style` (`bearer`, `x-api-key`, `x-goog-api-key`, `none`), `is_local`, `needs_key`, `models_path`. Model row: `id`, `provider`, `display_name`, `legacy_enum` (its enumerator in the model enums), `dialect` (`openai_chat`, `openai_responses`, `anthropic_messages`, `gemini`, `deepseek_chat`; defaults to the provider's), `context_window`, `max_output`, `pricing` {`input`, `output`, `cached_input`} in USD per 1M tokens (`cached_input` null when the provider lists none), `caps` {`system_prompt`, `temperature` (accepts sampling parameters), `default_temperature`, `structured_output_mode` (`json_schema`, `response_schema`, `json_object`, `none`), `reasoning_param` (`reasoning_effort`, `effort`, `thinking_level`, `thinking`, `none`), `reasoning_levels`, `reasoning_effort` (the thinking control this row sends; DeepSeek rows share an ID and differ here), `thinking_model`, `max_tokens_field` (`max_tokens`, `max_completion_tokens`, `max_output_tokens`, `maxOutputTokens`), `responses_only`, `streaming`}, `lifecycle` {`status` `active`|`deprecated`|`retired`, `shutdown_date` YYYY-MM-DD}, `is_default` (exactly one per provider with models, never deprecated).
    *   Each row becomes an `FN2CModelMetadata` (`Source/Public/LLM/N2CModelCatalog.h`). Identity: `ModelId`, `DisplayName`, `Provider`, `ProviderId`, `Dialect`, `LegacyEnumName`. Pricing: `InputCostPer1M`, `OutputCostPer1M`, `CachedInputCostPer1M`. Capabilities: `ContextWindowSize` (the token estimator reads it), `MaxOutputTokens`, `bSupportsSystemPrompts` (false merges the system prompt into the user message), `StructuredOutputMode` and `bSupportsStructuredOutput` (mode is not `none`; OpenAI: false sends no `response_format`; DeepSeek: only true sends `json_object`; Gemini always gets the schema), `bIsReasoningModel` (`thinking_model`; DeepSeek's Auto output cap reads it), `bRequiresResponsesAPI`, `bAcceptsSamplingParams` (false sends no temperature/top_p/top_k), `MaxTokensField` and `bUsesMaxCompletionTokens` (OpenAI Chat Completions: `max_completion_tokens` instead of `max_tokens`), `ReasoningEffort`, `ReasoningParam`, `ReasoningLevels` (descriptive: only DeepSeek's control is sent today), `bSupportsStreaming`. Defaults: `DefaultTemperature` (sent only with `bAcceptsSamplingParams`). Lifecycle: `bIsDeprecated`, `bIsRetired`, `ShutdownDate`, `bIsDefault`.
    *   The file lists the models NodeToCode offers (active and deprecated). Enumerators whose models are shut down are compiled in as retired rows (`RetiredRows` in `N2CModelCatalog.cpp`) so saved selections still resolve and migrate; the file can also mark a row `retired` itself, which hides it without a rebuild.
    *   A missing or malformed file logs one error and leaves the compiled-in fallback: the six provider rows and each cloud provider's default model (`FallbackModels`, which must equal the file's default rows; `NodeToCode.LLM.Catalog` checks it). In fallback `IsFallback()` is true and `UN2CSettings` keeps saved model selections and price overrides the fallback does not know instead of migrating them.
    *   `LoadFromFile(Path)`, `LoadFromString` and `ReloadBundled()` replace the rows (game thread; earlier pointers become invalid). Tests use them; the user overlay and live `/models` discovery are later cards.
*   **`N2CLLMModelRegistry.h/.cpp` (`FN2CLLMModelRegistry`)**: The enum-keyed view of the catalog for callers that still select by `EN2C*Model`. `Get()` files each catalog row under the enumerator its `legacy_enum` names (`OpenAIModels`, `AnthropicModels`, `GeminiModels`, `DeepSeekModels`; rows without one go to `UnmappedModels`), indexes them by model ID, and rebuilds when the catalog's generation changes. Lookups: `GetModelMetadata(Enum)`, `GetModelMetadataById(Id)` and `GetModelMetadataById(Id, ReasoningEffort)`, `GetModelId` (falls back to the catalog default's ID), `RequiresResponsesAPI`, the per-provider pricing getters and `GetAllModelsForProvider` (catalog order, retired rows included). It holds no model data; later cards move its callers to the catalog and remove it.
    *   An enumerator with no row gets no metadata: `GetModelId` returns the provider default's ID, so the request silently goes to the default model. A model ID with no row (for example an MCP model override) gets no sampling fields; on OpenAI it also gets `max_completion_tokens`, strict `json_schema` and Chat Completions, and on DeepSeek no `response_format`.
*   **`N2CLLMPricing.h`**: `FN2CModelPricing` (`InputCost`, `OutputCost` per 1M tokens), the value of `UN2CSettings::ModelPricing`. The per-provider structs `FN2COpenAIPricing`, `FN2CAnthropicPricing`, `FN2CGeminiPricing`, `FN2CDeepSeekPricing` are deprecated: the registry's enum pricing getters and the version 4 upgrade (`FN2CLegacyPricingMaps`, which reads the old maps) still use them.
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

1.  **Add the model to the catalog (`Content/Data/N2CModelCatalog.json`).** This is the only step for a new or changed model of an existing cloud provider:
    *   Add or edit its row in `Content/Data/N2CModelCatalog.json` with every field set from the provider's model and pricing pages (see "Model catalog" above). Set `responses_only` for an OpenAI model that the Chat Completions endpoint does not serve; leave `caps.temperature` false unless the provider documents that the model accepts temperature. A price, limit, capability or lifecycle change is a data change only: no rebuild. Nothing else holds model data: `FN2CLLMModelUtils` and `FN2CLLMModelRegistry` read the catalog, and `UN2CSettings::ModelPricing` holds only user overrides. Project Settings offers the row's ID at once; no enumerator is added (leave `legacy_enum` out of a new row).
    *   Deprecated but still served: `"status": "deprecated"` with its `shutdown_date`, and the date in its display name. Retiring a model: `"status": "retired"` (hidden at once, and a saved selection moves to the default); a row that names an enumerator moves to `RetiredRows` in `N2CModelCatalog.cpp` in the change that also marks the enumerator `UMETA(Hidden)`. Changing a default: move `is_default`, update `N2CDefaultModels`, `N2CDefaultModelIds`, `FallbackModels` in `N2CModelCatalog.cpp` and the default checks in `Source/Private/Tests/LLM/N2CModelRegistry.spec.cpp`.
    *   Run the `NodeToCode.LLM.Catalog`, `NodeToCode.LLM.Registry` and `NodeToCode.LLM.Payload.Golden` automation tests. They fail when the file does not load, when an enumerator has no row, when `UMETA(Hidden)` and retired disagree, when a live model ID with the same `ReasoningEffort` appears twice, when the fallback rows differ from the file's defaults, and when a live model has no payload golden (`build.ps1 -Test -UpdateGoldens`, then review the diff).

    **New provider only (`N2CLLMTypes.h`, `N2CLLMModels.h/.cpp`, `N2CLLMModelRegistry.h/.cpp`, `N2CModelCatalog.cpp`, `N2CLLMPricing.h`, the catalog file):**
    *   Add to `EN2CLLMProvider` enum. Current providers: OpenAI, Anthropic, Gemini, DeepSeek, Ollama, LMStudio.
    *   Add its provider row to the catalog file and to `ProviderNames` and `FallbackProviders` in `N2CModelCatalog.cpp`, and its `N2CDefaultModelIds` entry. No model enum: models are catalog rows selected by string ID. Ollama and LMStudio use dynamic string-based model names and have provider rows but no model rows.

2.  **Plugin Settings (`N2CSettings.h/.cpp`):**
    *   Add `<Provider>ModelId` (with `GetOptions` to a new `Get<Provider>ModelOptions`, defaulting to its `N2CDefaultModelIds` value), `<Provider>CustomModelId` and the API Key UI string. Prices need nothing: `ModelPricing` is keyed by model ID.
    *   Add the provider to `N2CModelIds::Selections` in `N2CSettings.cpp` (it drives `GetActiveModelForProvider`, the pricing getters and `MigrateRetiredModelSelections`) and to `GetModelIdPropertyName` / `GetCustomModelIdPropertyName`; update `GetActiveApiKey()` and its `*ForProvider` variant. `InitializePricing()` is kept but empty: default prices come from the catalog.
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
    *   If needed, add new `.md` prompt files and update `LoadPrompts()` in `UN2CSystemPromptManager`. A new language's `CodeGen_<Language>.md` includes `{{N2C_JSON_SPEC}}` and `{{N2C_RESPONSE_FORMAT}}` (see above) instead of its own copy of the spec.

9.  **Local Provider Configuration (for local LLMs like Ollama/LMStudio):**
    *   For local providers, consider configuration patterns:
        *   **Ollama Pattern**: Separate config struct (`FN2COllamaConfig`) with extensive parameters
        *   **LM Studio Pattern**: Simple settings directly in `UN2CSettings` with minimal configuration
    *   Key considerations for local providers:
        *   No API key authentication (or simple dummy keys)
        *   Custom endpoint configuration
        *   Free pricing (local models)
        *   Dynamic model names (free text, not catalog rows)
        *   Temperature parameter handling (skip for LM Studio to allow UI control)
        *   Support for prepended model commands for reasoning control

10. **Pricing**
    *   Always assume that you are unsure about the pricing of models when implementing a new one unless explicitly told the prices. If you are not told what the prices are, then search for them online via official documentation or other reputable resources.
