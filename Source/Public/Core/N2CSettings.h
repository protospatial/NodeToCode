// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Engine/DeveloperSettings.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2COllamaConfig.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "Models/N2CLogging.h"
#include "Core/Style/N2CUIColors.h"
#include "N2CSettings.generated.h"

struct FN2CModelMetadata;

USTRUCT(BlueprintType)
struct FN2CCodeEditorColors
{
    GENERATED_BODY()

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Normal Text"))
    FColor NormalText = FColor(0xffd6d6d6);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Operators"))
    FColor Operators = FColor(0xffe87d3e);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Keywords"))
    FColor Keywords = FColor(0xff9e86c8);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Strings"))
    FColor Strings = FColor(0xffe5b567);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Numbers"))
    FColor Numbers = FColor(0xff1c33ff);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Comments"))
    FColor Comments = FColor(0xff797979);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Preprocessor"))
    FColor Preprocessor = FColor(0xfff75340);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Parentheses"))
    FColor Parentheses = FColor(0xff00bfff);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Curly Braces"))
    FColor CurlyBraces = FColor(0xffe87d3e);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Square Brackets"))
    FColor SquareBrackets = FColor(0xff98fb98);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Colors", meta = (DisplayName = "Background"))
    FColor Background = FColor(0xff1e1e1e);  // Dark gray default, similar to VS Code
};

USTRUCT(BlueprintType)
struct FN2CCodeEditorThemes
{
    GENERATED_BODY()

    /**
     * The code editor's colors. One theme, "Unreal Engine", is drawn (UN2CSettings::GetThemeColors); the map is fixed
     * size in Project Settings so no entry can be added that nothing reads (card 0332). The map type stays so saved
     * config still loads.
     */
    UPROPERTY(config, EditAnywhere, EditFixedSize, Category = "Themes", meta = (DisplayName = "Themes",
        ToolTip = "The code editor's syntax highlighting colors"))
    TMap<FName, FN2CCodeEditorColors> Themes;

    FN2CCodeEditorThemes()
    {
        FN2CCodeEditorColors UnrealEngineTheme;
        UnrealEngineTheme.Background = FColor(0xff242424); // Dark charcoal background matching UE5 editor
        UnrealEngineTheme.NormalText = FColor(0xffc0c0c0); // Soft silver for comfortable reading
        UnrealEngineTheme.Keywords = FColor(0xff0070e0); // Accent blue for emphasis on language keywords
        UnrealEngineTheme.Operators = FColor(0xffA8A8A8); // Accent orange for clear operator visibility
        UnrealEngineTheme.Strings = FColor(0xffffb800); // UE gold for string literals
        UnrealEngineTheme.Numbers = FColor(0xff8bc24a); // Primary UE blue for numerical values
        UnrealEngineTheme.Comments = FColor(0xff484848); // Darker grey for subtle comments
        UnrealEngineTheme.Preprocessor = FColor(0xffff4040); // Accent red for preprocessor directives
        UnrealEngineTheme.Parentheses = FColor(0xff0097E0); // Primary UE blue for grouping
        UnrealEngineTheme.CurlyBraces = FColor(0xfffe9b07); // Accent orange matching operators
        UnrealEngineTheme.SquareBrackets = FColor(0xff26bbff); // Single green element for visual interest
        Themes.Add("Unreal Engine", UnrealEngineTheme);
    }
};

/**
 * The four enum-keyed price maps settings saved before version 4 (OpenAIModelPricing, AnthropicModelPricing,
 * GeminiModelPricing, DeepSeekModelPricing), read from the config by UN2CSettings::ReadLegacyPricingMaps with the
 * engine's own text import so the saved format is parsed exactly as LoadConfig parsed it. Not a setting.
 */
USTRUCT()
struct NODETOCODE_API FN2CLegacyPricingMaps
{
    GENERATED_BODY()

    UPROPERTY()
    TMap<EN2COpenAIModel, FN2COpenAIPricing> OpenAIModelPricing;

    UPROPERTY()
    TMap<EN2CAnthropicModel, FN2CAnthropicPricing> AnthropicModelPricing;

    UPROPERTY()
    TMap<EN2CGeminiModel, FN2CGeminiPricing> GeminiModelPricing;

    UPROPERTY()
    TMap<EN2CDeepSeekModel, FN2CDeepSeekPricing> DeepSeekModelPricing;
};

/** What UN2CSettings::MigrateModelSelectionsToIds found and did; every list holds one readable entry per item */
struct NODETOCODE_API FN2CModelIdMigrationReport
{
    /** Selections converted to their catalog ID ("AnthropicModel=Claude4_5_Sonnet -> claude-sonnet-4-5-20250929") */
    TArray<FString> Converted;

    /** Retired selections moved to the provider default ("OpenAI_Model=GPT_o1 (o1, shut down 2026-10-23) -> gpt-6.1-sol") */
    TArray<FString> Retired;

    /** Names this build's enum lacks, reset to the provider default ("Gemini_Model=Gemini_2_5_ProExp -> gemini-3.8-flash") */
    TArray<FString> Removed;

    /** Names the catalog does not list, kept as the Custom Model ID ("OpenAI_Model=GPT_5 -> custom gpt-5") */
    TArray<FString> Custom;

    /** Prices re-keyed by model ID ("AnthropicModelPricing[Claude4_5_Sonnet] -> claude-sonnet-4-5-20250929 1/2") */
    TArray<FString> Prices;

    /** Prices dropped: the 1.2.x cleanup (below version 3), retired models, or a second entry for the same ID */
    TArray<FString> DroppedPrices;

    /** Price maps the engine could not import (they name a value this build lacks); their entries are lost */
    TArray<FString> UnreadablePriceMaps;

    /** New properties the conversion set and that must be saved (AnthropicModelId, ModelPricing, ...) */
    TArray<FName> ChangedProperties;

