// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMPricing.h"
#include "N2CLLMModels.generated.h"

/*
 * Model enums.
 *
 * Settings are saved by enumerator name (Config=NodeToCode), so enumerators are never renamed or removed:
 * - New models are appended at the end of their enum.
 * - A model the provider has shut down keeps its enumerator, marked UMETA(Hidden) so Project Settings does not
 *   offer it, and its registry entry is marked retired. UN2CSettings::PostInitProperties moves a saved retired
 *   value to the provider default (see N2CDefaultModels below).
 * - A model the provider has deprecated but still serves shows its shutdown date in its display name.
 * Model IDs, limits, prices and dates live in FN2CLLMModelRegistry (N2CLLMModelRegistry.cpp).
 */

/** Available OpenAI models */
UENUM(BlueprintType)
enum class EN2COpenAIModel : uint8
{
    // GPT-5.2 family (December 2025)
    GPT_5_2                 UMETA(DisplayName = "GPT-5.2", Value = "gpt-5.2"),
    GPT_5_2_Pro             UMETA(DisplayName = "GPT-5.2 Pro", Value = "gpt-5.2-pro"),
    // GPT-5 family (August 2025), shut down 2026-12-11
    GPT_5                   UMETA(DisplayName = "GPT-5 (shuts down 2026-12-11)", Value = "gpt-5"),
    GPT_5_Mini              UMETA(DisplayName = "GPT-5 Mini (shuts down 2026-12-11)", Value = "gpt-5-mini"),
    GPT_5_Nano              UMETA(DisplayName = "GPT-5 Nano (shuts down 2026-12-11)", Value = "gpt-5-nano"),
    // o-series reasoning models
    GPT_o4_mini             UMETA(Hidden, DisplayName = "o4 Mini (retired)", Value = "o4-mini"),
    GPT_o3                  UMETA(DisplayName = "o3 (shuts down 2026-12-11)", Value = "o3"),
    GPT_o3_mini             UMETA(Hidden, DisplayName = "o3 Mini (retired)", Value = "o3-mini"),
    GPT_o1                  UMETA(Hidden, DisplayName = "o1 (retired)", Value = "o1"),
    GPT_o1_Preview          UMETA(Hidden, DisplayName = "o1 Preview (retired)", Value = "o1-preview-2024-09-12"),
    GPT_o1_Mini             UMETA(Hidden, DisplayName = "o1 Mini (retired)", Value = "o1-mini-2024-09-12"),
    // GPT-4 family, shut down 2027-01-14
    GPT_4_1                 UMETA(DisplayName = "GPT-4.1 (shuts down 2027-01-14)", Value = "gpt-4.1"),
    GPT4o_2024_08_06        UMETA(DisplayName = "GPT-4o (shuts down 2027-01-14)", Value = "gpt-4o-2024-08-06"),
    GPT4o_Mini_2024_07_18   UMETA(DisplayName = "GPT-4o Mini (shuts down 2027-01-14)", Value = "gpt-4o-mini-2024-07-18"),
    // GPT-6 family (2026)
    GPT_6_1_Sol             UMETA(DisplayName = "GPT-6.1 Sol", Value = "gpt-6.1-sol"),
    GPT_6_Astra             UMETA(DisplayName = "GPT-6 Astra", Value = "gpt-6-astra"),
    GPT_6_Luna              UMETA(DisplayName = "GPT-6 Luna", Value = "gpt-6-luna"),
};

/** Available Anthropic models */
UENUM(BlueprintType)
enum class EN2CAnthropicModel : uint8
{
    Claude4_5_Opus          UMETA(DisplayName = "Claude Opus 4.5", Value = "claude-opus-4-5-20251101"),
    Claude4_5_Sonnet        UMETA(DisplayName = "Claude Sonnet 4.5 (retires 2026-11-30)", Value = "claude-sonnet-4-5-20250929"),
    Claude4_1_Opus          UMETA(Hidden, DisplayName = "Claude Opus 4.1 (retired)", Value = "claude-opus-4-1-20250805"),
    Claude4_Opus            UMETA(Hidden, DisplayName = "Claude Opus 4 (retired)", Value = "claude-opus-4-20250514"),
    Claude4_Sonnet          UMETA(Hidden, DisplayName = "Claude Sonnet 4 (retired)", Value = "claude-sonnet-4-20250514"),
    Claude3_7_Sonnet        UMETA(Hidden, DisplayName = "Claude 3.7 Sonnet (retired)", Value = "claude-3-7-sonnet-20250219"),
    Claude3_5_Sonnet        UMETA(Hidden, DisplayName = "Claude 3.5 Sonnet (retired)", Value = "claude-3-5-sonnet-20241022"),
    Claude3_5_Haiku         UMETA(Hidden, DisplayName = "Claude 3.5 Haiku (retired)", Value = "claude-3-5-haiku-20241022"),
    // Claude 5.5 family (2026)
    Claude5_5_Sonnet        UMETA(DisplayName = "Claude Sonnet 5.5", Value = "claude-sonnet-5-5"),
    Claude5_5_Opus          UMETA(DisplayName = "Claude Opus 5.5", Value = "claude-opus-5-5"),
    Claude5_5_Haiku         UMETA(DisplayName = "Claude Haiku 5.5", Value = "claude-haiku-5-5"),
};

