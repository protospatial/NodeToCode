// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.h"
#include "N2CModelCatalog.generated.h"

/**
 * One model row of the model catalog (FN2CModelCatalog): what NodeToCode knows about a model, read from the bundled
 * catalog file Content/Data/N2CModelCatalog.json. Request building reads the capability flags; the cost and
 * context displays read the prices and limits.
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CModelMetadata
{
	GENERATED_BODY()

	/** API identifier sent to the provider (e.g., "claude-sonnet-5-5"). Catalog field: id */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString ModelId;

	/** Human-readable display name (e.g., "Claude Sonnet 5.5"). Catalog field: display_name */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString DisplayName;

	/** Provider this model belongs to (the provider row's legacy_enum) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

	/** Catalog provider ID (e.g., "anthropic"). Catalog field: provider */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FName ProviderId;

	/**
	 * Wire dialect of requests to this model: openai_chat, openai_responses, anthropic_messages, gemini,
	 * deepseek_chat or ollama. Catalog field: dialect (defaults to the provider's)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString Dialect;

	/**
	 * Name of the model's enumerator in EN2COpenAIModel, EN2CAnthropicModel, EN2CGeminiModel or EN2CDeepSeekModel,
	 * which saved settings use. Empty for a model added only in the catalog. Catalog field: legacy_enum
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FString LegacyEnumName;

	// === Pricing (per 1M tokens in USD). Catalog field: pricing ===

	/** Cost per 1M input tokens (USD). pricing.input */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pricing")
	float InputCostPer1M = 0.0f;

	/** Cost per 1M output tokens (USD). pricing.output */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pricing")
	float OutputCostPer1M = 0.0f;

	/** Cost per 1M cached input tokens (USD); 0 when the provider lists none. pricing.cached_input */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pricing")
	float CachedInputCostPer1M = 0.0f;

	// === Capabilities ===

	/** Maximum context window size in tokens. context_window */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	int32 ContextWindowSize = 128000;

	/**
	 * Maximum output tokens this model can generate. Requests send this in Auto (up to the non-streaming cap, and
	 * reduced so prompt plus output fits ContextWindowSize) and cap an explicit Max Output Tokens setting at it; see
	 * UN2CLLMPayloadBuilder::ResolveMaxOutputTokens. max_output
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	int32 MaxOutputTokens = 16000;

	/** Whether this model supports system/developer prompts. caps.system_prompt */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bSupportsSystemPrompts = true;

	/** Whether requests carry a native structured-output field (StructuredOutputMode is not "none") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bSupportsStructuredOutput = true;

	/**
	 * Native structured-output field: json_schema (OpenAI), response_schema (Gemini), json_object (DeepSeek
	 * without thinking) or none (the prompt alone asks for JSON). caps.structured_output_mode
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	FString StructuredOutputMode;

	/** Whether this is a thinking model whose reasoning counts against its output budget. caps.thinking_model */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bIsReasoningModel = false;

	/** Whether this model requires the OpenAI Responses API instead of Chat Completions. caps.responses_only */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bRequiresResponsesAPI = false;

	/**
	 * Whether the provider accepts sampling parameters (temperature, top_p, top_k) for this model.
	 * When false the payload builder sends none of them, so the provider's own default applies.
	 * Set from provider docs; false when unsure (current Claude, OpenAI reasoning and Gemini 3 models
	 * reject or discourage non-default sampling values). caps.temperature
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bAcceptsSamplingParams = false;

	/** Whether this OpenAI model takes max_completion_tokens instead of max_tokens (MaxTokensField) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bUsesMaxCompletionTokens = false;

	/** Request field that carries the output budget (max_tokens, max_completion_tokens, max_output_tokens, maxOutputTokens). caps.max_tokens_field */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	FString MaxTokensField;

	/**
	 * Thinking control sent with each request, for providers that switch thinking per request on one model ID
	 * (DeepSeek: "none" sends thinking disabled; "low", "high" or "max" sends thinking enabled with that
	 * reasoning_effort). Empty: the provider takes no such control, or its default applies.
	 * Entries may share a ModelId when they differ here; ModelId plus ReasoningEffort is unique per entry.
	 * caps.reasoning_effort
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	FString ReasoningEffort;

	/**
	 * The provider's thinking-depth parameter for this model (reasoning_effort, effort, thinking_level, thinking),
	 * or "none". Descriptive: only DeepSeek's is sent today. caps.reasoning_param
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	FString ReasoningParam;

	/** Levels the provider documents for ReasoningParam; empty when it lists none. caps.reasoning_levels */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	TArray<FString> ReasoningLevels;

	/** Whether the provider can stream this model's responses (requests are not streamed yet). caps.streaming */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bSupportsStreaming = true;

	// === Generation Defaults ===

	/** Default temperature for this model, sent only when bAcceptsSamplingParams is true. caps.default_temperature */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	float DefaultTemperature = 0.0f;

	// === Metadata (catalog field: lifecycle, is_default) ===

	/**
	 * Whether the provider has deprecated this model: it is retired already, or it has an announced shutdown date
	 * (lifecycle.status deprecated or retired). A default model is never deprecated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	bool bIsDeprecated = false;

	/**
	 * Whether the provider has shut this model down, so every request to it fails (lifecycle.status retired).
	 * Retired models stay in their enum (UMETA(Hidden)) so saved settings still load; they are not offered in
	 * Project Settings, the context window model picker or get-available-llm-providers, and a saved retired
	 * selection moves to the default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	bool bIsRetired = false;

	/** The provider's shutdown date (YYYY-MM-DD), past or announced; empty when none is known. lifecycle.shutdown_date */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	FString ShutdownDate;

	/** Whether this is its provider's default model (one per provider). is_default */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Metadata")
	bool bIsDefault = false;

	/** Default constructor */
	FN2CModelMetadata() = default;

	/** Full initialization constructor (tests and the compiled-in fallback rows) */
	FN2CModelMetadata(
		const FString& InModelId,
		const FString& InDisplayName,
		EN2CLLMProvider InProvider,
		float InInputCost,
		float InOutputCost,
		int32 InContextWindow = 128000,
		int32 InMaxOutput = 16000,
		bool bInSupportsSystem = true,
		bool bInSupportsStructured = true,
		bool bInIsReasoning = false,
		float InDefaultTemp = 0.0f,
		bool bInRequiresResponsesAPI = false,
		bool bInAcceptsSamplingParams = false,
		bool bInUsesMaxCompletionTokens = false
	)
		: ModelId(InModelId)
		, DisplayName(InDisplayName)
		, Provider(InProvider)
		, InputCostPer1M(InInputCost)
		, OutputCostPer1M(InOutputCost)
		, ContextWindowSize(InContextWindow)
		, MaxOutputTokens(InMaxOutput)
		, bSupportsSystemPrompts(bInSupportsSystem)
		, bSupportsStructuredOutput(bInSupportsStructured)
		, bIsReasoningModel(bInIsReasoning)
		, bRequiresResponsesAPI(bInRequiresResponsesAPI)
		, bAcceptsSamplingParams(bInAcceptsSamplingParams)
		, bUsesMaxCompletionTokens(bInUsesMaxCompletionTokens)
		, DefaultTemperature(InDefaultTemp)
		, bIsDeprecated(false)
	{
	}

	/** Marks a model the provider has shut down (an empty date when the provider no longer lists one) */
	FN2CModelMetadata& Retired(const TCHAR* InShutdownDate)
	{
		bIsDeprecated = true;
		bIsRetired = true;
		ShutdownDate = InShutdownDate;
		return *this;
	}

	/** Sets the per-request thinking control (see ReasoningEffort) */
	FN2CModelMetadata& WithReasoningEffort(const TCHAR* InReasoningEffort)
	{
		ReasoningEffort = InReasoningEffort;
		return *this;
	}

	/** Whether ReasoningEffort turns thinking on ("low", "high", "max"; not empty or "none") */
	bool IsThinkingEnabled() const
	{
		return !ReasoningEffort.IsEmpty() && ReasoningEffort != TEXT("none");
	}

	/** Marks a model the provider still serves but will shut down on the given date */
	FN2CModelMetadata& ShutsDown(const TCHAR* InShutdownDate)
	{
		bIsDeprecated = true;
		bIsRetired = false;
		ShutdownDate = InShutdownDate;
		return *this;
	}
};

