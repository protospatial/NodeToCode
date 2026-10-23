// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2COllamaConfig.generated.h"

/** What NodeToCode sends in the /api/chat "think" field */
UENUM(BlueprintType)
enum class EN2COllamaThink : uint8
{
    /** No think field: the model's own default applies */
    Default UMETA(DisplayName = "Model Default"),
    /** think: true */
    On UMETA(DisplayName = "On"),
    /** think: false */
    Off UMETA(DisplayName = "Off"),
    /** think: the Think Level string, for models that take levels (for example gpt-oss: low, medium, high) */
    Level UMETA(DisplayName = "Level")
};

/**
 * @struct FN2COllamaConfig
 * @brief Configuration settings for Ollama LLM integration.
 *
 * Only what the user sets is sent: each sampler value goes into the request options only when its override
 * toggle is on (0.0 is a valid temperature, so the value alone cannot mean "unset"), and the context window
 * and max tokens only when greater than 0. Everything else is left to the Ollama server and the model's own
 * defaults (its Modelfile parameters and the server's context length).
 */
USTRUCT(BlueprintType)
struct FN2COllamaConfig
{
    GENERATED_BODY()

	/** The base endpoint to use for the Ollama API. Defaults to http:localhost:11434  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
		meta=(DisplayName="Ollama Endpoint",
			  ToolTip="The base endpoint to use for the Ollama API. Defaults to http:localhost:11434"))
	FString OllamaEndpoint = "http://localhost:11434";

    /** Whether to use system prompts with Ollama models */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Use System Prompts",
              ToolTip="Send the system prompt as a system message. Ollama's chat templates handle system messages for current models; disable only for a model whose template drops them, to merge the system prompt into the user message."))
    bool bUseSystemPrompts = true;

    /** Text to prepend to all user messages */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Prepended Model Command",
              ToolTip="Text to prepend to user messages, for model-specific commands. This text will appear at the start of each user message. To switch thinking on or off, use Think instead."))
    FString PrependedModelCommand = "";

    /** Thinking control sent as the top-level think field */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Think",
              ToolTip="Model Default sends no think field. On and Off send think true or false. Level sends the Think Level string, for models that take levels (ollama show <model> lists them)."))
    EN2COllamaThink Think = EN2COllamaThink::Default;

    /** Thinking level string, sent when Think is Level */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Think Level", EditCondition="Think == EN2COllamaThink::Level", EditConditionHides,
              ToolTip="Level name the model accepts, exactly as /api/show lists it (for example low, medium or high for gpt-oss)."))
    FString ThinkLevel = TEXT("medium");

    /** Keep alive duration in seconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Keep Alive Duration",
              ToolTip="Seconds to keep the model loaded in memory after the request. 0 unloads it right after the response; -1 keeps it loaded indefinitely.",
              ClampMin="-1"))
    int32 KeepAlive = 3600;

    /** Maximum tokens to predict */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Max Tokens",
              ToolTip="Maximum number of tokens to generate (num_predict). 0 sends nothing, so the server default applies (no limit).",
              ClampMin="0"))
    int32 NumPredict = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta=(InlineEditConditionToggle))
    bool bOverride_Temperature = false;

    /** Temperature for creative variation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Temperature", EditCondition="bOverride_Temperature",
              ToolTip="Sent only when ticked; otherwise the model's default applies. Higher values increase creativity, lower values make responses more focused.",
              ClampMin="0.0", ClampMax="2.0"))
    float Temperature = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta=(InlineEditConditionToggle))
    bool bOverride_TopP = false;

    /** Top P sampling */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Top P", EditCondition="bOverride_TopP",
              ToolTip="Sent only when ticked; otherwise the model's default applies. Controls diversity via nucleus sampling. Higher values increase diversity.",
              ClampMin="0.0", ClampMax="1.0"))
    float TopP = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta=(InlineEditConditionToggle))
    bool bOverride_TopK = false;

    /** Top K sampling */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Top K", EditCondition="bOverride_TopK",
              ToolTip="Sent only when ticked; otherwise the model's default applies. Limits the number of tokens considered for generation. Lower values increase focus.",
              ClampMin="0"))
    int32 TopK = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta=(InlineEditConditionToggle))
    bool bOverride_MinP = false;

    /** Minimum P threshold */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Min P", EditCondition="bOverride_MinP",
              ToolTip="Sent only when ticked; otherwise the model's default applies. Minimum probability threshold relative to the highest token probability.",
              ClampMin="0.0", ClampMax="1.0"))
    float MinP = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation", meta=(InlineEditConditionToggle))
    bool bOverride_RepeatPenalty = false;

    /** Repeat penalty */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Repeat Penalty", EditCondition="bOverride_RepeatPenalty",
              ToolTip="Sent only when ticked; otherwise the model's default applies. Penalty for repeating tokens. Higher values reduce repetition.",
              ClampMin="0.0"))
    float RepeatPenalty = 1.1f;

    /** Context window size */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context",
        meta=(DisplayName="Context Window",
              ToolTip="Context window in tokens (num_ctx). 0 sends nothing, so the server's context length applies (OLLAMA_CONTEXT_LENGTH or the Ollama app setting; by default 4K, 32K or 256K depending on VRAM). The system prompt alone is about 4-5K tokens, so 32K-64K is recommended; Ollama silently cuts prompts longer than the window.",
              ClampMin="0"))
    int32 NumCtx = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta=(InlineEditConditionToggle))
    bool bOverride_Mirostat = false;

    /** Mirostat algorithm version */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Mirostat Mode", EditCondition="bOverride_Mirostat",
              ToolTip="Sent only when ticked. Mirostat sampling mode. 0=disabled, 1=Mirostat, 2=Mirostat 2.0"))
    int32 Mirostat = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta=(InlineEditConditionToggle))
    bool bOverride_MirostatEta = false;

    /** Mirostat learning rate */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Mirostat Eta", EditCondition="bOverride_MirostatEta",
              ToolTip="Sent only when ticked. Mirostat learning rate. Lower values make slower adjustments.",
              ClampMin="0.0", ClampMax="1.0"))
    float MirostatEta = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta=(InlineEditConditionToggle))
    bool bOverride_MirostatTau = false;

    /** Mirostat target entropy */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Mirostat Tau", EditCondition="bOverride_MirostatTau",
              ToolTip="Sent only when ticked. Mirostat target entropy. Lower values increase focus.",
              ClampMin="0.0"))
    float MirostatTau = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced", meta=(InlineEditConditionToggle))
    bool bOverride_Seed = false;

    /** Random seed for generation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Random Seed", EditCondition="bOverride_Seed",
              ToolTip="Sent only when ticked. A fixed seed makes generation reproducible for the same prompt."))
    int32 Seed = 0;
};
