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
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-5.2 Pro - More compute for harder problems (requires Responses API)
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_2_Pro, FN2CModelMetadata(
		TEXT("gpt-5.2-pro"),
		TEXT("GPT-5.2 Pro"),
		EN2CLLMProvider::OpenAI,
		21.0f, 168.0f,        // developers.openai.com/api/docs/models/gpt-5.2-pro (2026-10-22)
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		true,                 // Requires OpenAI Responses API
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	));

	// GPT-5 - Strong coding model (August 2025)
	OpenAIModels.Add(EN2COpenAIModel::GPT_5, FN2CModelMetadata(
		TEXT("gpt-5"),
		TEXT("GPT-5 (shuts down 2026-12-11)"),
		EN2CLLMProvider::OpenAI,
		1.25f, 10.0f,
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).ShutsDown(TEXT("2026-12-11"));

	// GPT-5 Mini - Faster, cost-efficient
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_Mini, FN2CModelMetadata(
		TEXT("gpt-5-mini"),
		TEXT("GPT-5 Mini (shuts down 2026-12-11)"),
		EN2CLLMProvider::OpenAI,
		0.25f, 2.0f,
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).ShutsDown(TEXT("2026-12-11"));

	// GPT-5 Nano - Smallest, fastest, most affordable
	OpenAIModels.Add(EN2COpenAIModel::GPT_5_Nano, FN2CModelMetadata(
		TEXT("gpt-5-nano"),
		TEXT("GPT-5 Nano (shuts down 2026-12-11)"),
		EN2CLLMProvider::OpenAI,
		0.05f, 0.4f,
		400000,
		128000,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).ShutsDown(TEXT("2026-12-11"));

	// o4 Mini - Reasoning model (supports developer prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o4_mini, FN2CModelMetadata(
		TEXT("o4-mini"),
		TEXT("o4 Mini (retired)"),
		EN2CLLMProvider::OpenAI,
		1.1f, 4.4f,           // Pricing per 1M tokens
		200000,               // Context window
		100000,               // Max output
		true,                 // Supports system prompts (developer messages)
		true,                 // Supports structured output
		true,                 // Is reasoning model
		1.0f,                 // Default temperature (required for reasoning)
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).Retired(TEXT("2026-10-23"));

	// o3 - Advanced reasoning model
	OpenAIModels.Add(EN2COpenAIModel::GPT_o3, FN2CModelMetadata(
		TEXT("o3"),
		TEXT("o3 (shuts down 2026-12-11)"),
		EN2CLLMProvider::OpenAI,
		10.0f, 40.0f,
		200000,
		100000,
		true,                 // Supports developer prompts
		true,                 // Structured output: strict json_schema (Structured Outputs supported)
		true,                 // Is reasoning model
		1.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).ShutsDown(TEXT("2026-12-11"));

	// o3 Mini - Smaller o3 variant
	OpenAIModels.Add(EN2COpenAIModel::GPT_o3_mini, FN2CModelMetadata(
		TEXT("o3-mini"),
		TEXT("o3 Mini (retired)"),
		EN2CLLMProvider::OpenAI,
		1.1f, 4.4f,
		200000,
		100000,
		true,
		true,                 // Structured output: strict json_schema (Structured Outputs supported)
		true,
		1.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).Retired(TEXT("2026-10-23"));

	// o1 - Original reasoning model (NO system prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o1, FN2CModelMetadata(
		TEXT("o1"),
		TEXT("o1 (retired)"),
		EN2CLLMProvider::OpenAI,
		15.0f, 60.0f,
		200000,
		100000,
		false,                // Does NOT support system prompts
		true,                 // Structured output: strict json_schema (Structured Outputs supported)
		true,
		1.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).Retired(TEXT("2026-10-23"));

	// o1 Preview (NO system prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o1_Preview, FN2CModelMetadata(
		TEXT("o1-preview-2024-09-12"),
		TEXT("o1 Preview (retired)"),
		EN2CLLMProvider::OpenAI,
		15.0f, 60.0f,
		128000,
		32768,
		false,
		false,                // No response_format at all (neither json_schema nor json_object)
		true,
		1.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).Retired(TEXT("2025-07-28"));

	// o1 Mini (NO system prompts)
	OpenAIModels.Add(EN2COpenAIModel::GPT_o1_Mini, FN2CModelMetadata(
		TEXT("o1-mini-2024-09-12"),
		TEXT("o1 Mini (retired)"),
		EN2CLLMProvider::OpenAI,
		1.1f, 4.4f,
		128000,
		65536,
		false,
		false,                // No response_format at all (neither json_schema nor json_object)
		true,
		1.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p, max_completion_tokens
		true                  // Uses max_completion_tokens
	)).Retired(TEXT("2025-10-27"));

	// GPT-4.1
	OpenAIModels.Add(EN2COpenAIModel::GPT_4_1, FN2CModelMetadata(
		TEXT("gpt-4.1"),
		TEXT("GPT-4.1 (shuts down 2027-01-14)"),
		EN2CLLMProvider::OpenAI,
		2.0f, 8.0f,
		1000000,              // 1M context window
		32768,
		true,
		true,
		false,                // Not a reasoning model
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: OpenAI chat model: temperature and max_tokens accepted
		false                 // Uses max_completion_tokens
	)).ShutsDown(TEXT("2027-01-14"));

	// GPT-4o
	OpenAIModels.Add(EN2COpenAIModel::GPT4o_2024_08_06, FN2CModelMetadata(
		TEXT("gpt-4o-2024-08-06"),
		TEXT("GPT-4o (shuts down 2027-01-14)"),
		EN2CLLMProvider::OpenAI,
		2.5f, 10.0f,
		128000,
		16384,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: OpenAI chat model: temperature and max_tokens accepted
		false                 // Uses max_completion_tokens
	)).ShutsDown(TEXT("2027-01-14"));

	// GPT-4o Mini
	OpenAIModels.Add(EN2COpenAIModel::GPT4o_Mini_2024_07_18, FN2CModelMetadata(
		TEXT("gpt-4o-mini-2024-07-18"),
		TEXT("GPT-4o Mini (shuts down 2027-01-14)"),
		EN2CLLMProvider::OpenAI,
		0.15f, 0.6f,
		128000,
		16384,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: OpenAI chat model: temperature and max_tokens accepted
		false                 // Uses max_completion_tokens
	)).ShutsDown(TEXT("2027-01-14"));

	// GPT-6 family. Limits and prices from developers.openai.com/api/docs/models/<id> (checked 2026-10-22).
	// All three are reasoning models (reasoning.effort): no temperature, max_completion_tokens. Chat Completions
	// is supported on each (gpt-6.1-sol there without tool calling, which translation does not use).

	// GPT-6.1 Sol - near-Astra quality at a lower cost (default)
	OpenAIModels.Add(EN2COpenAIModel::GPT_6_1_Sol, FN2CModelMetadata(
		TEXT("gpt-6.1-sol"),
		TEXT("GPT-6.1 Sol"),
		EN2CLLMProvider::OpenAI,
		2.0f, 10.0f,          // Pricing per 1M tokens
		1050000,              // Context window (922,000 max input)
		128000,               // Max output
		true,                 // Supports system prompts (developer messages)
		true,                 // Structured output: strict json_schema
		true,                 // Is reasoning model
		0.0f,                 // Default temperature (not sent)
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p
		true                  // Uses max_completion_tokens
	));

	// GPT-6 Astra - most capable
	OpenAIModels.Add(EN2COpenAIModel::GPT_6_Astra, FN2CModelMetadata(
		TEXT("gpt-6-astra"),
		TEXT("GPT-6 Astra"),
		EN2CLLMProvider::OpenAI,
		10.0f, 50.0f,
		1050000,
		128000,
		true,
		true,
		true,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p
		true                  // Uses max_completion_tokens
	));

	// GPT-6 Luna - most efficient
	OpenAIModels.Add(EN2COpenAIModel::GPT_6_Luna, FN2CModelMetadata(
		TEXT("gpt-6-luna"),
		TEXT("GPT-6 Luna"),
		EN2CLLMProvider::OpenAI,
		0.1f, 0.5f,
		1050000,
		128000,
		true,
		true,
		true,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: OpenAI reasoning model: no temperature/top_p
		true                  // Uses max_completion_tokens
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
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	));

	// Claude Sonnet 4.5
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_5_Sonnet, FN2CModelMetadata(
		TEXT("claude-sonnet-4-5-20250929"),
		TEXT("Claude Sonnet 4.5 (retires 2026-11-30)"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).ShutsDown(TEXT("2026-11-30"));

	// Claude Opus 4.1
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_1_Opus, FN2CModelMetadata(
		TEXT("claude-opus-4-1-20250805"),
		TEXT("Claude Opus 4.1 (retired)"),
		EN2CLLMProvider::Anthropic,
		15.0f, 75.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-08-05"));

	// Claude Opus 4
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_Opus, FN2CModelMetadata(
		TEXT("claude-opus-4-20250514"),
		TEXT("Claude Opus 4 (retired)"),
		EN2CLLMProvider::Anthropic,
		15.0f, 75.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-06-15"));

	// Claude Sonnet 4
	AnthropicModels.Add(EN2CAnthropicModel::Claude4_Sonnet, FN2CModelMetadata(
		TEXT("claude-sonnet-4-20250514"),
		TEXT("Claude Sonnet 4 (retired)"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-06-15"));

	// Claude 3.7 Sonnet
	AnthropicModels.Add(EN2CAnthropicModel::Claude3_7_Sonnet, FN2CModelMetadata(
		TEXT("claude-3-7-sonnet-20250219"),
		TEXT("Claude 3.7 Sonnet (retired)"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		64000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-02-19"));

	// Claude 3.5 Sonnet
	AnthropicModels.Add(EN2CAnthropicModel::Claude3_5_Sonnet, FN2CModelMetadata(
		TEXT("claude-3-5-sonnet-20241022"),
		TEXT("Claude 3.5 Sonnet (retired)"),
		EN2CLLMProvider::Anthropic,
		3.0f, 15.0f,
		200000,
		8192,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2025-10-28"));

	// Claude 3.5 Haiku
	AnthropicModels.Add(EN2CAnthropicModel::Claude3_5_Haiku, FN2CModelMetadata(
		TEXT("claude-3-5-haiku-20241022"),
		TEXT("Claude 3.5 Haiku (retired)"),
		EN2CLLMProvider::Anthropic,
		0.8f, 4.0f,
		200000,
		8192,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Claude 4.5 and older accept temperature (4.7+ reject it)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-02-19"));

	// Claude 5.5 family. IDs, limits and prices from platform.claude.com/docs/en/about-claude/models/overview
	// (checked 2026-10-22): 1M context, 128K max output, adaptive thinking. Sampling parameters are rejected
	// (removed in API version 2026-10-21, and 400 on Claude 4.7+ for non-default values).

	// Claude Sonnet 5.5 (default)
	AnthropicModels.Add(EN2CAnthropicModel::Claude5_5_Sonnet, FN2CModelMetadata(
		TEXT("claude-sonnet-5-5"),
		TEXT("Claude Sonnet 5.5"),
		EN2CLLMProvider::Anthropic,
		2.0f, 10.0f,
		1000000,
		128000,
		true,
		false,                // Anthropic uses tool use for structured output, not native
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Claude 4.7 and later reject temperature/top_p/top_k
		false                 // Uses max_completion_tokens
	));

	// Claude Opus 5.5
	AnthropicModels.Add(EN2CAnthropicModel::Claude5_5_Opus, FN2CModelMetadata(
		TEXT("claude-opus-5-5"),
		TEXT("Claude Opus 5.5"),
		EN2CLLMProvider::Anthropic,
		4.0f, 20.0f,
		1000000,
		128000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Claude 4.7 and later reject temperature/top_p/top_k
		false                 // Uses max_completion_tokens
	));

	// Claude Haiku 5.5
	AnthropicModels.Add(EN2CAnthropicModel::Claude5_5_Haiku, FN2CModelMetadata(
		TEXT("claude-haiku-5-5"),
		TEXT("Claude Haiku 5.5"),
		EN2CLLMProvider::Anthropic,
		0.1f, 0.5f,           // Prompts up to 100K tokens; $0.50/$2.50 beyond
		1000000,
		128000,
		true,
		false,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Claude 4.7 and later reject temperature/top_p/top_k
		false                 // Uses max_completion_tokens
	));

	// ========================================================================
	// Gemini Models
	// ========================================================================

	// Gemini 3 Pro Preview
	GeminiModels.Add(EN2CGeminiModel::Gemini_3_Pro_Preview, FN2CModelMetadata(
		TEXT("gemini-3-pro-preview"),
		TEXT("Gemini 3 Pro Preview (retired)"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Preview - pricing TBD
		1000000,
		65536,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-03-09"));

	// Gemini 3 Flash Preview
	GeminiModels.Add(EN2CGeminiModel::Gemini_3_Flash_Preview, FN2CModelMetadata(
		TEXT("gemini-3-flash-preview"),
		TEXT("Gemini 3 Flash Preview"),
		EN2CLLMProvider::Gemini,
		0.5f, 3.0f,           // $0.50/1M input, $3/1M output
		1048576,              // 1,048,576 input tokens (ai.google.dev model page, 2026-10-22)
		65536,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	));

	// Gemini 2.5 Pro
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_5_Pro, FN2CModelMetadata(
		TEXT("gemini-2.5-pro-preview-05-06"),
		TEXT("Gemini 2.5 Pro Preview (retired)"),
		EN2CLLMProvider::Gemini,
		1.25f, 10.0f,
		1000000,
		65536,
		true,
		true,
		false,
		1.0f,                 // Default temperature (more reliable structured output at 1.0)
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2025-12-02"));

	// Gemini 2.5 Flash
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_5_Flash, FN2CModelMetadata(
		TEXT("gemini-2.5-flash-preview-05-20"),
		TEXT("Gemini 2.5 Flash Preview (retired)"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Preview - pricing TBD
		1000000,
		65536,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2025-11-18"));

	// Gemini 2.0 Flash
	GeminiModels.Add(EN2CGeminiModel::Gemini_Flash_2_0, FN2CModelMetadata(
		TEXT("gemini-2.0-flash"),
		TEXT("Gemini 2.0 Flash (retired)"),
		EN2CLLMProvider::Gemini,
		0.1f, 0.4f,
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2026-06-01"));

	// Gemini 2.0 Flash Lite Preview
	GeminiModels.Add(EN2CGeminiModel::Gemini_Flash_Lite_2_0, FN2CModelMetadata(
		TEXT("gemini-2.0-flash-lite-preview-02-05"),
		TEXT("Gemini 2.0 Flash-Lite-Preview-02-05 (retired)"),
		EN2CLLMProvider::Gemini,
		0.075f, 0.3f,
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT("2025-12-09"));

	// Gemini 1.5 Flash
	GeminiModels.Add(EN2CGeminiModel::Gemini_1_5_Flash, FN2CModelMetadata(
		TEXT("gemini-1.5-flash"),
		TEXT("Gemini 1.5 Flash (retired)"),
		EN2CLLMProvider::Gemini,
		0.075f, 0.3f,
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT(""));

	// Gemini 1.5 Pro
	GeminiModels.Add(EN2CGeminiModel::Gemini_1_5_Pro, FN2CModelMetadata(
		TEXT("gemini-1.5-pro"),
		TEXT("Gemini 1.5 Pro (retired)"),
		EN2CLLMProvider::Gemini,
		1.25f, 5.0f,
		2000000,              // 2M context window
		8192,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT(""));

	// Gemini 2.0 Pro Exp
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_0_ProExp_02_05, FN2CModelMetadata(
		TEXT("gemini-2.0-pro-exp-02-05"),
		TEXT("Gemini 2.0 Pro Exp 02-05 (retired)"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Experimental - free
		1000000,
		8192,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: Gemini 1.5-2.5 accept temperature
		false                 // Uses max_completion_tokens
	)).Retired(TEXT(""));

	// Gemini 2.0 Flash Thinking Exp (reasoning model)
	GeminiModels.Add(EN2CGeminiModel::Gemini_2_0_FlashThinkingExp, FN2CModelMetadata(
		TEXT("gemini-2.0-flash-thinking-exp-01-21"),
		TEXT("Gemini 2.0 Flash Thinking Exp 01-21 (retired)"),
		EN2CLLMProvider::Gemini,
		0.0f, 0.0f,           // Experimental - free
		1000000,
		65536,
		true,
		true,
		true,                 // This is a reasoning/thinking model
		1.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Thinking model: no sampling fields
		false                 // Uses max_completion_tokens
	)).Retired(TEXT(""));

	// Gemini 3.5+ and 4 (stable). Limits from ai.google.dev/gemini-api/docs/models/<id>, prices from
	// ai.google.dev/gemini-api/docs/pricing (standard paid tier, checked 2026-10-22). Gemini 3 and later keep
	// Google's default temperature.

	// Gemini 3.8 Flash (default). Introductory price; Google lists $1.50/$7.50 from 2027-01-01.
	GeminiModels.Add(EN2CGeminiModel::Gemini_3_8_Flash, FN2CModelMetadata(
		TEXT("gemini-3.8-flash"),
		TEXT("Gemini 3.8 Flash"),
		EN2CLLMProvider::Gemini,
		0.75f, 3.75f,
		1048576,              // Input token limit
		65536,                // Output token limit
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	));

	// Gemini 4 Argon. Unified 1,048,576-token input+output window; 65,536 output by default.
	// Introductory price; Google lists $4/$20 from 2026-12-01.
	GeminiModels.Add(EN2CGeminiModel::Gemini_4_Argon, FN2CModelMetadata(
		TEXT("gemini-4-argon"),
		TEXT("Gemini 4 Argon"),
		EN2CLLMProvider::Gemini,
		2.0f, 10.0f,
		1048576,
		65536,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	));

	// Gemini 3.5 Flash-Lite
	GeminiModels.Add(EN2CGeminiModel::Gemini_3_5_Flash_Lite, FN2CModelMetadata(
		TEXT("gemini-3.5-flash-lite"),
		TEXT("Gemini 3.5 Flash-Lite"),
		EN2CLLMProvider::Gemini,
		0.3f, 2.5f,
		1048576,
		65536,
		true,
		true,
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: Gemini 3: keep Google's default temperature (lower values can loop)
		false                 // Uses max_completion_tokens
	));

	// ========================================================================
	// DeepSeek Models
	// ========================================================================

	// Checked 2026-10-22 against api-docs.deepseek.com (quick_start/pricing, api/create-chat-completion,
	// guides/thinking_mode, updates). The API takes two model IDs, deepseek-flash and deepseek-v4-pro, each with
	// a 1M context and up to 384K (393216) output tokens. Thinking is switched per request (thinking.type, plus a
	// top-level reasoning_effort of low/high/max) and is on by default. temperature has no effect in thinking
	// mode. deepseek-chat and deepseek-reasoner were discontinued 2026-07-24, so the DeepSeek_R1 and DeepSeek_V3
	// enumerators now select thinking modes of deepseek-flash. Entries that share an ID differ in ReasoningEffort.
	// Prices are the peak-hour cache-miss rates (off-peak is half).

	// Registered first, so it is the entry GetModelMetadataById("deepseek-flash") returns: thinking on at high
	// effort is the API's own default for the ID. No response_format: the docs do not say JSON Output works
	// with thinking on.
	DeepSeekModels.Add(EN2CDeepSeekModel::DeepSeek_R1, FN2CModelMetadata(
		TEXT("deepseek-flash"),
		TEXT("DeepSeek Flash, thinking (high effort)"),
		EN2CLLMProvider::DeepSeek,
		0.3f, 1.2f,
		1000000,
		393216,
		true,
		false,                // Structured output: no response_format with thinking on
		true,                 // Is reasoning model
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: temperature has no effect in thinking mode
		false                 // Uses max_completion_tokens
	).WithReasoningEffort(TEXT("high")));

	DeepSeekModels.Add(EN2CDeepSeekModel::DeepSeek_V3, FN2CModelMetadata(
		TEXT("deepseek-flash"),
		TEXT("DeepSeek Flash, no thinking (JSON output)"),
		EN2CLLMProvider::DeepSeek,
		0.3f, 1.2f,
		1000000,
		393216,
		true,
		true,                 // Structured output: response_format json_object
		false,
		0.0f,
		false,                // Chat Completions (not Responses API)
		true,                 // Accepts sampling params: temperature applies in non-thinking mode
		false                 // Uses max_completion_tokens
	).WithReasoningEffort(TEXT("none")));

	DeepSeekModels.Add(EN2CDeepSeekModel::DeepSeek_V4_Pro, FN2CModelMetadata(
		TEXT("deepseek-v4-pro"),
		TEXT("DeepSeek V4 Pro, thinking (high effort)"),
		EN2CLLMProvider::DeepSeek,
		1.32f, 3.96f,
		1000000,
		393216,
		true,
		false,                // Structured output: no response_format with thinking on
		true,                 // Is reasoning model
		0.0f,
		false,                // Chat Completions (not Responses API)
		false,                // Accepts sampling params: temperature has no effect in thinking mode
		false                 // Uses max_completion_tokens
	).WithReasoningEffort(TEXT("high")));

	// ========================================================================
	// Build Model ID Index for fast lookup
	// ========================================================================

	// For an ID shared by several entries the index keeps the first live one registered (maps iterate in
	// insertion order); a retired entry only fills an ID no live entry has.
	auto IndexEntry = [this](const FN2CModelMetadata& Entry)
	{
		ModelIdEntries.Add(Entry.ModelId, &Entry);
		const FN2CModelMetadata*& Slot = ModelIdIndex.FindOrAdd(Entry.ModelId, nullptr);
		if (!Slot || (Slot->bIsRetired && !Entry.bIsRetired))
		{
			Slot = &Entry;
		}
	};
	for (const auto& Pair : OpenAIModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const auto& Pair : AnthropicModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const auto& Pair : GeminiModels)
	{
		IndexEntry(Pair.Value);
	}
	for (const auto& Pair : DeepSeekModels)
	{
		IndexEntry(Pair.Value);
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

const FN2CModelMetadata* FN2CLLMModelRegistry::GetModelMetadataById(const FString& ModelId, const FString& ReasoningEffort) const
{
	if (!ReasoningEffort.IsEmpty())
	{
		TArray<const FN2CModelMetadata*, TInlineAllocator<4>> Entries;
		ModelIdEntries.MultiFind(ModelId, Entries, /*bMaintainOrder*/ true);
		for (const FN2CModelMetadata* Entry : Entries)
		{
			if (Entry->ReasoningEffort == ReasoningEffort && !Entry->bIsRetired)
			{
				return Entry;
			}
		}
	}
	return GetModelMetadataById(ModelId);
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
	return TEXT("gpt-6.1-sol"); // Fallback: N2CDefaultModels::OpenAI
}

FString FN2CLLMModelRegistry::GetModelId(EN2CAnthropicModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("claude-sonnet-5-5"); // Fallback: N2CDefaultModels::Anthropic
}

FString FN2CLLMModelRegistry::GetModelId(EN2CGeminiModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("gemini-3.8-flash"); // Fallback: N2CDefaultModels::Gemini
}

FString FN2CLLMModelRegistry::GetModelId(EN2CDeepSeekModel Model) const
{
	if (const FN2CModelMetadata* Meta = GetModelMetadata(Model))
	{
		return Meta->ModelId;
	}
	return TEXT("deepseek-flash"); // Fallback: N2CDefaultModels::DeepSeek
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
