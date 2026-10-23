// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModelRegistry.h"

FN2CLLMModelRegistry& FN2CLLMModelRegistry::Get()
{
	static FN2CLLMModelRegistry Instance;
	if (!Instance.bIsInitialized)
	{
		Instance.InitializeRegistry();
		Instance.bIsInitialized = true;
	}
	return Instance;
}

FN2CLLMModelRegistry::FN2CLLMModelRegistry()
{
}

void FN2CLLMModelRegistry::InitializeRegistry()
{
	// ========================================================================
	// OpenAI Models
	// ========================================================================

	// GPT-5.2 - Flagship model (December 2025)
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_2, FN2CModelMetadata(
		TEXT("gpt-5.2"),
		TEXT("GPT-5.2"),
		EN2CLLMProvider::OpenAI,
		1.75f, 14.0f,         // Pricing per 1M tokens
		400000,               // Context window
		128000,               // Max output
		true,                 // Supports system prompts
		true,                 // Supports structured output
		false,                // Not a reasoning model (has reasoning_effort param)
		0.0f,                 // Default temperature
		16000,                // Default max tokens
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-5.2 Pro - More compute for harder problems (requires Responses API)
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_2_Pro, FN2CModelMetadata(
		TEXT("gpt-5.2-pro"),
		TEXT("GPT-5.2 Pro"),
		EN2CLLMProvider::OpenAI,
		3.5f, 28.0f,          // Estimated 2x base pricing
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		16000,
		true,                 // Requires OpenAI Responses API
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-5 - Strong coding model (August 2025)
	OpenAIModels.Add(EN2COpenAIModel::GPT_5, FN2CModelMetadata(
		TEXT("gpt-5"),
		TEXT("GPT-5"),
		EN2CLLMProvider::OpenAI,
		1.25f, 10.0f,
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-5 Mini - Faster, cost-efficient
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_Mini, FN2CModelMetadata(
		TEXT("gpt-5-mini"),
		TEXT("GPT-5 Mini"),
		EN2CLLMProvider::OpenAI,
		0.25f, 2.0f,
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-5 Nano - Smallest, fastest, most affordable
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_Nano, FN2CModelMetadata(
		TEXT("gpt-5-nano"),
		TEXT("GPT-5 Nano"),
		EN2CLLMProvider::OpenAI,
		0.05f, 0.4f,
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// o4 Mini - Reasoning model (supports developer prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o4_mini, FN2CModelMetadata(
		TEXT("o4-mini"),
		TEXT("o4 Mini"),
		EN2CLLMProvider::OpenAI,
		1.1f, 4.4f,           // Pricing per 1M tokens
		200000,               // Context window
		100000,               // Max output
		true,                 // Supports system prompts (developer messages)
		true,                 // Supports structured output
		true,                 // Is reasoning model
		1.0f,                 // Default temperature (required for reasoning)
		16000,                // Default max tokens
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// o3 - Advanced reasoning model
	OpenAIModels.Add(EN2COpenAIModel::GPT_o3, FN2CModelMetadata(
		TEXT("o3"),
		TEXT("o3"),
		EN2CLLMProvider::OpenAI,
		10.0f, 40.0f,
		200000,
		100000,
		true,                 // Supports developer prompts
		false,                // Structured output: json_object only (no json_schema), as before
		true,                 // Is reasoning model
		1.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// o3 Mini - Smaller o3 variant
	OpenAIModels.Add(EN2COpenAIModel::GPT_o3_mini, FN2CModelMetadata(
		TEXT("o3-mini"),
		TEXT("o3 Mini"),
		EN2CLLMProvider::OpenAI,
		1.1f, 4.4f,
		200000,
		100000,
		true,
		false,                // Structured output: json_object only (no json_schema), as before
		true,
		1.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// o1 - Original reasoning model (NO system prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o1, FN2CModelMetadata(
		TEXT("o1"),
		TEXT("o1"),
		EN2CLLMProvider::OpenAI,
		15.0f, 60.0f,
		200000,
		100000,
		false,                // Does NOT support system prompts
		false,                // Structured output: json_object only (no json_schema), as before
		true,
		1.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// o1 Preview (NO system prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o1_Preview, FN2CModelMetadata(
		TEXT("o1-preview-2024-09-12"),
		TEXT("o1 Preview"),
		EN2CLLMProvider::OpenAI,
		15.0f, 60.0f,
		128000,
		32768,
		false,
		true,
		true,
		1.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// o1 Mini (NO system prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o1_Mini, FN2CModelMetadata(
		TEXT("o1-mini-2024-09-12"),
		TEXT("o1 Mini"),
		EN2CLLMProvider::OpenAI,
		1.1f, 4.4f,
		128000,
		65536,
		false,
		true,
		true,
		1.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-4.1
	OpenAIModels.Add(EN2COpenAIModel::GPT_4_1, FN2CModelMetadata(
		TEXT("gpt-4.1"),
		TEXT("GPT-4.1"),
		EN2CLLMProvider::OpenAI,
		2.0f, 8.0f,
		1000000,              // 1M context window
		32768,
		true,
		true,
		false,                // Not a reasoning model
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: OpenAI chat model: temperature and max_tokens accepted
		false                 // Uses max_completion_tokens
	));

	// GPT-4o
	OpenAIModels.Add(EN2COpenAIModel::GPT4o_2024_08_06, FN2CModelMetadata(
		TEXT("gpt-4o-2024-08-06"),
		TEXT("GPT-4o"),
		EN2CLLMProvider::OpenAI,
		2.5f, 10.0f,
		128000,
		16384,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: OpenAI chat model: temperature and max_tokens accepted
		false                 // Uses max_completion_tokens
	));

	// GPT-4o Mini
	OpenAIModels.Add(EN2COpenAIModel::GPT4o_Mini_2024_07_18, FN2CModelMetadata(
		TEXT("gpt-4o-mini-2024-07-18"),
		TEXT("GPT-4o Mini"),
		EN2CLLMProvider::OpenAI,
		0.15f, 0.6f,
		128000,
		16384,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: OpenAI chat model: temperature and max_tokens accepted
		false                 // Uses max_completion_tokens
	));

	// ========================================================================
	// Anthropic Models
	// ========================================================================

	// Claude Opus 4.5
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_5_Opus, FN2CModelMetadata(
		TEXT("claude-opus-4-5-20251101"),
		TEXT("Claude Opus 4.5"),
		EN2CLLMProvider::Anthropic,
		5.0f, 25.0f,
		200000,
		64000,
		true,
		false,                // Anthropic uses tool use for structured output, not native
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude Sonnet 4.5
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_5_Sonnet, FN2CModelMetadata(
		TEXT("claude-sonnet-4-5-20250929"),
		TEXT("Claude Sonnet 4.5"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude Opus 4.1
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_1_Opus, FN2CModelMetadata(
		TEXT("claude-opus-4-1-20250805"),
		TEXT("Claude Opus 4.1"),
		EN2CLLMProvider::Anthropic,
		15.0f, 75.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude Opus 4
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_Opus, FN2CModelMetadata(
		TEXT("claude-opus-4-20250514"),
		TEXT("Claude Opus 4"),
		EN2CLLMProvider::Anthropic,
		15.0f, 75.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude Sonnet 4
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_Sonnet, FN2CModelMetadata(
		TEXT("claude-sonnet-4-20250514"),
		TEXT("Claude Sonnet 4"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude 3.7 Sonnet
	AnthropicModels.Add(EN2CAnthropicModel::Claude3_7_Sonnet, FN2CModelMetadata(
		TEXT("claude-3-7-sonnet-20250219"),
		TEXT("Claude 3.7 Sonnet"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude 3.5 Sonnet
	AnthropicModels.Add(EN2CAnthropicModel::Claude3_5_Sonnet, FN2CModelMetadata(
		TEXT("claude-3-5-sonnet-20241022"),
		TEXT("Claude 3.5 Sonnet"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		8192,
		true,
		false,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude 3.5 Haiku
	AnthropicModels.Add(EN2CAnthropicModel::Claude3_5_Haiku, FN2CModelMetadata(
		TEXT("claude-3-5-haiku-20241022"),
		TEXT("Claude 3.5 Haiku"),
		EN2CLLMProvider::Anthropic,
		0.8f, 4.0f,
		200000,
		8192,
		true,
		false,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// ========================================================================
	// Gemini Models
	// ========================================================================

	// Gemini 3 Pro Preview
	GeminiModels.Add(EN2CGeminiModel::Gemini_3_Pro_Preview, FN2CModelMetadata(
		TEXT("gemini-3-pro-preview"),
		TEXT("Gemini 3 Pro Preview"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Preview - pricing TBD
		1000000,
		65536,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	));

	// Gemini 3 Flash Preview
	GeminiModels.Add(EN2CGeminiModel::Gemini_3_Flash_Preview, FN2CModelMetadata(
		TEXT("gemini-3-flash-preview"),
		TEXT("Gemini 3 Flash Preview"),
		EN2CLLMProvider::Gemini,
		0.5f, 3.0f,           // $0.50/1M input, $3/1M output
		200000,               // 200K context (not 1M like Pro)
		65536,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.5 Pro
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_5_Pro, FN2CModelMetadata(
		TEXT("gemini-2.5-pro-preview-05-06"),
		TEXT("Gemini 2.5 Pro Preview"),
		EN2CLLMProvider::Gemini,
		1.25f, 10.0f,
		1000000,
		65536,
		true,
		true,
		false,
		1.0f,                 // Default temperature (more reliable structured output at 1.0)
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.5 Flash
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_5_Flash, FN2CModelMetadata(
		TEXT("gemini-2.5-flash-preview-05-20"),
		TEXT("Gemini 2.5 Flash Preview"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Preview - pricing TBD
		1000000,
		65536,
		true,
		true,
		false,
		0.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.0 Flash
	GeminiModels.Add(EN2CGeminiModel::Gemini_Flash_2_0, FN2CModelMetadata(
		TEXT("gemini-2.0-flash"),
		TEXT("Gemini 2.0 Flash"),
		EN2CLLMProvider::Gemini,
		0.1f, 0.4f,
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.0 Flash Lite Preview
	GeminiModels.Add(EN2CGeminiModel::Gemini_Flash_Lite_2_0, FN2CModelMetadata(
		TEXT("gemini-2.0-flash-lite-preview-02-05"),
		TEXT("Gemini 2.0 Flash-Lite-Preview-02-05"),
		EN2CLLMProvider::Gemini,
		0.075f, 0.3f,
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 1.5 Flash
	GeminiModels.Add(EN2CGeminiModel::Gemini_1_5_Flash, FN2CModelMetadata(
		TEXT("gemini-1.5-flash"),
		TEXT("Gemini 1.5 Flash"),
		EN2CLLMProvider::Gemini,
		0.075f, 0.3f,
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 1.5 Pro
	GeminiModels.Add(EN2CGeminiModel::Gemini_1_5_Pro, FN2CModelMetadata(
		TEXT("gemini-1.5-pro"),
		TEXT("Gemini 1.5 Pro"),
		EN2CLLMProvider::Gemini,
		1.25f, 5.0f,
		2000000,              // 2M context window
		8192,
		true,
		true,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.0 Pro Exp
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_0_ProExp_02_05, FN2CModelMetadata(
		TEXT("gemini-2.0-pro-exp-02-05"),
		TEXT("Gemini 2.0 Pro Exp 02-05"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Experimental - free
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.0 Flash Thinking Exp (reasoning model)
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_0_FlashThinkingExp, FN2CModelMetadata(
		TEXT("gemini-2.0-flash-thinking-exp-01-21"),
		TEXT("Gemini 2.0 Flash Thinking Exp 01-21"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Experimental - free
		1000000,
		65536,
		true,
		true,
		true,                 // This is a reasoning/thinking model
		1.0f,
		16000,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Thinking model: no sampling fields
		false                 // Uses max_completion_tokens
	));

	// ========================================================================
	// DeepSeek Models
	// ========================================================================

	// DeepSeek R1 (reasoning model)
	DeepSeekModels.Add(EN2CDeepSeekModel::DeepSeek_R1, FN2CModelMetadata(
		TEXT("deepseek-reasoner"),
		TEXT("DeepSeek R1"),
		EN2CLLMProvider::DeepSeek,
		0.55f, 2.19f,         // Cache miss pricing
		64000,
		32768,                // 32K max output (includes reasoning chains)
		true,
		true,
		true,                 // Is reasoning model
		1.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: DeepSeek reasoner ignores sampling parameters
		false                 // Uses max_completion_tokens
	));

	// DeepSeek V3
	DeepSeekModels.Add(EN2CDeepSeekModel::DeepSeek_V3, FN2CModelMetadata(
		TEXT("deepseek-chat"),
		TEXT("DeepSeek V3"),
		EN2CLLMProvider::DeepSeek,
		0.27f, 1.1f,          // Cache miss pricing
		64000,
		8192,
		true,
		true,
		false,
		0.0f,
		8192,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: DeepSeek chat accepts temperature
		false                 // Uses max_completion_tokens
	));

	// ========================================================================
	// Build Model ID Index for fast lookup
	// ========================================================================

	for (auto& Pair : OpenAIModels)
	{
		ModelIdIndex.Add(Pair.Value.ModelId, &Pair.Value);
	}
	for (auto& Pair : AnthropicModels)
	{
		ModelIdIndex.Add(Pair.Value.ModelId, &Pair.Value);
	}
	for (auto& Pair : GeminiModels)
	{
		ModelIdIndex.Add(Pair.Value.ModelId, &Pair.Value);
	}
	for (auto& Pair : DeepSeekModels)
	{
		ModelIdIndex.Add(Pair.Value.ModelId, &Pair.Value);
	}
}

// ============================================================================
// Model Metadata Accessors
// ============================================================================

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2COpenAIModel Model) const
{
	return OpenAIModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2CAnthropicModel Model) const
{
	return AnthropicModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2CGeminiModel Model) const
{
	return GeminiModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadata(EN2CDeepSeekModel Model) const
{
	return DeepSeekModels.Find(Model);
}

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadataById(const FString& ModelId) const
{
	const FN2CModelMetadata* const* Found = ModelIdIndex.Find(ModelId);
	return Found ? *Found : nullptr;
}

// ============================================================================
// Model ID Accessors
// ============================================================================

FString FN2CLLMModelRegistry::GetModelId(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("gpt-4o-2024-08-06"); // Default fallback
}

FString FN2CLLMModelRegistry::GetModelId(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("claude-sonnet-4-5-20250929"); // Default fallback
}

FString FN2CLLMModelRegistry::GetModelId(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("gemini-3-flash-preview"); // Default fallback
}

FString FN2CLLMModelRegistry::GetModelId(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("deepseek-reasoner"); // Default fallback
}

// ============================================================================
// Capability Checks
// ============================================================================

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true; // Default to true
}

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true;
}

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true;
}

bool FN2CLLMModelRegistry::SupportsSystemPrompts(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bSupportsSystemPrompts;
	}
	return true;
}

bool FN2CLLMModelRegistry::IsReasoningModel(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bIsReasoningModel;
	}
	return false;
}

bool FN2CLLMModelRegistry::IsReasoningModel(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bIsReasoningModel;
	}
	return false;
}

bool FN2CLLMModelRegistry::IsReasoningModel(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bIsReasoningModel;
	}
	return false;
}

bool FN2CLLMModelRegistry::IsReasoningModel(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bIsReasoningModel;
	}
	return false;
}

bool FN2CLLMModelRegistry::RequiresResponsesAPI(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->bRequiresResponsesAPI;
	}
	return false;
}

bool FN2CLLMModelRegistry::RequiresResponsesAPI(const FString& ModelId) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadataById(ModelId))
	{
		return Meta->bRequiresResponsesAPI;
	}
	return false;
}

// ============================================================================
// Pricing Accessors (Backward Compatibility)
// ============================================================================

FN2COpenAIPricing FN2CLLMModelRegistry::GetOpenAIPricing(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2COpenAIPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2COpenAIPricing();
}

FN2CAnthropicPricing FN2CLLMModelRegistry::GetAnthropicPricing(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2CAnthropicPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2CAnthropicPricing();
}

FN2CGeminiPricing FN2CLLMModelRegistry::GetGeminiPricing(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2CGeminiPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2CGeminiPricing();
}

FN2CDeepSeekPricing FN2CLLMModelRegistry::GetDeepSeekPricing(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return FN2CDeepSeekPricing(Meta->InputCostPer1M, Meta->OutputCostPer1M);
	}
	return FN2CDeepSeekPricing();
}

// ============================================================================
// Generation Defaults
// ============================================================================

float FN2CLLMModelRegistry::GetDefaultTemperature(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultTemperature;
	}
	return 0.0f;
}

float FN2CLLMModelRegistry::GetDefaultTemperature(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultTemperature;
	}
	return 0.0f;
}

float FN2CLLMModelRegistry::GetDefaultTemperature(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultTemperature;
	}
	return 0.0f;
}

float FN2CLLMModelRegistry::GetDefaultTemperature(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultTemperature;
	}
	return 0.0f;
}

int32 FN2CLLMModelRegistry::GetDefaultMaxTokens(EN2COpenAIModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultMaxTokens;
	}
	return 16000;
}

int32 FN2CLLMModelRegistry::GetDefaultMaxTokens(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultMaxTokens;
	}
	return 16000;
}

int32 FN2CLLMModelRegistry::GetDefaultMaxTokens(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultMaxTokens;
	}
	return 16000;
}

int32 FN2CLLMModelRegistry::GetDefaultMaxTokens(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->DefaultMaxTokens;
	}
	return 8192;
}

// ============================================================================
// Provider Enumeration
// ============================================================================

TArray<FN2CModelMetadata> FN2CLLMModelRegistry::GetAllModelsForProvider(EN2CLLMProvider Provider) const
{
	TArray<FN2CModelMetadata> Result;

	switch (Provider)
	{
	case EN2CLLMProvider::OpenAI:
		for (const auto& Pair : OpenAIModels)
		{
			Result.Add(Pair.Value);
		}
		break;
	case EN2CLLMProvider::Anthropic:
		for (const auto& Pair : AnthropicModels)
		{
			Result.Add(Pair.Value);
		}
		break;
	case EN2CLLMProvider::Gemini:
		for (const auto& Pair : GeminiModels)
		{
			Result.Add(Pair.Value);
		}
		break;
	case EN2CLLMProvider::DeepSeek:
		for (const auto& Pair : DeepSeekModels)
		{
			Result.Add(Pair.Value);
		}
		break;
	default:
		// Ollama and LM Studio don't have predefined models
		break;
	}

	return Result;
}
