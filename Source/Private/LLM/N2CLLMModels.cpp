// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMPricing.h"

// Initialize static pricing maps
const TMap<EN2COpenAIModel, FN2COpenAIPricing> FN2CLLMModelUtils::OpenAIPricing = {
    {EN2COpenAIModel::GPT4o_2024_08_06, FN2COpenAIPricing(2.5f, 10.0f)},
    {EN2COpenAIModel::GPT4o_Mini_2024_07_18, FN2COpenAIPricing(0.15f, 0.6f)},
    {EN2COpenAIModel::GPT_4_1, FN2COpenAIPricing(2.0f, 8.0f)},
    {EN2COpenAIModel::GPT_o1, FN2COpenAIPricing(15.0f, 60.0f)},
    {EN2COpenAIModel::GPT_o1_Preview, FN2COpenAIPricing(15.0f, 60.0f)},
    {EN2COpenAIModel::GPT_o1_Mini, FN2COpenAIPricing(1.1f, 4.4f)},
    {EN2COpenAIModel::GPT_o3, FN2COpenAIPricing(10.0f, 40.0f)},
    {EN2COpenAIModel::GPT_o3_mini, FN2COpenAIPricing(1.1f, 4.4f)},
    {EN2COpenAIModel::GPT_o4_mini, FN2COpenAIPricing(1.0f, 4.0f)}
};

const TMap<EN2CGeminiModel, FN2CGeminiPricing> FN2CLLMModelUtils::GeminiPricing = {
    {EN2CGeminiModel::Gemini_2_5_Flash, FN2CGeminiPricing(0.0f, 0.0f)},
    {EN2CGeminiModel::Gemini_2_5_Pro, FN2CGeminiPricing(1.25f, 10.0f)},
    {EN2CGeminiModel::Gemini_Flash_2_0, FN2CGeminiPricing(0.0f, 0.0f)},
    {EN2CGeminiModel::Gemini_Flash_Lite_2_0, FN2CGeminiPricing(0.0f, 0.0f)},
    {EN2CGeminiModel::Gemini_1_5_Flash, FN2CGeminiPricing(0.0f, 0.0f)},
    {EN2CGeminiModel::Gemini_1_5_Pro, FN2CGeminiPricing(0.0f, 0.0f)},
    {EN2CGeminiModel::Gemini_2_0_ProExp_02_05, FN2CGeminiPricing(0.0f, 0.0f)},
    {EN2CGeminiModel::Gemini_2_0_FlashThinkingExp, FN2CGeminiPricing(0.0f, 0.0f)},
};

const TMap<EN2CAnthropicModel, FN2CAnthropicPricing> FN2CLLMModelUtils::AnthropicPricing = {
    {EN2CAnthropicModel::Claude4_Opus, FN2CAnthropicPricing(15.0f, 75.0f)},
    {EN2CAnthropicModel::Claude4_Sonnet, FN2CAnthropicPricing(3.0f, 15.0f)},
    {EN2CAnthropicModel::Claude3_7_Sonnet, FN2CAnthropicPricing(3.0f, 15.0f)},
    {EN2CAnthropicModel::Claude3_5_Sonnet, FN2CAnthropicPricing(3.0f, 15.0f)},
    {EN2CAnthropicModel::Claude3_5_Haiku, FN2CAnthropicPricing(0.8f, 4.0f)}
};

const TMap<EN2CDeepSeekModel, FN2CDeepSeekPricing> FN2CLLMModelUtils::DeepSeekPricing = {
    {EN2CDeepSeekModel::DeepSeek_R1, FN2CDeepSeekPricing(0.14f, 0.55f)},
    {EN2CDeepSeekModel::DeepSeek_V3, FN2CDeepSeekPricing(0.07f, 0.27f)}
};

const TMap<EN2CxAIModel, FN2CxAIPricing> FN2CLLMModelUtils::xAIPricing = {
    {EN2CxAIModel::Grok_Code_Fast_1, FN2CxAIPricing(0.2f, 1.5f)},
	{EN2CxAIModel::Grok_4_Fast_Reasoning, FN2CxAIPricing(0.4f, 1.0f)},
	{EN2CxAIModel::Grok_4_Fast_NonReasoning, FN2CxAIPricing(0.4f, 1.0f)},
	{EN2CxAIModel::Grok_4, FN2CxAIPricing(6.0f, 30.0f)}
};

FString FN2CLLMModelUtils::GetOpenAIModelValue(EN2COpenAIModel Model)
{
    switch (Model)
    {
        case EN2COpenAIModel::GPT4o_2024_08_06:
            return TEXT("gpt-4o-2024-08-06");
        case EN2COpenAIModel::GPT4o_Mini_2024_07_18:
            return TEXT("gpt-4o-mini-2024-07-18");
        case EN2COpenAIModel::GPT_4_1:
            return TEXT("gpt-4.1");
        case EN2COpenAIModel::GPT_o1:
            return TEXT("o1");
        case EN2COpenAIModel::GPT_o3:
            return TEXT("o3");
        case EN2COpenAIModel::GPT_o3_mini:
            return TEXT("o3-mini");
        case EN2COpenAIModel::GPT_o4_mini:
            return TEXT("o4-mini");
        case EN2COpenAIModel::GPT_o1_Preview:
            return TEXT("o1-preview-2024-09-12");
        case EN2COpenAIModel::GPT_o1_Mini:
            return TEXT("o1-mini-2024-09-12");
        default:
            return TEXT("gpt-4o-2024-08-06");
    }
}

