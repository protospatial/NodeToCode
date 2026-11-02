// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"

/** What a connection test found */
struct NODETOCODE_API FN2CConnectionTestResult
{
	bool bOk = false;

	/** One line for people: "Connected to Anthropic (claude-...)" or the provider's error */
	FString Message;
};

/**
 * The Get Started card's Test Connection button (card 0269): sends one tiny request ("Reply with OK") to the provider
 * and model in the settings, through a fresh service made the way the LLM module makes its own (same credential
 * source, endpoint and headers), and reports whether the provider answered. No translation, no saved file.
 */
class NODETOCODE_API FN2CConnectionTest
{
public:
	DECLARE_DELEGATE_OneParam(FOnDone, const FN2CConnectionTestResult& /*Result*/);

	/** Starts the test for the provider in the settings; OnDone runs on the game thread once the provider answers */
	static void Run(const FOnDone& OnDone);

	/**
	 * Reads the provider's raw response: a top-level "error" (the provider's own, or NodeToCode's error JSON for a
	 * missing key, timeout or HTTP failure) is a failure with its display text, any other JSON object is a success.
	 */
	static FN2CConnectionTestResult InterpretResponse(const FString& Response, EN2CLLMProvider Provider, const FString& Model);
};