    /** Old keys found in the config (AnthropicModel, OpenAIModelPricing, ...), to remove once the new ones are saved */
    TArray<FString> LegacyKeys;

    /** True when a saved model name is one only NodeToCode 1.2.x wrote (a trace of 1.2.x in the project) */
    bool bFound12xNames = false;

    /** True when the conversion ran (SettingsVersion below 4 and an old key present) */
    bool bRan = false;

    /** True when the catalog file did not load: converted in memory only, the caller must not save or stamp */
    bool bDeferred = false;
};

/** Where UN2CSettings::MigrateLegacy12Settings reads and writes; the editor uses ForProject(), tests temp files */
struct NODETOCODE_API FN2CLegacyMigrationTargets
{
    /** Config file name in GConfig that the saved settings are read from (the class config in the editor) */
    FString ReadConfig;

    /** Ini file on disk that migrated values and SettingsVersion are written to (Config/DefaultNodeToCode.ini) */
    FString WriteIni;

    /** True when WriteIni is the class's default config, so GConfig is reloaded after writing as the engine does */
    bool bReloadGConfigAfterWrite = false;

    /** The project's Config/DefaultEngine.ini on disk, where 1.2.x wrote its HTTP timeouts */
    FString EngineIni;

    /** Editor Preferences > Performance > Use Less CPU when in Background, which 1.2.x turned off on every start */
    bool bThrottleCPUWhenNotForeground = true;

    /** The current project's files; bThrottleCPUWhenNotForeground stays true (the caller reads the preference) */
    static FN2CLegacyMigrationTargets ForProject();
};

/** What UN2CSettings::MigrateLegacy12Settings found and did; every list holds one readable entry per item */
struct NODETOCODE_API FN2CLegacyMigrationReport
{
    /** Properties written to WriteIni (SettingsVersion included when it was stamped) */
    TArray<FString> PersistedProperties;

    /**
     * Saved values 2.0 cannot load, which kept the 2.0 value instead: enums as "Key=Old -> New", other properties
     * (a pricing map keyed by a removed model) by name
     */
    TArray<FString> ResetEnumValues;

    /** Pricing entries dropped so the model registry's prices apply */
    TArray<FString> DroppedPrices;

    /** 1.2.x per-language theme keys still in the config (CPPThemes, ...); 2.0 does not read them */
    TArray<FString> LegacyThemeKeys;

    /** "[HTTP] Key=Value" entries removed from EngineIni */
    TArray<FString> RemovedEngineKeys;

    /** The version 4 conversion of enum model selections and price maps to string IDs (MigrateModelSelectionsToIds) */
    FN2CModelIdMigrationReport ModelIds;

    /** Keys of properties 2.0 no longer has that were removed from WriteIni (AnthropicModel, OpenAIModelPricing, ...) */
    TArray<FString> RemovedLegacyKeys;

    /** True when the one-time 1.2.x steps ran (SettingsVersion was below 3) */
    bool bRanLegacySteps = false;

    /** True when the CPU-throttle notice was logged */
    bool bReportedThrottle = false;

    /** True when SettingsVersion was written as CurrentSettingsVersion */
    bool bStampedVersion = false;

    /** True when WriteIni could not be written (read-only or missing folder) */
    bool bWriteFailed = false;
};


