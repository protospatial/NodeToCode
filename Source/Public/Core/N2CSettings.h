// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Engine/DeveloperSettings.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2COllamaConfig.h"
#include "Models/N2CLogging.h"
#include "Styling/SlateColor.h"
#include "N2CSettings.generated.h"

USTRUCT(BlueprintType)
struct FN2CUIColors
{
    GENERATED_BODY()

    // ── Background Colors ──────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Panel Background"))
    FColor BgPanel = FColor(37, 37, 38);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Panel Background Dark"))
    FColor BgPanelDarker = FColor(26, 26, 26);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Input Background"))
    FColor BgInput = FColor(45, 45, 45);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Hover Background"))
    FColor BgHover = FColor(51, 51, 51);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Overlay Panel Background"))
    FColor BgOverlayPanel = FColor(10, 10, 10);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Button Dark Background"))
    FColor BgButtonDark = FColor(28, 28, 28);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Backgrounds", meta = (DisplayName = "Button Dark Background Selected"))
    FColor BgButtonDarkSelected = FColor(42, 42, 42);

    // ── Button Style Colors ───────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Normal"))
    FColor BtnNormal = FColor(55, 55, 55);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Hovered"))
    FColor BtnHovered = FColor(70, 70, 70);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Pressed"))
    FColor BtnPressed = FColor(40, 40, 40);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Disabled"))
    FColor BtnDisabled = FColor(35, 35, 35);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Foreground"))
    FColor BtnForeground = FColor(204, 204, 204);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Foreground Hovered"))
    FColor BtnForegroundHover = FColor(230, 230, 230);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (DisplayName = "Button Foreground Disabled"))
    FColor BtnForegroundDisabled = FColor(107, 107, 107);

    // ── Border Colors ──────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (DisplayName = "Border"))
    FColor BorderColor = FColor(60, 60, 60);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (DisplayName = "Border Subtle"))
    FColor BorderSubtle = FColor(42, 42, 42);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Borders", meta = (DisplayName = "Fallback Border"))
    FColor FallbackBorder = FColor(28, 28, 28);

    // ── Text Colors ────────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Primary"))
    FColor TextPrimary = FColor(204, 204, 204);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Secondary"))
    FColor TextSecondary = FColor(157, 157, 157);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Muted"))
    FColor TextMuted = FColor(107, 107, 107);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Text Shadow"))
    FColor TextShadow = FColor(15, 15, 15);

    // ── Accent Colors ──────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Orange"))
    FColor AccentOrange = FColor(212, 160, 74);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Orange Dim"))
    FColor AccentOrangeDim = FColor(139, 105, 20);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Green"))
    FColor AccentGreen = FColor(78, 201, 176);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Red"))
    FColor AccentRed = FColor(241, 76, 76);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Gold"))
    FColor AccentGold = FColor(212, 161, 74);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Blue"))
    FColor AccentBlue = FColor(79, 194, 255);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Accents", meta = (DisplayName = "Accent Yellow"))
    FColor AccentYellow = FColor(229, 192, 123);

    // ── Editor Colors ───────────────────────────────────────────────

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Editor", meta = (DisplayName = "Text Highlight"))
    FColor TextHighlight = FColor(5, 77, 0);

    // ── Helper Methods ─────────────────────────────────────────────

    static FLinearColor ToLinear(const FColor& Color)
    {
        return FLinearColor::FromSRGBColor(Color);
    }

    static FLinearColor ToLinearWithAlpha(const FColor& Color, float Alpha)
    {
        FLinearColor Linear = FLinearColor::FromSRGBColor(Color);
        Linear.A = Alpha;
        return Linear;
    }
};

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

    UPROPERTY(config, EditAnywhere, Category = "Themes", meta = (DisplayName = "Themes",
        ToolTip = "Collection of color themes for syntax highlighting"))
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

    /** Anthropic Model Selection - Sonnet 5.5 recommended for the best quality/cost balance, Opus 5.5 for the most capability, Haiku 5.5 for the lowest cost */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Anthropic")
    EN2CAnthropicModel AnthropicModel = N2CDefaultModels::Anthropic;
    
    /**
     * Anthropic API key from console.anthropic.com, stored separately in user secrets. NodeToCode needs an API key:
     * a Claude Pro/Max subscription cannot be used here. To translate with a subscription, use Claude Code with the
     * NodeToCode MCP server (see the Provider Configuration wiki page).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "API Key"))
    FString Anthropic_API_Key_UI;

    /**
     * Where the Anthropic key that requests use comes from: the API Key field above, or the ANTHROPIC_API_KEY environment
     * variable of the editor process when the field is empty. The variable's value is never shown.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "API Key Source"))
    FString Anthropic_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window (the Messages API requires a value, so one is always sent). Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 AnthropicMaxOutputTokens = 0;

    /** OpenAI Model Selection - GPT-6.1 Sol recommended for the best quality/cost balance, GPT-6 Astra for the most capability, GPT-6 Luna for the lowest cost */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | OpenAI")
    EN2COpenAIModel OpenAI_Model = N2CDefaultModels::OpenAI;

    /** OpenAI API Key - Stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "API Key"))
    FString OpenAI_API_Key_UI;

    /**
     * Where the OpenAI key that requests use comes from: the API Key field above, or the OPENAI_API_KEY environment
     * variable of the editor process when the field is empty. The variable's value is never shown.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "API Key Source"))
    FString OpenAI_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window; sent as max_completion_tokens to reasoning models and as max_output_tokens on the Responses API. Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 OpenAIMaxOutputTokens = 0;

    /** Gemini Model Selection - Gemini 3.8 Flash recommended for the best price-performance, Gemini 4 Argon for the most capability, Gemini 3.5 Flash-Lite for the lowest cost */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Gemini")
    EN2CGeminiModel Gemini_Model = N2CDefaultModels::Gemini;

    /** Gemini API key from Google AI Studio, stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "API Key"))
    FString Gemini_API_Key_UI;

    /**
     * Where the Gemini key that requests use comes from: the API Key field above, or the GEMINI_API_KEY environment
     * variable of the editor process when the field is empty. The variable's value is never shown.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "API Key Source"))
    FString Gemini_API_Key_Source;

    /** Output token budget per request; 0 = Auto, from the model's registry entry (see the tooltip) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "Max Output Tokens", ClampMin = "0", UIMin = "0",
            ToolTip = "Most output tokens a translation may use. 0 = Auto: sends the model's maximum output, at most 32000 while requests are not streamed (65536 for DeepSeek thinking modes), reduced so the prompt plus the output fits the model's context window. Any other value is sent as-is, capped at the model's maximum. Thinking tokens count against it on reasoning models."))
    int32 GeminiMaxOutputTokens = 0;

    /** DeepSeek Model Selection - each entry names the model and thinking mode it sends */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | DeepSeek")
    EN2CDeepSeekModel DeepSeekModel = N2CDefaultModels::DeepSeek;
    
    /** DeepSeek API Key - Stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "API Key"))
    FString DeepSeek_API_Key_UI;

    /**
     * Where the DeepSeek key that requests use comes from: the API Key field above, or the DEEPSEEK_API_KEY environment
     * variable of the editor process when the field is empty. The variable's value is never shown.
     */
    UPROPERTY(VisibleAnywhere, Transient, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "API Key Source"))
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
     */
    static constexpr int32 CurrentSettingsVersion = 2;
    
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
    
    /** OpenAI Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | OpenAI", DisplayName = "OpenAI Model Pricing")
    TMap<EN2COpenAIModel, FN2COpenAIPricing> OpenAIModelPricing;

    /** Anthropic Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | Anthropic")
    TMap<EN2CAnthropicModel, FN2CAnthropicPricing> AnthropicModelPricing;

    /** Gemini Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | Gemini")
    TMap<EN2CGeminiModel, FN2CGeminiPricing> GeminiModelPricing;

    /** DeepSeek Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | DeepSeek")
    TMap<EN2CDeepSeekModel, FN2CDeepSeekPricing> DeepSeekModelPricing;
    
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

    /** MCP server port (Model Context Protocol HTTP server) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Server Port", ClampMin="1024", ClampMax="65535", UIMin="1024", UIMax="65535"))
    int32 McpServerPort = 27000;
    
    /** Enable dynamic tool discovery for MCP server. When enabled, only assess-needed-tools will be available initially.
     * This feature requires MCP clients that support discovery (e.g., VSCode, Cline).
     * Note: Changing this setting requires an editor restart to take effect. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Enable Dynamic Tool Discovery",
              ToolTip="Enable dynamic tool discovery for MCP server. When enabled, only assess-needed-tools will be available initially. This feature requires MCP clients that support discovery (e.g., VSCode, Cline). Note: Changing this setting requires an editor restart to take effect."))
    bool bEnableDynamicToolDiscovery = false;

    /** Enable Python script-only mode. When enabled, most C++ MCP tools are disabled
     * and LLMs use Python scripts via run-python instead. Only essential tools remain:
     * run-python and script management tools (list, search, get, save, delete, get-functions).
     * Pair with Context7 MCP server for UE Python API documentation lookup.
     * Note: Changing this setting requires an editor restart to take effect. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | MCP Server",
        meta=(DisplayName="Enable Python Script-Only Mode",
              ToolTip="When enabled, most C++ tools are disabled. LLMs use Python scripts via run-python instead. Includes script library tools for reusable script management. Pair with Context7 MCP for API docs. Requires editor restart."))
    bool bEnablePythonScriptOnlyMode = false;

    /** Get the API key for the selected provider */
    FString GetActiveApiKey() const;

    /** Get the model for the selected provider */
    FString GetActiveModel() const;

    /** Get the API key for a specific provider */
    FString GetActiveApiKeyForProvider(EN2CLLMProvider Provider) const;

    /** Fills the API Key Source fields from the secret store and the environment (never shows a key) */
    void RefreshApiKeySources();

    /** The API Key Source text for Provider: the saved key, the environment variable it uses, or none */
    static FString DescribeApiKeySource(EN2CLLMProvider Provider);

    /** Get the model for a specific provider */
    FString GetActiveModelForProvider(EN2CLLMProvider Provider) const;

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
     * from the provider's Max Output Tokens setting, which an override model gets too (capped at its own maximum).
     */
    void ResolveRequestModel(EN2CLLMProvider InProvider, const FString& ModelIdOverride, FN2CLLMConfig& InOutConfig) const;

    /** Get the minimum severity level for logging */
    EN2CLogSeverity GetMinLogSeverity() const { return MinSeverity; }

    /** Notify that model settings have changed - call this to update token estimation UI */
    void NotifyModelSettingsChanged();

    /** UI Color palette for all NodeToCode widgets */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming",
        meta=(DisplayName="UI Colors"))
    FN2CUIColors UIColors;

    /** Get the UI color palette */
    static const FN2CUIColors& GetUIColors()
    {
        return GetDefault<UN2CSettings>()->UIColors;
    }

    /** Code editor theme colors */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming",
        meta=(DisplayName="Code Editor Theme"))
    FN2CCodeEditorThemes CodeEditorTheme;
    
    /** Get theme colors for a specific language and theme name */
    const FN2CCodeEditorColors* GetThemeColors(EN2CCodeLanguage Language, const FName& ThemeName) const;

    // Begin UObject Interface
    virtual void PostInitProperties() override;
    virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;
    virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    // End UObject Interface

    /**
     * Moves each saved model selection whose model the provider has shut down (registry bIsRetired, or a value
     * the registry does not know) to that provider's N2CDefaultModels value, and logs one warning naming every
     * change. Called after the config is loaded (PostInitProperties) and after it is reloaded (PostReloadConfig,
     * which is how a plugin's config branch reaches the settings object at startup), so a project that saved
     * AnthropicModel=Claude3_7_Sonnet loads with the current default instead of failing every request.
     * The config file is not rewritten; choosing a model in Project Settings persists the new value.
     * @return the number of selections changed
     */
    int32 MigrateRetiredModelSelections();

    /**
     * Runs the migrations for every settings version above SettingsVersion. It does not change SettingsVersion,
     * and every step gives the same result when run again on its own output, so it is safe on each load and
     * reload; SettingsVersion moves to CurrentSettingsVersion only on an edit (PreEditChange), so the config is
     * not rewritten on load. Called from PostInitProperties and PostReloadConfig.
     * @return the number of values changed
     */
    int32 MigrateSavedSettings();

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

    /** Get the current model's input cost */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services | Pricing")
    float GetCurrentInputCost() const
    {
        switch (Provider)
        {
            case EN2CLLMProvider::OpenAI:
                if (const FN2COpenAIPricing* Pricing = OpenAIModelPricing.Find(OpenAI_Model))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetOpenAIPricing(OpenAI_Model).InputCost;
            case EN2CLLMProvider::Anthropic:
                if (const FN2CAnthropicPricing* Pricing = AnthropicModelPricing.Find(AnthropicModel))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetAnthropicPricing(AnthropicModel).InputCost;
            case EN2CLLMProvider::Gemini:
                if (const FN2CGeminiPricing* Pricing = GeminiModelPricing.Find(Gemini_Model))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetGeminiPricing(Gemini_Model).InputCost;
            case EN2CLLMProvider::DeepSeek:
                if (const FN2CDeepSeekPricing* Pricing = DeepSeekModelPricing.Find(DeepSeekModel))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetDeepSeekPricing(DeepSeekModel).InputCost;
            case EN2CLLMProvider::Ollama:
            case EN2CLLMProvider::LMStudio:
                return 0.0f; // Local models are free
            default:
                return 0.0f;
        }
    }

    /** Get the current model's output cost */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services | Pricing")
    float GetCurrentOutputCost() const
    {
        switch (Provider)
        {
            case EN2CLLMProvider::OpenAI:
                if (const FN2COpenAIPricing* Pricing = OpenAIModelPricing.Find(OpenAI_Model))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetOpenAIPricing(OpenAI_Model).OutputCost;
            case EN2CLLMProvider::Anthropic:
                if (const FN2CAnthropicPricing* Pricing = AnthropicModelPricing.Find(AnthropicModel))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetAnthropicPricing(AnthropicModel).OutputCost;
            case EN2CLLMProvider::Gemini:
                if (const FN2CGeminiPricing* Pricing = GeminiModelPricing.Find(Gemini_Model))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetGeminiPricing(Gemini_Model).OutputCost;
            case EN2CLLMProvider::DeepSeek:
                if (const FN2CDeepSeekPricing* Pricing = DeepSeekModelPricing.Find(DeepSeekModel))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetDeepSeekPricing(DeepSeekModel).OutputCost;
            case EN2CLLMProvider::Ollama:
            case EN2CLLMProvider::LMStudio:
                return 0.0f; // Local models are free
            default:
                return 0.0f;
        }
    }

    /** Calculate and store token estimate for reference files */
    int32 GetReferenceFilesTokenEstimate() const
    {
        int32 TotalTokens = 0;
        for (const FFilePath& Path : ReferenceSourceFilePaths)
        {
            if (FPaths::FileExists(Path.FilePath))
            {
                FString Content;
                if (FFileHelper::LoadFileToString(Content, *Path.FilePath))
                {
                    // Estimate tokens by dividing character count by 4
                    TotalTokens += FMath::CeilToInt(Content.Len() / 4.0f);
                }
            }
        }
        return TotalTokens;
    }


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
    
    /** Validate all reference source file paths */
    void ValidateReferenceSourcePaths();

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
    
