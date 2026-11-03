// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/IN2CWireDialect.h"

/**
 * OpenAI Chat Completions as every OpenAI-compatible host speaks it (card 0218): vLLM, llama.cpp, LM Studio, Ollama's
 * /v1, DeepSeek, OpenRouter, Groq, Together, Fireworks, xAI, Mistral, Azure OpenAI v1, Bedrock, Gemini's OpenAI
 * endpoint, MiniMax, Kimi, Z.ai and profiles of your own. What differs between them is profile data:
 * - response_format from StructuredOutput (json_schema with strict as a JSON boolean, json_schema, json_object, none);
 * - the budget in MaxTokensField (max_tokens or max_completion_tokens), only when the request has one;
 * - ThinkingOn / ThinkingOff merged into the body when the request asks, with the level in ReasoningEffortField;
 * - ExtraBody merged into every body, ExtraHeaders sent with every request.
 * The model comes from the request (or the profile's default), never from settings or a global. No rule here looks
 * at a model's name.
 */
class NODETOCODE_API FN2COpenAIChatDialect : public IN2CWireDialect
{
public:
	static const FN2COpenAIChatDialect& Get();

	virtual FName GetName() const override { return FName(TEXT("openai_chat")); }

	virtual bool EncodeRequest(const FN2CLLMRequest& Request, const FN2CProviderProfile& Profile, FN2CHttpRequestSpec& Out,
		FString& OutError) const override;

	virtual FN2CLLMResult DecodeResponse(const FString& Body, const FN2CProviderProfile& Profile, const FString& OutputBudgetHint) const override;

	/** The body EncodeRequest sends, as a JSON object (specs and the service's payload logging) */
	static TSharedRef<FJsonObject> BuildBody(const FN2CLLMRequest& Request, const FN2CProviderProfile& Profile);

	/** Copies every field of Fragment into Target, merging nested objects field by field */
	static void MergeInto(const TSharedRef<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Fragment);
};
