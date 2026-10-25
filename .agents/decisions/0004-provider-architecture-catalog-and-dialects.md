# 0004. Provider architecture: a data-driven model catalog with string IDs and wire-dialect adapters

## Status

proposed (2026-10-25), provisional pending the owner's confirmation on card 0115

**Provisional:** the owner has not decided card 0115 (review decision dec-8). Card 0128 applies the review's
recommendation so the bundled catalog can land; cards 0129 (string model IDs and the settings upgrade) and the
other E09 cards follow it. What the owner is most likely to change is kept in a few places: the file location
(`FN2CModelCatalog::GetBundledPath`), the file format (`FN2CModelCatalog::Parse` in
`Source/Private/LLM/N2CModelCatalog.cpp`, `schema_version` 1), the provider ID strings (`ProviderNames` in the same
file) and the enum-keyed wrappers (`FN2CLLMModelRegistry`). Option 1 (keep the enums) would only need the registry
to own its rows again; nothing outside `Source/*/LLM/` reads the catalog yet.

## Sources

- Kanban card 0115 "Decide the provider architecture that replaces the model enums" (owner decision, open) and card
  0128, which applies it first; cards 0129, 7 and 19 build on it.
- NodeToCode 2.0 review, 2026-10-22: dec-8 (options and recommendation), next-3 (scope), imp-llm-layer-5,
  arch-arch-core-llm-ui-3, arch-arch-organize-8. Evidence:
  `python -I Reviews/2026-10-22/kanban/lookup.py dec-8 next-3 imp-llm-layer-5 arch-arch-core-llm-ui-3 arch-arch-organize-8`
  in the workspace.
- PRs #20, #21 and #25 (live model lists, extra providers, Anthropic `/v1/models`), against main.

## Context

NodeToCode selected models through four UENUMs (`EN2COpenAIModel`, `EN2CAnthropicModel`, `EN2CGeminiModel`,
`EN2CDeepSeekModel`) and about 550 lines of hand-written rows in `FN2CLLMModelRegistry::InitializeRegistry`, with a
fixed `EN2CLLMProvider` that about 20 files switch on. A model change touched 4-6 files, `N2CLLMModels.h` is included
by 36 translation units through `N2CSettings.h`, and every change meant a rebuild and a Fab resubmission on three
engines. Model lineups now change in weeks: between 2026-10-22 and 2026-10-25 OpenAI, Anthropic and Google each
listed models the registry does not have.

Options (dec-8):

1. Keep bespoke per-provider enums and services.
2. A data-driven catalog with string model IDs and wire-dialect adapters (OpenAI-compatible chat, OpenAI Responses,
   Anthropic Messages, Gemini, Ollama native) plus provider presets.
3. Adopt a third-party multi-provider library.

The review recommends option 2: it absorbs PRs #20, #21 and #25, covers vLLM, llama.cpp, OpenRouter, Groq and Azure
without code changes, and survives model churn.

## Decision

Option 2, provisionally. The sub-questions of card 0115:

1. **(a) Catalog file location and format.** `Content/Data/N2CModelCatalog.json`, shipped with the plugin (BuildPlugin
   packages `/Content`; `Scripts/package-release.ps1` requires the file). JSON, `schema_version` 1: provider rows
   (`id`, `display_name`, `legacy_enum`, `dialect`, `base_url`, `path`, `auth_style`, `is_local`, `needs_key`,
   `models_path`) and model rows (`id`, `provider`, `display_name`, `legacy_enum`, `dialect`, `context_window`,
   `max_output`, `pricing` {`input`, `output`, `cached_input`}, `caps`, `lifecycle` {`status`, `shutdown_date`},
   `is_default`); the full field list is in `Context/llm-providers/AGENTS-llm-providers.md`. The file lists the
   models NodeToCode offers; shut-down enumerators are compiled-in retired rows that exist only for saved settings.
   A missing or malformed file logs an error and leaves a compiled-in list with each cloud provider's default.
   An optional user overlay, `<Project>/Saved/NodeToCode/N2CModelCatalog.json`, merged over the shipped rows by
   provider and model ID, is a later card; it is not read yet.
2. **(b) Migration.** A one-time upgrader in `UN2CSettings::PostInitProperties` (and `PostReloadConfig`) reads the old
   enum keys through `GConfig` and maps each enumerator name to its row with `FN2CModelCatalog::FindByLegacyEnum`
   (every live row names its enumerator in `legacy_enum`; the retired ones are compiled in), writes the new string
   ID keys and records that it ran (card 0129). DeepSeek rows share a model ID, so a DeepSeek selection is the model
   ID plus its `reasoning_effort`. No CoreRedirects EnumRedirects: no enumerator is removed in 2.x. The four model
   enums stay for the 2.x line as deprecated BlueprintType types, since user Blueprints or Python may reference them,
   and go in the next major version.
3. **(c) Live `/models` discovery.** Off by default, opt in per provider in Project Settings, never run in commandlets
   or `-unattended` sessions; discovered IDs are merged under the catalog rows, which win for known IDs.
4. **(d) Bespoke dialects and presets.** Bespoke adapters: OpenAI Chat Completions, OpenAI Responses, Anthropic
   Messages, Gemini and Ollama native (needed for `num_ctx`). Presets of the OpenAI-compatible chat adapter: DeepSeek
   (with its `thinking` extension), LM Studio, MiniMax, vLLM, llama.cpp, OpenRouter, Groq and Azure OpenAI. Each
   preset is a provider row (`dialect`, `base_url`, `path`, `auth_style`, `models_path`), not code.
5. **(e) Provider identity.** A string provider ID (`FName`: `openai`, `anthropic`, `gemini`, `deepseek`, `ollama`,
   `lmstudio`, later presets) keys the catalog and, after card 0129, the settings. `EN2CLLMProvider` stays only for
   the built-in services until the dialect adapters replace them, and for the config upgrade; each provider row names
   its value in `legacy_enum`.

## Consequences

- Adding, repricing, deprecating or retiring a model is an edit to `Content/Data/N2CModelCatalog.json` with no
  rebuild, checked by `NodeToCode.LLM.Catalog`, `NodeToCode.LLM.Registry` and the payload goldens. Until card 0129
  lands, a model users pick in Project Settings still needs an appended enumerator.
- `FN2CModelMetadata` moved to `N2CModelCatalog.h` (same module, same struct path) and gained `ProviderId`,
  `Dialect`, `LegacyEnumName`, `CachedInputCostPer1M`, `StructuredOutputMode`, `ReasoningParam`, `ReasoningLevels`,
  `MaxTokensField`, `bSupportsStreaming` and `bIsDefault`. `FN2CLLMModelRegistry` is an enum-keyed view of the catalog
  and is removed when its callers move to string IDs.
- If the catalog file does not load, settings keep model selections and price overrides the fallback does not know
  rather than migrating them, so a broken install never rewrites a project's config.
- If the owner chooses option 1 or 3, a new record supersedes this one; the catalog file can stay as the registry's
  data source either way.