/** How an OpenAI-compatible host is asked for JSON (the dialect writes response_format accordingly) */
UENUM(BlueprintType)
enum class EN2CStructuredOutputMode : uint8
{
	/** response_format json_schema with strict: true (vLLM, LM Studio, OpenRouter, Groq, ...) */
	JsonSchemaStrict UMETA(DisplayName = "JSON Schema (strict)"),
	/** response_format json_schema without strict */
	JsonSchema UMETA(DisplayName = "JSON Schema"),
	/** response_format json_object: JSON, no schema (DeepSeek) */
	JsonObject UMETA(DisplayName = "JSON Object"),
	/** No response_format: the prompt alone asks for JSON */
	PromptOnly UMETA(DisplayName = "Prompt Only")
};

/** The field an OpenAI-compatible host takes the output budget in */
UENUM(BlueprintType)
enum class EN2CMaxTokensField : uint8
{
	MaxTokens UMETA(DisplayName = "max_tokens"),
	MaxCompletionTokens UMETA(DisplayName = "max_completion_tokens")
};

/**
 * One provider row of the model catalog: how to reach a provider. Cards that add providers read these rows.
 *
 * Rows whose dialect is openai_chat and whose Provider is OpenAICompatible, DeepSeek or LM Studio are presets of the
 * one OpenAI-compatible adapter (FN2COpenAIChatDialect, card 0218): everything that differs between those hosts is
 * data here (output format, budget field, thinking fragments, headers, key), not code.
 */
