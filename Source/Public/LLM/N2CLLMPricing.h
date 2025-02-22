// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMPricing.generated.h"

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

/** Pricing information for OpenAI models */
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