FString FN2CLLMModelUtils::GetAnthropicModelValue(EN2CAnthropicModel Model)
{
    switch (Model)
    {
        case EN2CAnthropicModel::Claude4_Opus:
            return TEXT("claude-4-opus-20250514");
        case EN2CAnthropicModel::Claude4_Sonnet:
            return TEXT("claude-4-sonnet-20250514");
        case EN2CAnthropicModel::Claude3_7_Sonnet:
            return TEXT("claude-3-7-sonnet-20250219");
        case EN2CAnthropicModel::Claude3_5_Sonnet:
            return TEXT("claude-3-5-sonnet-20241022");
        case EN2CAnthropicModel::Claude3_5_Haiku:
            return TEXT("claude-3-5-haiku-20241022");
        default:
            return TEXT("claude-4-sonnet-20250514");
    }
}

FString FN2CLLMModelUtils::GetGeminiModelValue(EN2CGeminiModel Model)
{
    switch (Model)
    {
    case EN2CGeminiModel::Gemini_2_5_Flash:
        return TEXT("gemini-2.5-flash-preview-05-20");
    case EN2CGeminiModel::Gemini_2_5_Pro:
        return TEXT("gemini-2.5-pro-preview-05-06");
    case EN2CGeminiModel::Gemini_Flash_2_0:
        return TEXT("gemini-2.0-flash");
    case EN2CGeminiModel::Gemini_Flash_Lite_2_0:
        return TEXT("gemini-2.0-flash-lite-preview-02-05");
    case EN2CGeminiModel::Gemini_1_5_Flash:
        return TEXT("gemini-1.5-flash");
    case EN2CGeminiModel::Gemini_1_5_Pro:
        return TEXT("gemini-1.5-pro");
    case EN2CGeminiModel::Gemini_2_0_ProExp_02_05:
        return TEXT("gemini-2.0-pro-exp-02-05");
    case EN2CGeminiModel::Gemini_2_0_FlashThinkingExp:
        return TEXT("gemini-2.0-flash-thinking-exp-01-21");
    default:
        return TEXT("gemini-2.5-flash-preview-05-20");
    }
}


FString FN2CLLMModelUtils::GetDeepSeekModelValue(EN2CDeepSeekModel Model)
{
    switch (Model)
    {
        case EN2CDeepSeekModel::DeepSeek_R1:
            return TEXT("deepseek-reasoner");
        case EN2CDeepSeekModel::DeepSeek_V3:
            return TEXT("deepseek-chat");
        default:
            return TEXT("deepseek-reasoner");
    }
}

FN2COpenAIPricing FN2CLLMModelUtils::GetOpenAIPricing(EN2COpenAIModel Model)
{
    if (const FN2COpenAIPricing* Found = OpenAIPricing.Find(Model))
    {
        return *Found;
    }
    return FN2COpenAIPricing();
}

FN2CAnthropicPricing FN2CLLMModelUtils::GetAnthropicPricing(EN2CAnthropicModel Model)
{
    if (const FN2CAnthropicPricing* Found = AnthropicPricing.Find(Model))
    {
        return *Found;
    }
    return FN2CAnthropicPricing();
}

FN2CDeepSeekPricing FN2CLLMModelUtils::GetDeepSeekPricing(EN2CDeepSeekModel Model)
{
    if (const FN2CDeepSeekPricing* Found = DeepSeekPricing.Find(Model))
    {
        return *Found;
    }
    return FN2CDeepSeekPricing();
}

FN2CGeminiPricing FN2CLLMModelUtils::GetGeminiPricing(EN2CGeminiModel Model)
{
    if (const FN2CGeminiPricing* Found = GeminiPricing.Find(Model))
    {
        return *Found;
    }
    return FN2CGeminiPricing();
}

FString FN2CLLMModelUtils::GetxAIModelValue(EN2CxAIModel Model)
{
    switch (Model)
    {
        case EN2CxAIModel::Grok_Code_Fast_1:
            return TEXT("grok-code-fast");
    	case EN2CxAIModel::Grok_4_Fast_Reasoning:
    		return TEXT("grok-4-fast-reasoning-latest");
        case EN2CxAIModel::Grok_4_Fast_NonReasoning:
            return TEXT("grok-4-fast-non-reasoning-latest");
    	case EN2CxAIModel::Grok_4:
    		return TEXT("grok-4-latest");
        default:
            return TEXT("grok-code-fast");
    }
}

FN2CxAIPricing FN2CLLMModelUtils::GetxAIPricing(EN2CxAIModel Model)
{
    if (const FN2CxAIPricing* Found = xAIPricing.Find(Model))
    {
        return *Found;
    }
    return FN2CxAIPricing();
}