struct NODETOCODE_API FN2CProviderProfile
{
	/** Catalog provider ID ("openai", "anthropic", "gemini", "deepseek", "ollama", "lmstudio") */
	FName Id;
	FString DisplayName;
	/** Default wire dialect of the provider's models (see FN2CModelMetadata::Dialect) */
	FString Dialect;
	/** Default base URL (scheme and host) and request path; a user endpoint setting overrides them */
	FString BaseUrl;
	FString Path;
	/** How the API key is sent: bearer, x-api-key, x-goog-api-key or none */
	FString AuthStyle;
	bool bIsLocal = false;
	bool bNeedsKey = true;
	/** Path of the provider's model list endpoint (live discovery, not used yet) */
	FString ModelsPath;
	/** The EN2CLLMProvider value whose service sends this provider's requests */
	EN2CLLMProvider Provider = EN2CLLMProvider::OpenAI;

	// OpenAI-compatible adapter fields (catalog: structured_output, max_tokens_field, thinking_on, thinking_off,
	// reasoning_effort_field, reasoning_fields, extra_headers, extra_body, secret_id, api_key_env, default_model,
	// context_discovery); every one is optional

	EN2CStructuredOutputMode StructuredOutput = EN2CStructuredOutputMode::JsonSchemaStrict;
	EN2CMaxTokensField MaxTokensField = EN2CMaxTokensField::MaxTokens;

	/** Merged into the body when the request asks for thinking on or off (DeepSeek thinking, vLLM chat_template_kwargs, OpenRouter reasoning) */
	TSharedPtr<FJsonObject> ThinkingOn;
	TSharedPtr<FJsonObject> ThinkingOff;

	/** Where a thinking level (low, high, max) goes when thinking is on, empty for none (DeepSeek: reasoning_effort) */
	FString ReasoningEffortField;

	/** Message fields that hold the model's reasoning (read and logged, never parsed as the answer) */
	TArray<FString> ReasoningFields;

	/** Sent with every request, in order (OpenRouter's attribution headers) */
	TArray<TPair<FString, FString>> ExtraHeaders;

	/** Merged into every body (LM Studio: stream false) */
	TSharedPtr<FJsonObject> ExtraBody;

