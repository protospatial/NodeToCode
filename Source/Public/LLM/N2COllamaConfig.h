// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2COllamaConfig.generated.h"

/**
 * @struct FN2COllamaConfig
 * @brief Configuration settings for Ollama LLM integration. Please 
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
              ToolTip="Enable if your Ollama model supports system prompts. Disable to merge system and user prompts."))
    bool bUseSystemPrompts = false;

    /** Text to prepend to all user messages */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Prepended Model Command", 
              ToolTip="Text to prepend to user messages (e.g., '/no_think' to disable thinking for reasoning models, or other model-specific commands). This text will appear at the start of each user message."))
    FString PrependedModelCommand = "";

    /** Keep alive duration in seconds */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "General",
        meta=(DisplayName="Keep Alive Duration", 
              ToolTip="Duration in seconds to keep the model loaded in memory following the request. -1 will keep it loaded indefinitely.",
              ClampMin="-1"))
    int32 KeepAlive = 3600;

    /** Temperature for creative variation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Temperature", 
              ToolTip="Controls creativity in responses. Higher values increase creativity, lower values make responses more focused.",
              ClampMin="0.0", ClampMax="2.0"))
    float Temperature = 0.0f;

    /** Maximum tokens to predict */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Max Tokens", 
              ToolTip="Maximum number of tokens to generate. -1 for unlimited.",
              ClampMin="-1"))
    int32 NumPredict = 8192;

    /** Top P sampling */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Top P", 
              ToolTip="Controls diversity via nucleus sampling. Higher values increase diversity.",
              ClampMin="0.0", ClampMax="1.0"))
    float TopP = 0.5f;

    /** Top K sampling */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Top K", 
              ToolTip="Limits the number of tokens considered for generation. Lower values increase focus.",
              ClampMin="0"))
    int32 TopK = 40;

    /** Minimum P threshold */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Min P", 
              ToolTip="Minimum probability threshold relative to the highest token probability.",
              ClampMin="0.0", ClampMax="1.0"))
    float MinP = 0.05f;

    /** Repeat penalty */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generation",
        meta=(DisplayName="Repeat Penalty", 
              ToolTip="Penalty for repeating tokens. Higher values reduce repetition.",
              ClampMin="0.0"))
    float RepeatPenalty = 1.1f;

    /** Context window size */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context",
        meta=(DisplayName="Context Window", 
              ToolTip="Size of the context window in tokens. For Resursive Translation and/or larger blueprint graphs, you'll definitely want this to be 16k-32k+. The higher you can do, the better.",
              ClampMin="8192"))
    int32 NumCtx = 8192;

    /** Mirostat algorithm version */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Mirostat Mode", 
              ToolTip="Mirostat sampling mode. 0=disabled, 1=Mirostat, 2=Mirostat 2.0"))
    int32 Mirostat = 0;

    /** Mirostat learning rate */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Mirostat Eta", 
              ToolTip="Mirostat learning rate. Lower values make slower adjustments.",
              ClampMin="0.0", ClampMax="1.0"))
    float MirostatEta = 0.1f;

    /** Mirostat target entropy */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Mirostat Tau", 
              ToolTip="Mirostat target entropy. Lower values increase focus.",
              ClampMin="0.0"))
    float MirostatTau = 5.0f;

    /** Random seed for generation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Advanced",
        meta=(DisplayName="Random Seed", 
              ToolTip="Seed for random generation. 0 for random, other values for reproducible generation."))
    int32 Seed = 0;
};