// Questions? Check out the Docs: github.com/protospatial/NodeToCode/wiki
UCLASS(Config = NodeToCode, DefaultConfig, meta = (Category = "Node to Code", DisplayName = "Node to Code"))
class NODETOCODE_API UN2CSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UN2CSettings();

    // Begin UDeveloperSettings Interface
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FText GetSectionText() const override;
    // End UDeveloperSettings Interface

    /** Selected LLM provider */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Provider")
    EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

    /**
     * Seconds a translation request may wait without receiving any data from the provider before it fails with a
     * Timeout error (the HTTP activity timeout). Requests are not streamed: the provider sends nothing until the whole
     * answer is ready, so raise this for very long translations or slow local models. The whole request is also
     * limited to 3600 s. Applies to the next request.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Provider",
        meta = (DisplayName = "HTTP Activity Timeout (s)", ClampMin = "1", ClampMax = "3600", UIMin = "30", UIMax = "3600",
            ToolTip = "Seconds a translation request may go without receiving data before it fails with a Timeout error. Responses are not streamed, so the provider sends nothing until the answer is complete: raise this for very long translations or slow local models. Applies to the next request."))
    float HttpActivityTimeoutSeconds = 300.0f;

    /** Anthropic model ID - claude-sonnet-5-5 recommended for the best quality/cost balance, claude-opus-5-5 for the most capability, claude-haiku-5-5 for the lowest cost */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "Model", GetOptions = "GetAnthropicModelOptions",
            ToolTip = "Model ID sent to Anthropic, chosen from the model catalog (Content/Data/N2CModelCatalog.json). claude-sonnet-5-5 is recommended for the best quality/cost balance, claude-opus-5-5 for the most capability, claude-haiku-5-5 for the lowest cost. Custom Model ID, when set, is sent instead."))
    FString AnthropicModelId = N2CDefaultModelIds::Anthropic;

    /** Anthropic model ID sent instead of Model when set (a model the catalog does not list) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "Custom Model ID", ToolTip = "A model ID the catalog does not list (for example a model released after this NodeToCode version). When set it is sent instead of Model, without the catalog's limits and prices (add one under Model Price Overrides to see costs). Leave empty to use Model."))
    FString AnthropicCustomModelId;
    
    /**
     * Anthropic API key from console.anthropic.com. Write-only: an entered key is saved through FN2CSecretStore (Windows
     * Credential Manager) and the field is cleared, so it never holds a key. NodeToCode needs an API key:
     * a Claude Pro/Max subscription cannot be used here. To translate with a subscription, use Claude Code with the
     * NodeToCode MCP server (see the Provider Configuration wiki page).
     */
    UPROPERTY(EditAnywhere, Transient, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "API Key", PasswordField = true,
            ToolTip = "Type or paste a key and press Enter: it is saved to Windows Credential Manager (the per-user secrets file on other platforms) and the field empties again. The key is never shown, saved to a config file, or readable from Blueprints or Python; API Key Status says where the key in use comes from."))
    FString Anthropic_API_Key_UI;

    /**
     * Where the Anthropic key that requests use comes from (DescribeApiKeySource): a key saved with the API Key field, or the
     * ANTHROPIC_API_KEY environment variable of the editor process when none is saved, or not set. Shows at most the last
     * four characters of a saved key and never an environment value.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "API Key Status"))
    FString Anthropic_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window (the Messages API requires a value, so one is always sent). Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 AnthropicMaxOutputTokens = 0;

    /** OpenAI model ID - gpt-6.1-sol recommended for the best quality/cost balance, gpt-6-astra for the most capability, gpt-6-luna for the lowest cost */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "Model", GetOptions = "GetOpenAIModelOptions",
            ToolTip = "Model ID sent to OpenAI, chosen from the model catalog (Content/Data/N2CModelCatalog.json). gpt-6.1-sol is recommended for the best quality/cost balance, gpt-6-astra for the most capability, gpt-6-luna for the lowest cost. Custom Model ID, when set, is sent instead."))
    FString OpenAIModelId = N2CDefaultModelIds::OpenAI;

    /** OpenAI model ID sent instead of Model when set (a model the catalog does not list) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "Custom Model ID", ToolTip = "A model ID the catalog does not list (for example a model released after this NodeToCode version). When set it is sent instead of Model, without the catalog's limits and prices (add one under Model Price Overrides to see costs). Leave empty to use Model."))
    FString OpenAICustomModelId;

    /** OpenAI API key. Write-only: an entered key is saved through FN2CSecretStore and the field is cleared */
    UPROPERTY(EditAnywhere, Transient, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "API Key", PasswordField = true,
            ToolTip = "Type or paste a key and press Enter: it is saved to Windows Credential Manager (the per-user secrets file on other platforms) and the field empties again. The key is never shown, saved to a config file, or readable from Blueprints or Python; API Key Status says where the key in use comes from."))
    FString OpenAI_API_Key_UI;

    /**
     * Where the OpenAI key that requests use comes from (DescribeApiKeySource): a key saved with the API Key field, or the
     * OPENAI_API_KEY environment variable of the editor process when none is saved, or not set. Shows at most the last
     * four characters of a saved key and never an environment value.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "API Key Status"))
    FString OpenAI_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window; sent as max_completion_tokens to reasoning models and as max_output_tokens on the Responses API. Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 OpenAIMaxOutputTokens = 0;

    /** Gemini model ID - gemini-3.8-flash recommended for the best price-performance, gemini-4-argon for the most capability, gemini-3.5-flash-lite for the lowest cost */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "Model", GetOptions = "GetGeminiModelOptions",
            ToolTip = "Model ID sent to Gemini, chosen from the model catalog (Content/Data/N2CModelCatalog.json). gemini-3.8-flash is recommended for the best price-performance, gemini-4-argon for the most capability, gemini-3.5-flash-lite for the lowest cost. Custom Model ID, when set, is sent instead."))
    FString GeminiModelId = N2CDefaultModelIds::Gemini;

    /** Gemini model ID sent instead of Model when set (a model the catalog does not list) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "Custom Model ID", ToolTip = "A model ID the catalog does not list (for example a model released after this NodeToCode version). When set it is sent instead of Model, without the catalog's limits and prices (add one under Model Price Overrides to see costs). Leave empty to use Model."))
    FString GeminiCustomModelId;

    /** Gemini API key from Google AI Studio. Write-only: an entered key is saved through FN2CSecretStore and the field is cleared */
    UPROPERTY(EditAnywhere, Transient, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "API Key", PasswordField = true,
            ToolTip = "Type or paste a key and press Enter: it is saved to Windows Credential Manager (the per-user secrets file on other platforms) and the field empties again. The key is never shown, saved to a config file, or readable from Blueprints or Python; API Key Status says where the key in use comes from."))
    FString Gemini_API_Key_UI;

    /**
     * Where the Gemini key that requests use comes from (DescribeApiKeySource): a key saved with the API Key field, or the
     * GEMINI_API_KEY environment variable of the editor process when none is saved, or not set. Shows at most the last
     * four characters of a saved key and never an environment value.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "API Key Status"))
    FString Gemini_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window. Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 GeminiMaxOutputTokens = 0;

    /** DeepSeek model ID - deepseek-flash recommended (Thinking high, or none for the fastest and cheapest), deepseek-v4-pro for the most capability */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "Model", GetOptions = "GetDeepSeekModelOptions",
            ToolTip = "Model ID sent to DeepSeek, chosen from the model catalog (Content/Data/N2CModelCatalog.json). deepseek-flash is recommended, deepseek-v4-pro is the most capable; Thinking sets the thinking mode. Custom Model ID, when set, is sent instead."))
    FString DeepSeekModelId = N2CDefaultModelIds::DeepSeek;

    /** DeepSeek thinking mode sent with each request: none (thinking off, JSON output), or low, high or max effort */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "Thinking", GetOptions = "GetDeepSeekReasoningEffortOptions",
            ToolTip = "Thinking mode sent with each DeepSeek request: none turns thinking off (faster and cheaper, with JSON output); low, high or max turn it on at that reasoning effort. Empty uses the catalog entry's mode for the model."))
    FString DeepSeekReasoningEffort = N2CDefaultModelIds::DeepSeekReasoningEffort;

    /** DeepSeek model ID sent instead of Model when set (a model the catalog does not list) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "Custom Model ID", ToolTip = "A model ID the catalog does not list (for example a model released after this NodeToCode version). When set it is sent instead of Model, with the Thinking mode above but without the catalog's limits and prices (add one under Model Price Overrides to see costs). Leave empty to use Model."))
    FString DeepSeekCustomModelId;
    
    /** DeepSeek API key. Write-only: an entered key is saved through FN2CSecretStore and the field is cleared */
    UPROPERTY(EditAnywhere, Transient, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "API Key", PasswordField = true,
            ToolTip = "Type or paste a key and press Enter: it is saved to Windows Credential Manager (the per-user secrets file on other platforms) and the field empties again. The key is never shown, saved to a config file, or readable from Blueprints or Python; API Key Status says where the key in use comes from."))
    FString DeepSeek_API_Key_UI;

    /**
     * Where the DeepSeek key that requests use comes from (DescribeApiKeySource): a key saved with the API Key field, or the
     * DEEPSEEK_API_KEY environment variable of the editor process when none is saved, or not set. Shows at most the last
     * four characters of a saved key and never an environment value.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "API Key Status"))
    FString DeepSeek_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window. Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 DeepSeekMaxOutputTokens = 0;

    /** Ollama configuration */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Ollama")
    FN2COllamaConfig OllamaConfig;

    /**
     * Layout version of the saved settings. A config saved before versioning has no value and loads as 0, so
     * the migrations for later versions run on it (MigrateSavedSettings). Any edit in Project Settings stamps
     * the current version, which is then saved with the edited values. Not shown in the editor.
     */
    UPROPERTY(Config)
    int32 SettingsVersion = 0;

    /**
     * 1: Ollama options are sent only when set (override toggles, num_ctx and num_predict 0 = omitted).
     * 2: per-provider Max Output Tokens (0 = Auto); a LMStudioMaxTokens value saved by the PR #20 build carries over.
     * 3: settings left by NodeToCode 1.2.x cleaned up once at editor start (MigrateLegacy12Settings), and this
     *    version written to the config so it does not run again.
     * 4: model selections and price overrides stored by string model ID (AnthropicModelId, ..., ModelPricing) instead
     *    of the model enums (AnthropicModel, ..., AnthropicModelPricing): MigrateModelSelectionsToIds converts a saved
     *    config, and MigrateLegacy12Settings writes the result once at editor start and removes the old keys.
     */
    static constexpr int32 CurrentSettingsVersion = 4;

    /** The version whose migration stores model selections and prices by string model ID */
    static constexpr int32 ModelIdSettingsVersion = 4;
    
    /** Ollama Model Selection */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Ollama",
        meta=(DisplayName="Model Name"))
    FString OllamaModel = "qwen3:32b";
    
    /** LM Studio Model Selection */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta=(DisplayName="Model Name"))
    FString LMStudioModel = "qwen3-32b";
    
    /** LM Studio Endpoint - Default is localhost:1234 */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta=(DisplayName="Server Endpoint"))
    FString LMStudioEndpoint = "http://localhost:1234";
    
    /** LM Studio Prepended Model Command - Text to prepend to user messages (e.g., '/no_think' to disable thinking for reasoning models, or other model-specific commands). This text will appear at the start of each user message. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta=(DisplayName="Prepended Model Command", 
              ToolTip="Text to prepend to user messages (e.g., '/no_think' to disable thinking for reasoning models, or other model-specific commands). This text will appear on first line of each user message."))
    FString LMStudioPrependedModelCommand = "";

    /** Output token budget per request; 0 = Auto sends none, so the server decides (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: no max_tokens is sent, so the server uses the context the prompt leaves (needed for servers with a small window, such as vLLM with max_model_len 16384). Any other value is sent as-is; keep prompt plus this value inside the model's loaded context length."))
    int32 LMStudioMaxOutputTokens = 0;
    
    /**
     * Price overrides per model ID (USD per 1M tokens). A model without an entry costs its catalog price; Ollama and
     * LM Studio are free. Read by the cost estimates and GetCurrentInputCost / GetCurrentOutputCost.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing",
        meta = (DisplayName = "Model Price Overrides", GetKeyOptions = "GetModelPricingKeyOptions",
            ToolTip = "Price overrides per model ID, in USD per 1M input and output tokens. A model without an entry uses its catalog price (Content/Data/N2CModelCatalog.json); local providers are free."))
    TMap<FString, FN2CModelPricing> ModelPricing;
    
    /** Target programming language for translation */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation", 
        meta=(DisplayName="Target Language"))
    EN2CCodeLanguage TargetLanguage = EN2CCodeLanguage::Cpp;

    /** Maximum depth for nested graph translation (0 = No nested translation). This setting can greatly impact costs and context window utilization, so be mindful! */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta=(DisplayName="Max Translation Depth", ClampMin="0", ClampMax="5", UIMin="0", UIMax="5"))
    int32 TranslationDepth = 0;
    
    /** Minimum severity level for logging */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Logging")
    EN2CLogSeverity MinSeverity = EN2CLogSeverity::Info;

    /** MCP server port (Model Context Protocol HTTP server, http://127.0.0.1:<port>/mcp). When another program
     * already uses it, the server takes the next free port in Port Fallback Range and says so in a notification.
     * Changing it restarts the server on the new port. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Server Port", ClampMin="1024", ClampMax="65535", UIMin="1024", UIMax="65535",
              ToolTip="Port of the MCP server (http://127.0.0.1:<port>/mcp). If another program already uses it, the server takes the next free port in Port Fallback Range and shows a notification. Changing it restarts the server on the new port."))
    int32 McpServerPort = 27000;

    /** How many ports, starting at Server Port, the MCP server tries when a port is already in use (1 = only Server
     * Port). If none is free the server does not start and an error notification says so. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Port Fallback Range", ClampMin="1", ClampMax="100", UIMin="1", UIMax="100",
              ToolTip="How many ports, starting at Server Port, the MCP server tries when a port is already in use (1 = only Server Port). If none is free the server does not start and an error notification says so."))
    int32 McpServerPortRange = 10;

    /** Where the MCP server is listening now (set by the server; not saved) */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Server Status", ToolTip="Where the MCP server is listening now, or why it is not running."))
    FString McpServerStatus = TEXT("Not started");
    
    /** Which toolsets the MCP server serves (decision 0005, card 0165). Replaces Enable Python Script-Only Mode, whose
     * saved True loads as Python Scripting Only. Read when the server starts: restart the editor after a change. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Tool Profile",
              ToolTip="Which toolsets the MCP server serves. Default Toolsets: every toolset. Python Scripting Only: run-python and the script library tools. Custom: the toolsets in Custom Toolsets. Requires an editor restart."))
    EN2CMcpToolProfile McpToolProfile = EN2CMcpToolProfile::DefaultToolsets;

    /** The toolsets the Custom tool profile serves, by name (BlueprintGraphTools, TranslationTools, ...) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Custom Toolsets", GetOptions="GetMcpToolsetOptions",
              EditCondition="McpToolProfile == EN2CMcpToolProfile::Custom", EditConditionHides,
              ToolTip="The toolsets the Custom tool profile serves. Requires an editor restart."))
    TArray<FString> McpCustomToolsets;

    /** How tools/list shows the served tools. Tool Search lists only list_toolsets, describe_toolset and call_tool (UE
     * 5.8's names), for clients with a tool limit (VS Code 128, Windsurf 100). tools/list never changes while the
     * server runs in either mode. Replaces Enable Dynamic Tool Discovery, whose saved True loads as Tool Search. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Tool Exposure",
              ToolTip="List Every Tool: tools/list holds every tool of the profile. Tool Search: tools/list holds only list_toolsets, describe_toolset and call_tool, which reach the same tools; use it for clients with a tool limit. tools/list never changes while the server runs. Requires an editor restart."))
    EN2CMcpToolExposure McpToolExposure = EN2CMcpToolExposure::ListAllTools;

    /** What the MCP server does with the 34 NodeToCode tools Epic's Unreal MCP also has (EditorToolset, UE 5.8; card
     * 0174). Automatic follows NodeToCode's default (N2CMcpEpicOverlap::bLeaveToUnrealMcpByDefault): with Unreal MCP and
     * EditorToolset enabled, the Default toolsets profile leaves them out, and a Custom profile serves its toolsets as
     * chosen. A saved Leave or Always serve keeps its meaning if that default changes. No effect on UE 5.6 and 5.7. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Overlapping Tools",
              ToolTip="The 34 NodeToCode tools that Epic's Unreal MCP also has on UE 5.8 (Content Browser, components, classes, compile and save, function pins, variable nodes). Automatic: left out of the Default toolsets profile when the Unreal MCP (ModelContextProtocol) and EditorToolset plugins are enabled. Leave Them to Unreal MCP: left out of every profile then. Always Serve Them: served, as on UE 5.6 and 5.7. Requires an editor restart."))
    EN2CMcpOverlappingTools McpOverlappingTools = EN2CMcpOverlappingTools::Automatic;

/** Get the model for the selected provider */
    FString GetActiveModel() const;