	/** The secret store entry the key is read from (FN2CSecretStore id), empty for a host that takes none */
	FString SecretId;

	/** Environment variables read for the key when the store has none, first set wins */
	TArray<FString> ApiKeyEnvironmentVariables;

	/** The model sent when the settings name none (a host that serves one model, or a sensible first pick) */
	FString DefaultModel;

	/** How the context window is learnt: models (the /models list), none, ... (informational until card 0216) */
	FString ContextDiscovery;

	/** BaseUrl (trailing slash dropped) followed by Path */
	FString GetEndpoint() const;

	/** The header the key is sent in for AuthStyle: Authorization (bearer), x-api-key, x-goog-api-key, api-key; empty for none */
	FString GetKeyHeaderName() const;

	/** For openai_chat rows: reads the adapter fields above from a catalog or profile JSON row; false and OutError on a bad value */
	static bool ReadAdapterFields(const FJsonObject& Row, FN2CProviderProfile& InOut, FString& OutError);

	/** Lower-case names as the catalog writes them */
	static FString StructuredOutputToString(EN2CStructuredOutputMode Mode);
	static bool StructuredOutputFromString(const FString& Name, EN2CStructuredOutputMode& Out);
};

/**
 * A profile of your own for an OpenAI-compatible host (Editor Preferences > Node to Code (User) > Custom Provider
 * Profiles), kept per user. Becomes an FN2CProviderProfile with ToProfile.
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CCustomProviderProfile
{
	GENERATED_BODY()

	/** What Provider Profile names this profile by (letters, digits, - and _); must not be a catalog row ID */
	UPROPERTY(EditAnywhere, Category = "Profile")
	FString Id;

	UPROPERTY(EditAnywhere, Category = "Profile")
	FString DisplayName;

	/** Base URL up to and including the version, for example http://localhost:8000/v1 */
	UPROPERTY(EditAnywhere, Category = "Profile")
	FString BaseUrl = TEXT("http://localhost:8000/v1");

	/** Request path after the base URL */
	UPROPERTY(EditAnywhere, Category = "Profile")
	FString Path = TEXT("/chat/completions");

	/** How the key is sent: bearer (Authorization: Bearer), api-key (Azure), x-api-key, or none */
	UPROPERTY(EditAnywhere, Category = "Profile", meta = (GetOptions = "N2CUserSettings.GetAuthStyleOptions"))
	FString AuthStyle = TEXT("bearer");

	/** Environment variable holding the key, for a host that needs one (the key is never stored in this profile) */
	UPROPERTY(EditAnywhere, Category = "Profile")
	FString ApiKeyEnvironmentVariable;

	/** The model sent when Model is empty in the OpenAI-Compatible settings */
	UPROPERTY(EditAnywhere, Category = "Profile")
	FString DefaultModel;

	UPROPERTY(EditAnywhere, Category = "Profile")
	EN2CStructuredOutputMode StructuredOutput = EN2CStructuredOutputMode::JsonSchemaStrict;

	UPROPERTY(EditAnywhere, Category = "Profile")
	EN2CMaxTokensField MaxTokensField = EN2CMaxTokensField::MaxTokens;

	/** Extra headers sent with every request (no keys here: use the environment variable) */
	UPROPERTY(EditAnywhere, Category = "Profile")
	TMap<FString, FString> ExtraHeaders;

	/** As a provider row (Provider OpenAICompatible, dialect openai_chat) */
	FN2CProviderProfile ToProfile() const;
};

/**
 * The model catalog: provider and model rows keyed by string ID, loaded from the bundled data file
 * Content/Data/N2CModelCatalog.json, so adding, repricing or retiring a model is a data change.
 *
 * - A model is identified by provider ID plus model ID. Rows may share a model ID when they differ in
 *   ReasoningEffort (DeepSeek thinking modes); the ID alone finds the first live row, which sends the provider's
 *   default mode.
 * - A missing or malformed file logs an error and leaves a compiled-in list with each cloud provider's default
 *   model (IsFallback() is then true).
 * - Retired enumerators of the legacy model enums are always present as retired rows (compiled in), so saved
 *   settings that name them still resolve and migrate.
 * FN2CLLMModelRegistry wraps the catalog for the enum-keyed callers.
 */