#if WITH_EDITOR
    /** Show a notification that the editor needs to restart */
    void ShowRestartEditorNotification();

    /** Weak pointer to the restart notification */
    TWeakPtr<SNotificationItem> RestartNotificationPtr;
#endif
};

// Compact UI color accessors for widget code (unity-build safe)
inline const FN2CUIColors& N2CUI() { return UN2CSettings::GetUIColors(); }
inline FLinearColor UIL(const FColor& C) { return FN2CUIColors::ToLinear(C); }
inline FLinearColor UILA(const FColor& C, float A) { return FN2CUIColors::ToLinearWithAlpha(C, A); }

// TAttribute-bound UI color accessors for live settings updates (unity-build safe)
inline TAttribute<FSlateColor> UIBind(FColor FN2CUIColors::* Member)
{
    return TAttribute<FSlateColor>::CreateLambda([Member]() -> FSlateColor
    {
        return FSlateColor(FN2CUIColors::ToLinear(UN2CSettings::GetUIColors().*Member));
    });
}

inline TAttribute<FSlateColor> UIBindAlpha(FColor FN2CUIColors::* Member, float Alpha)
{
    return TAttribute<FSlateColor>::CreateLambda([Member, Alpha]() -> FSlateColor
    {
        return FSlateColor(FN2CUIColors::ToLinearWithAlpha(UN2CSettings::GetUIColors().*Member, Alpha));
    });
}