/** Fills the API Key Status fields from the secret store and the environment (never shows a key) */
    void RefreshApiKeySources();

    /**
     * The API Key Status text for Provider, from the credential source requests use (IN2CCredentialSource::Get()):
     * "Stored in Windows Credential Manager (ends ...abcd)", "From environment variable ANTHROPIC_API_KEY" or
     * "Not set". At most the last four characters of a saved key, and only of a key 16 or more characters long.
     */
    static FString DescribeApiKeySource(EN2CLLMProvider Provider);

    /**
     * Saves Provider's API key (an empty Key removes the saved one) through FN2CSecretStore, as the API Key field does,
     * and updates the API Key Status rows. With HasApiKey, the only key access Blueprints and Python get: nothing
     * reflected returns a key.
     * @return true if the key was saved (or removed)
     */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Services")
    static bool SetApiKey(EN2CLLMProvider InProvider, const FString& Key);

    /** True when requests to Provider have a key (saved or from the environment), or need none (Ollama, LM Studio) */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    static bool HasApiKey(EN2CLLMProvider InProvider);

    /** Remove Saved Key buttons under each provider in Project Settings */
    UFUNCTION(CallInEditor, Category = "Node to Code | LLM Services | Anthropic", meta = (DisplayName = "Remove Saved Anthropic Key"))
    void RemoveSavedAnthropicKey();
    UFUNCTION(CallInEditor, Category = "Node to Code | LLM Services | OpenAI", meta = (DisplayName = "Remove Saved OpenAI Key"))
    void RemoveSavedOpenAIKey();
    UFUNCTION(CallInEditor, Category = "Node to Code | LLM Services | Gemini", meta = (DisplayName = "Remove Saved Gemini Key"))
    void RemoveSavedGeminiKey();
    UFUNCTION(CallInEditor, Category = "Node to Code | LLM Services | DeepSeek", meta = (DisplayName = "Remove Saved DeepSeek Key"))
    void RemoveSavedDeepSeekKey();

    /**
     * The model ID requests to Provider send: its Custom Model ID when set, else its Model (the provider default when
     * empty); Ollama and LM Studio their model name.
     */
    FString GetActiveModelForProvider(EN2CLLMProvider Provider) const;

    /** Catalog row of Provider's selected model (with the DeepSeek thinking mode); null for a model the catalog lacks */
    const FN2CModelMetadata* GetActiveModelMetadataForProvider(EN2CLLMProvider InProvider) const;

    /** Price per 1M tokens of Provider's selected model: Model Price Overrides entry, else catalog price, else 0 */
    FN2CModelPricing GetActivePricingForProvider(EN2CLLMProvider InProvider) const;

    /**
     * Sets InProvider's selected model to a catalog row: Model to its ID, Thinking to its mode (DeepSeek), and clears
     * the Custom Model ID. Does not save.
     */
    void SelectModel(const FN2CModelMetadata& Row);

    /** Names of a cloud provider's Model and Custom Model ID properties (NAME_None for Ollama and LM Studio) */
    static FName GetModelIdPropertyName(EN2CLLMProvider InProvider);
    static FName GetCustomModelIdPropertyName(EN2CLLMProvider InProvider);

    /** Options of the Custom Toolsets entries: the declared toolset names (N2CMcpToolsets) */
    UFUNCTION()
    static TArray<FString> GetMcpToolsetOptions();

    /** Options of the Model drop-downs (also callable from Blueprints and Python): the provider's live catalog model IDs, plus the current value when it is not one */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    TArray<FString> GetAnthropicModelOptions() const;
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    TArray<FString> GetOpenAIModelOptions() const;
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    TArray<FString> GetGeminiModelOptions() const;
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    TArray<FString> GetDeepSeekModelOptions() const;

    /** Options of DeepSeek Thinking: the levels the catalog lists for the selected model */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    TArray<FString> GetDeepSeekReasoningEffortOptions() const;

    /** Key options of Model Price Overrides: every live catalog model ID, the custom model IDs and the keys already set */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services")
    TArray<FString> GetModelPricingKeyOptions() const;

    /** Thinking control of the selected model's registry entry (see FN2CLLMConfig::ReasoningEffort); empty if none */
    FString GetActiveReasoningEffortForProvider(EN2CLLMProvider InProvider) const;

    /**
     * The provider's Max Output Tokens setting (0 = Auto, see FN2CLLMConfig::MaxOutputTokens). Ollama returns 0:
     * its budget is OllamaConfig.NumPredict, which ConfigureForOllama sends.
     */
    int32 GetMaxOutputTokensForProvider(EN2CLLMProvider InProvider) const;

    /**
     * Sets InOutConfig.Model and InOutConfig.ReasoningEffort for a request to InProvider: the selected model and
     * its thinking mode, or ModelIdOverride. An override naming a different model than the selection leaves
     * ReasoningEffort empty, so that model's own registry entry decides. Also sets InOutConfig.MaxOutputTokens
     * from the provider's Max Output Tokens setting, which an override model gets too (capped at its own maximum),
     * and InOutConfig.ActivityTimeoutSeconds from HTTP Activity Timeout.
     */
    void ResolveRequestModel(EN2CLLMProvider InProvider, const FString& ModelIdOverride, FN2CLLMConfig& InOutConfig) const;

    /** Get the minimum severity level for logging */
    EN2CLogSeverity GetMinLogSeverity() const { return MinSeverity; }

    /** Notify that model settings have changed - call this to update token estimation UI */
    void NotifyModelSettingsChanged();

    /** UI Color palette for all NodeToCode widgets. Widgets read it through N2C::UI::Colors() (Core/Style/N2CUIColors.h). */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming",
        meta=(DisplayName="UI Colors"))
    FN2CUIColors UIColors;

    /** Code editor theme colors */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming",
        meta=(DisplayName="Code Editor Theme"))
    FN2CCodeEditorThemes CodeEditorTheme;
    
    /**
     * The code editor's colors (one theme for every language): the "Unreal Engine" entry of CodeEditorTheme, else its
     * first entry (a renamed key), else the default colors (an emptied map). Never null.
     */
    const FN2CCodeEditorColors* GetThemeColors(EN2CCodeLanguage Language, const FName& ThemeName) const;

    // Begin UObject Interface
    virtual void PostInitProperties() override;
    virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;
    virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    // End UObject Interface

    /**
     * Moves each selected model ID whose catalog row is retired (the provider shut the model down) to that provider's
     * default model, and logs one warning naming every change. A Custom Model ID and an ID the catalog does not list
     * are left alone, so a model released after this build keeps working. Called after the config is loaded
     * (PostInitProperties) and reloaded (PostReloadConfig, which is how a plugin's config branch reaches the settings
     * object at startup). The config file is not rewritten here; MigrateLegacy12Settings saves the new value once at
     * editor start.
     * @return the number of selections changed
     */
    int32 MigrateRetiredModelSelections();

    /**
     * Version 4 migration: converts the model selections and price maps a config saved before version 4 holds as model
     * enum names (AnthropicModel=Claude4_5_Sonnet, AnthropicModelPricing=((Claude4_5_Sonnet, (...)))) to string model
     * IDs on this object (AnthropicModelId=claude-sonnet-4-5-20250929, ModelPricing=(("claude-...", (...)))). Reads the
     * old keys of ConfigFilename through GConfig. Runs only while SettingsVersion is below 4, and converts only into a
     * new key ConfigFilename does not hold yet, so running it again, or on a converted config, changes nothing.
     * - A name with a live catalog row (legacy_enum) becomes that row's ID (and, for DeepSeek, its thinking mode).
     * - A retired row, or a name this build's enum lacks (1.2.3's Gemini_2_5_ProExp), becomes the provider default.
     * - A name the enum has but the catalog does not list keeps its model ID (the enumerator's UMETA Value) as the
     *   Custom Model ID, with a warning, instead of being reset.
     * - Prices are re-keyed by model ID; below version 3 the 1.2.x cleanup (DropLegacyPricing) is applied first, and
     *   entries for retired models are dropped.
     * If the catalog file did not load (FN2CModelCatalog::IsFallback), names are converted in memory from the UMETA
     * Value strings, nothing is reset or dropped, and bDeferred tells the caller not to save, so a broken install
     * never rewrites the project's config. Warnings and the Info line are logged only for values this call changes
     * in memory, so loading and the startup save report each conversion once.
     */
    FN2CModelIdMigrationReport MigrateModelSelectionsToIds(const FString& ConfigFilename);

    /**
     * Reads the four enum-keyed price maps saved before version 4 (OpenAIModelPricing, ...) from ConfigFilename. A map
     * the engine cannot import (it names a value this build's enum lacks) is reported in OutUnreadable and left empty.
     * @param OutKeys the keys found in the config
     */
    static void ReadLegacyPricingMaps(const FString& ConfigFilename, FN2CLegacyPricingMaps& OutMaps, TArray<FString>& OutKeys, TArray<FString>& OutUnreadable);

    /**
     * Runs the migrations for every settings version above SettingsVersion. It does not change SettingsVersion,
     * and every step gives the same result when run again on its own output, so it is safe on each load and
     * reload; SettingsVersion moves to CurrentSettingsVersion only on an edit (PreEditChange), so the config is
     * not rewritten on load. Called from PostInitProperties and PostReloadConfig.
     * @param ConfigFilename config the legacy keys are read from (empty: this class's config)
     * @return the number of values changed
     */
    int32 MigrateSavedSettings(const FString& ConfigFilename = FString());

    /**
     * Maps the MCP tool-mode switches NodeToCode saved before card 0165 onto Tool Profile and Tool Exposure, in memory:
     * bEnablePythonScriptOnlyMode=True becomes Python Scripting Only, and bEnableDynamicToolDiscovery=True becomes Tool
     * Search (unless script-only mode was on too, which took precedence and listed the Python tools directly). A new
     * key ConfigFilename already holds wins, so running it again, or after Project Settings saved the new keys,
     * changes nothing. Called from MigrateSavedSettings on every load; the old keys stay in the file, unread otherwise.
     * @param ConfigFilename config the old keys are read from (empty: this class's config)
     * @param OutChanges one readable entry per value changed
     */
    void MigrateLegacyMcpToolModes(const FString& ConfigFilename, TArray<FString>& OutChanges);

    /**
     * Saves what loading a NodeToCode 1.2.x config changed and cleans up what 1.2.x left behind. The editor calls it
     * once at startup on the class default object (FNodeToCodeModule::StartupModule, not in commandlets).
     * Every start: a saved enum value that loaded as something else (a name 2.0 does not know, which the engine
     *   resets) and a saved model ID MigrateRetiredModelSelections moved off a retired model are written back, so
     *   their warnings are logged once; unknown names get their own warning.
     * Once, while SettingsVersion is below 4 (MigrateModelSelectionsToIds): the model selections and price overrides
     *   converted from model enum names to model IDs are written and the old keys (AnthropicModel, ...,
     *   DeepSeekModelPricing) removed from WriteIni; enum names 2.0 lacks are named in the unknown-value warning. Not
     *   while the model catalog file is missing: then nothing of it is written and the version stays below 4.
     * Once, while SettingsVersion is below 3:
     * - the version 1 and 2 results (Ollama options, LM Studio max tokens) are written, since they stop running once
     *   the version is stamped;
     * - pricing entries for retired or unknown models, and entries still at 1.2.x's built-in price (a Project Settings
     *   save of any map wrote every price), are dropped so the catalog's prices apply; other prices are kept (done by
     *   the version 4 conversion before it re-keys the prices);
     * - the 1.2.x per-language theme keys (CPPThemes ... PseudocodeThemes) are reported in one warning: 2.0 draws
     *   every language with CodeEditorTheme and 1.2.x kept the selected theme name in its UI asset, not in the config,
     *   so they cannot be converted; the keys are left in the ini for the user to copy colours from;
     * - [HTTP] HttpConnectionTimeout=300 and HttpActivityTimeout=3600 are removed from EngineIni (only those values,
     *   which are the ones 1.2.x wrote; other values are the user's and stay);
     * - when the project showed any 1.2.x trace and Use Less CPU when in Background is off, one warning says it is
     *   left off because 1.2.x and a user choice cannot be told apart.
     * When the one-time steps found anything, SettingsVersion is stamped and written, so they do not run again.
     * A config with nothing to migrate is not written. Safe to run again: a second run finds nothing.
     */
    FN2CLegacyMigrationReport MigrateLegacy12Settings(const FN2CLegacyMigrationTargets& Targets);

    /**
     * Drops pricing entries for retired or unknown models and entries equal to 1.2.x's built-in price for that model
     * (NodeToCode 1.2.x filled every map with its prices and saved them all), so the catalog's prices apply. Applied by
     * MigrateModelSelectionsToIds to a config below version 3, before the prices are re-keyed by model ID.
     * @param OutDropped one entry per dropped price
     * @return the names of the maps that changed (OpenAIModelPricing, ...)
     */
    static TArray<FName> DropLegacyPricing(FN2CLegacyPricingMaps& Maps, TArray<FString>& OutDropped);

    /**
     * Removes the HTTP timeouts NodeToCode 1.2.x wrote to the project's DefaultEngine.ini ([HTTP]
     * HttpConnectionTimeout=300, HttpActivityTimeout=3600). A key holding any other value is left alone.
     * @param OutRemoved one "[HTTP] Key=Value" entry per removed key
     * @return false when a key should have been removed but the file could not be written
     */
    static bool RemoveLegacyHttpTimeouts(const FString& EngineIniPath, TArray<FString>& OutRemoved);

    /**
     * Logs, once per editor session, when the config still selects the Claude Pro/Max or Google account sign-in
     * that NodeToCode 2.0 removed. Those settings no longer exist, so the provider already uses its API key; the
     * stale entry is ignored on load and dropped the next time Project Settings saves this section.
     * Called from PostInitProperties and PostReloadConfig with this class's config.
     * @return the providers whose saved setting chose the removed sign-in
     */
    int32 ReportRemovedSignInSelections() const;

    /**
     * The providers ("Anthropic", "Gemini") whose saved settings in ConfigFilename still select the removed
     * sign-in. Reads the config only; does not log.
     */
    static TArray<FString> FindRemovedSignInSelections(const FString& ConfigFilename);

    /**
     * Version 1 Ollama migration. Before 2.0 every Ollama option was always sent, and Project Settings saves the
     * whole struct, so a saved value equal to the old default says nothing about the user's intent: those are
     * cleared (num_ctx 8192 and num_predict 8192 become 0, sampler values at the old default keep their override
     * off, and the old bUseSystemPrompts default false becomes true) so the server and model defaults apply.
     * A saved sampler value that differs from the old default was the user's choice and gets its override toggle
     * turned on, so it is still sent. Values with an override already on are left alone.
     * @param OutChanges one entry per changed value, for the log
     * @return the number of values changed
     */
    static int32 MigrateLegacyOllamaDefaults(FN2COllamaConfig& Config, TArray<FString>& OutChanges);

    /**
     * Version 2 migration. Before it every request sent a fixed 16000 output tokens (no setting), so configs saved
     * then have no Max Output Tokens keys and load as 0 = Auto, which needs no change. The one setting that did
     * exist is LMStudioMaxTokens from the PR #20 build (default 32768, no way to omit the field): a value other
     * than that default was the user's choice and moves to LMStudioMaxOutputTokens; the default itself becomes
     * Auto (nothing to change), since 32768 overflows small local windows. Nothing happens when
     * LMStudioMaxOutputTokens is already set.
     * @param ConfigFilename config holding this class's section (reads only the legacy key)
     * @param OutChanges one entry per changed value, for the log
     * @return the number of values changed
     */
    static int32 MigrateLegacyLMStudioMaxTokens(const FString& ConfigFilename, int32& InOutLMStudioMaxOutputTokens, TArray<FString>& OutChanges);

    /** Check if a property is a color property in our editor colors structs */
    bool IsColorProperty(const FProperty* Property) const;

    /**
     * Input price per 1M tokens of the selected provider's model: its Model Price Overrides entry, else the catalog
     * price (Gemini included), 0 for Ollama, LM Studio and a custom model without an override
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services | Pricing")
    float GetCurrentInputCost() const;

    /** Output price per 1M tokens of the selected provider's model (as GetCurrentInputCost) */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services | Pricing")
    float GetCurrentOutputCost() const;

    /** Token estimate of the reference files (characters / 4 each); reads every file */
    int32 GetReferenceFilesTokenEstimate() const;


    /** Estimated token count from reference files */
    UPROPERTY(Config, VisibleAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta = (DisplayName = "Estimated Reference File Tokens"))
    int32 EstimatedReferenceTokens = 0;

    /** Source files to provide as context to the LLM */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation", 
        meta = (DisplayName = "Reference Source Files",
               FilePathFilter = "C++ Files (*.h;*.cpp)|*.h;*.cpp",
               ToolTip="Source files to include as context in LLM prompts"))
    TArray<FFilePath> ReferenceSourceFilePaths;
    
    /** Custom output directory for translations */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta = (DisplayName = "Custom Translation Output Directory",
               ToolTip="If set, translations will be saved to this directory instead of the default location in Saved/NodeToCode/Translations"))
    FDirectoryPath CustomTranslationOutputDirectory;
    
    /**
     * Drops reference source files that do not exist (one warning per missing path per editor session) and warns
     * once about a missing Custom Translation Output Directory. Runs after the config is loaded (RefreshReferenceFiles);
     * the dropped paths stay in the config file, so a file missing on one machine is not removed for the team.
     */
    void ValidateReferenceSourcePaths();

    /**
     * The one way NodeToCode saves these settings, used by Project Settings edits (PostEditChangeProperty) and by the
     * provider and model picker in the NodeToCode window. Writes each named config property, with SettingsVersion, to
     * Config/DefaultNodeToCode.ini (the file Project Settings writes), removes the same keys from the
     * Saved/Config/<Platform>Editor/NodeToCode.ini layer, which loads after it and would otherwise win at the next
     * start, then reloads GConfig from the files. Names that are not config properties of this class, and array and
     * set properties (Project Settings saves the whole section for those), are skipped.
     * @return false when the project file could not be written (read-only, for example not checked out)
     */
    bool SaveToProjectConfig(TConstArrayView<FName> PropertyNames);

    /** Get the plugin settings */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code", 
        meta = (DisplayName = "Get Node to Code Settings"))
    static UN2CSettings* GetN2CSettings() 
    {
        return GetMutableDefault<UN2CSettings>();
    }

    /** Copy text to system clipboard */                                                                                                                                                                      
    UFUNCTION(BlueprintCallable, Category = "Node to Code | Utilities",                                                                                                                                       
        meta = (DisplayName = "Copy To Clipboard"))                                                                                                                                                           
    static void CopyToClipboard(const FString& Text);

private:

    /** Keep track of the last edited property */
    FProperty* LastEditedProperty;

    void InitializePricing();

    /** Prunes missing reference files and recomputes EstimatedReferenceTokens; run once the config has loaded */
    void RefreshReferenceFiles();

    /** The layout version the values in memory have: what PreEditChange and SaveToProjectConfig stamp before saving */
    int32 GetVersionToSave() const;

    /**
     * Writes these config properties' current values into IniPath, leaving the file's other lines as they are (the
     * same per-key update Project Settings makes), then reloads this class's config into GConfig when asked.
     * KeysToRemove (keys of properties this class no longer has) are deleted from the file; OutRemovedKeys gets those
     * that were there.
     * @return false when IniPath could not be written
     */
    bool WriteConfigProperties(const TArray<FName>& PropertyNames, const FString& IniPath, bool bReloadGConfig,
        const TArray<FString>& KeysToRemove, TArray<FString>* OutRemovedKeys = nullptr);
    
#if WITH_EDITOR
    /** Show a notification that the editor needs to restart */
    void ShowRestartEditorNotification();

    /** Weak pointer to the restart notification */
    TWeakPtr<SNotificationItem> RestartNotificationPtr;
#endif
};