/** Available Gemini models */
UENUM(BlueprintType)
enum class EN2CGeminiModel : uint8
{
    Gemini_3_Pro_Preview         UMETA(Hidden, DisplayName = "Gemini 3 Pro Preview (retired)", Value = "gemini-3-pro-preview"),
    Gemini_3_Flash_Preview       UMETA(DisplayName = "Gemini 3 Flash Preview", Value = "gemini-3-flash-preview"),
    Gemini_2_5_Pro               UMETA(Hidden, DisplayName = "Gemini 2.5 Pro Preview (retired)", Value = "gemini-2.5-pro-preview-05-06"),
    Gemini_2_5_Flash             UMETA(Hidden, DisplayName = "Gemini 2.5 Flash Preview (retired)", Value = "gemini-2.5-flash-preview-05-20"),
    Gemini_Flash_2_0             UMETA(Hidden, DisplayName = "Gemini 2.0 Flash (retired)", Value = "gemini-2.0-flash"),
    Gemini_Flash_Lite_2_0        UMETA(Hidden, DisplayName = "Gemini 2.0 Flash-Lite-Preview-02-05 (retired)", Value = "gemini-2.0-flash-lite-preview-02-05"),
    Gemini_1_5_Flash             UMETA(Hidden, DisplayName = "Gemini 1.5 Flash (retired)", Value = "gemini-1.5-flash"),
    Gemini_1_5_Pro               UMETA(Hidden, DisplayName = "Gemini 1.5 Pro (retired)", Value = "gemini-1.5-pro"),
    Gemini_2_0_ProExp_02_05      UMETA(Hidden, DisplayName = "Gemini 2.0 Pro Exp 02-05 (retired)", Value = "gemini-2.0-pro-exp-02-05"),
    Gemini_2_0_FlashThinkingExp  UMETA(Hidden, DisplayName = "Gemini 2.0 Flash Thinking Exp 01-21 (retired)", Value = "gemini-2.0-flash-thinking-exp-01-21"),
    // Gemini 3.5+ and 4 (2026)
    Gemini_3_8_Flash             UMETA(DisplayName = "Gemini 3.8 Flash", Value = "gemini-3.8-flash"),
    Gemini_4_Argon               UMETA(DisplayName = "Gemini 4 Argon", Value = "gemini-4-argon"),
    Gemini_3_5_Flash_Lite        UMETA(DisplayName = "Gemini 3.5 Flash-Lite", Value = "gemini-3.5-flash-lite"),
};

/** Available DeepSeek models */
UENUM(BlueprintType)
enum class EN2CDeepSeekModel : uint8
{
    // deepseek-reasoner and deepseek-chat were discontinued 2026-07-24. The enumerator names stay (saved settings
    // use them) and now select a thinking mode of a current model: the registry entry says what each one sends.
    DeepSeek_R1      UMETA(DisplayName = "DeepSeek Flash, thinking (high effort)", Value = "deepseek-flash"),
    DeepSeek_V3      UMETA(DisplayName = "DeepSeek Flash, no thinking (JSON output)", Value = "deepseek-flash"),
    // DeepSeek V4 (2026)
    DeepSeek_V4_Pro  UMETA(DisplayName = "DeepSeek V4 Pro, thinking (high effort)", Value = "deepseek-v4-pro"),
};


/** Helper functions for model enums */
struct FN2CLLMModelUtils
{
    /** Model value getters */
    static FString GetOpenAIModelValue(EN2COpenAIModel Model);
    static FString GetAnthropicModelValue(EN2CAnthropicModel Model);
    static FString GetGeminiModelValue(EN2CGeminiModel Model);
    static FString GetDeepSeekModelValue(EN2CDeepSeekModel Model);

    /** Pricing getters */
    static FN2COpenAIPricing GetOpenAIPricing(EN2COpenAIModel Model);
    static FN2CAnthropicPricing GetAnthropicPricing(EN2CAnthropicModel Model);
    static FN2CDeepSeekPricing GetDeepSeekPricing(EN2CDeepSeekModel Model);
    static FN2CGeminiPricing GetGeminiPricing(EN2CGeminiModel Model);

    /** System prompt support checks - delegates to FN2CLLMModelRegistry */
    static bool SupportsSystemPrompts(EN2COpenAIModel Model);
    static bool SupportsSystemPrompts(EN2CAnthropicModel Model);
};

/**
 * Default model per provider: the Project Settings defaults, the registry's fallback IDs, and the value a saved
 * retired selection is moved to. Each must map to a registry entry that is not deprecated
 * (NodeToCode.LLM.Registry checks this).
 */
namespace N2CDefaultModels
{
    constexpr EN2COpenAIModel OpenAI = EN2COpenAIModel::GPT_6_1_Sol;
    constexpr EN2CAnthropicModel Anthropic = EN2CAnthropicModel::Claude5_5_Sonnet;
    constexpr EN2CGeminiModel Gemini = EN2CGeminiModel::Gemini_3_8_Flash;
    constexpr EN2CDeepSeekModel DeepSeek = EN2CDeepSeekModel::DeepSeek_R1;
}