class NODETOCODE_API FN2CModelCatalog
{
public:
	/** The catalog, loading the bundled file on first use */
	static FN2CModelCatalog& Get();

	/** Full path of the bundled catalog file (Content/Data/N2CModelCatalog.json under the plugin) */
	static FString GetBundledPath();

	/** Catalog provider ID for a provider enum value ("openai", ..., "lmstudio") */
	static FName ProviderIdFor(EN2CLLMProvider Provider);

	/** Every provider row */
	TConstArrayView<FN2CProviderProfile> Providers() const { return ProviderRows; }

	const FN2CProviderProfile* FindProvider(FName ProviderId) const;

	/** The rows the OpenAI-Compatible provider offers (Provider OpenAICompatible), in catalog order */
	TArray<const FN2CProviderProfile*> OpenAICompatiblePresets() const;

	/** A provider's models in catalog order; retired rows only when asked for */
	TArray<const FN2CModelMetadata*> List(FName ProviderId, bool bIncludeRetired = false) const;

	/** Every row, retired ones included, in catalog order */
	TConstArrayView<FN2CModelMetadata> AllModels() const { return Models; }

	/** The row for a provider's model ID: the first live one when several share the ID; null when unknown */
	const FN2CModelMetadata* Find(FName ProviderId, const FString& ModelId) const;

	/** The row with both the model ID and the thinking control; Find(ProviderId, ModelId) when none has it */
	const FN2CModelMetadata* Find(FName ProviderId, const FString& ModelId, const FString& ReasoningEffort) const;

	/** The row for a model ID under any provider (first live row, then a retired one); null when unknown */
	const FN2CModelMetadata* FindById(const FString& ModelId, const FString& ReasoningEffort = FString()) const;

	/** The row a saved legacy enumerator name selects (for example "Claude5_5_Sonnet"); null when none */
	const FN2CModelMetadata* FindByLegacyEnum(FName ProviderId, const FString& EnumName) const;

	/** The provider's default model row; null for a provider without models (Ollama, LM Studio) */
	const FN2CModelMetadata* FindDefault(FName ProviderId) const;

	/** Whether the file failed to load and only the compiled-in default rows are present */
	bool IsFallback() const { return bIsFallback; }

	/** The file the rows came from (empty in fallback) */
	const FString& GetSourcePath() const { return SourcePath; }

	/** Increases on every load; FN2CLLMModelRegistry rebuilds when it changes */
	uint32 GetGeneration() const { return Generation; }

	/**
	 * Replaces the rows with those of a catalog file. A missing or malformed file logs an error and leaves the
	 * compiled-in fallback rows. Returns whether the file loaded. Game thread only: pointers from earlier lookups
	 * become invalid.
	 */
	bool LoadFromFile(const FString& Path);

	/** As LoadFromFile, from JSON text; SourceLabel names it in log lines */
	bool LoadFromString(const FString& JsonText, const FString& SourceLabel);

	/** Loads the bundled file again (tests call this to restore the shipped catalog) */
	bool ReloadBundled() { return LoadFromFile(GetBundledPath()); }

private:
	FN2CModelCatalog() = default;

	/** Parses JsonText into the out arrays; false with a reason when it is malformed */
	static bool Parse(const FString& JsonText, TArray<FN2CProviderProfile>& OutProviders, TArray<FN2CModelMetadata>& OutModels, FString& OutError);

	/** Sets the rows (and appends the compiled-in retired rows) */
	void Install(TArray<FN2CProviderProfile>&& InProviders, TArray<FN2CModelMetadata>&& InModels, const FString& InSourcePath, bool bInFallback);

	TArray<FN2CProviderProfile> ProviderRows;
	TArray<FN2CModelMetadata> Models;
	FString SourcePath;
	bool bIsFallback = false;
	bool bIsLoaded = false;
	uint32 Generation = 0;
};
