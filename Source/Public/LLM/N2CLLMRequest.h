// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Models/N2CTranslation.h"

/**
 * One LLM request as a wire dialect sees it (card 0218): everything a request carries, with nothing read from
 * process-wide settings or statics. The service that builds it resolves the settings, the model's catalog row and
 * the output budget first; the dialect only encodes.
 */
struct NODETOCODE_API FN2CLLMRequest
{
	/** The model sent; empty takes the profile's default model */
	FString Model;

	FString SystemPrompt;
	FString UserMessage;

	/** Output budget sent in the profile's budget field; 0 sends none (the server decides) */
	int32 MaxOutputTokens = 0;

	/** Sent as temperature when set */
	TOptional<float> Temperature;

	/**
	 * Thinking for this request: "none" switches it off (the profile's ThinkingOff fragment), a level (low, high, max)
	 * switches it on (ThinkingOn, and the level in the profile's ReasoningEffortField). Empty sends neither, so the
	 * host's default applies.
	 */
	FString ReasoningEffort;

	/** The answer's JSON schema; null, or a profile in prompt-only mode, sends no response_format */
	TSharedPtr<FJsonObject> ResponseSchema;

	/** False when the model takes no response_format at all (DeepSeek in thinking mode, an unregistered DeepSeek ID) */
	bool bAllowStructuredOutput = true;

	/** response_format.json_schema.name */
	FString SchemaName = TEXT("n2c_translation_schema");

	/** Named in a Truncated error: where the user raises the output budget */
	FString OutputBudgetHint;
};

/** What a wire dialect decoded from one response */
struct NODETOCODE_API FN2CLLMResult
{
	/** The answer text (the model's JSON); empty on an error */
	FString Content;

	/** The model's reasoning text, from the profile's reasoning fields (never the answer) */
	FString Reasoning;

	/** The host's finish_reason */
	FString FinishReason;

	FN2CTranslationUsage Usage;

	/** Set when there is no answer: the host's error, a cut-off, filtered or refused answer, or an unreadable body */
	FN2CTranslationError Error;

	bool IsError() const { return Error.IsError(); }
};
