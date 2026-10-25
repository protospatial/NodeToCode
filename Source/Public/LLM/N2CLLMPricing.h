// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMPricing.generated.h"

/**
 * A price override for one model, per 1M tokens in USD: the value type of UN2CSettings::ModelPricing, keyed by model
 * ID. A model without an override costs its catalog price (Content/Data/N2CModelCatalog.json).
 */
USTRUCT(BlueprintType)
struct FN2CModelPricing
{
    GENERATED_BODY()

    /** Cost per 1M input tokens (USD) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float InputCost = 0.0f;

    /** Cost per 1M output tokens (USD) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float OutputCost = 0.0f;

    FN2CModelPricing() {}
    FN2CModelPricing(float InInput, float InOutput)
        : InputCost(InInput), OutputCost(InOutput) {}
};

/*
 * The per-provider pricing structs below are deprecated: settings keep one ModelPricing map keyed by model ID since
 * settings version 4 (card 0129). They stay for the 2.x line because Blueprints or Python may name them, and the
 * settings upgrade reads the saved 1.2.x maps through them.
 */

/** Pricing information for OpenAI models */
USTRUCT(BlueprintType)
struct FN2COpenAIPricing
{
    GENERATED_BODY()

    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float InputCost = 0.0f;
    
    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float OutputCost = 0.0f;
    

    FN2COpenAIPricing() {}
    FN2COpenAIPricing(float InInput, float InOutput) 
        : InputCost(InInput), OutputCost(InOutput) {}
};

/** Pricing information for Anthropic models */
USTRUCT(BlueprintType)
struct FN2CAnthropicPricing
{
    GENERATED_BODY()

    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float InputCost = 0.0f;

    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float OutputCost = 0.0f;

    FN2CAnthropicPricing() {}
    FN2CAnthropicPricing(float InInput, float InOutput) 
        : InputCost(InInput), OutputCost(InOutput) {}
};

/** Pricing information for Gemini models */
USTRUCT(BlueprintType)
struct FN2CGeminiPricing
{
    GENERATED_BODY()

    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float InputCost = 0.0f;
    
    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float OutputCost = 0.0f;
    

    FN2CGeminiPricing() {}
    FN2CGeminiPricing(float InInput, float InOutput) 
        : InputCost(InInput), OutputCost(InOutput) {}
};

/** Pricing information for DeepSeek models */
USTRUCT(BlueprintType)
struct FN2CDeepSeekPricing
{
    GENERATED_BODY()

    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float InputCost = 0.0f;

    // Cost per 1M tokens
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Pricing")
    float OutputCost = 0.0f;

    FN2CDeepSeekPricing() {}
    FN2CDeepSeekPricing(float InInput, float InOutput) 
        : InputCost(InInput), OutputCost(InOutput) {}
};
