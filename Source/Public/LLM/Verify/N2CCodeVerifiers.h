// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/Verify/IN2CCodeVerifier.h"

/**
 * Parses each graph's Python code with the editor's own Python (compile(..., "exec")) and reports every syntax error
 * with its line and column (card 0312). Runs in process through PythonScriptPlugin, on the game thread; when Python is
 * not available the report says so (VERIFIER_UNAVAILABLE, a warning) and bRan is false.
 */
class NODETOCODE_API FN2CPythonSyntaxVerifier : public IN2CCodeVerifier
{
public:
	virtual FString GetName() const override { return TEXT("python-syntax"); }
	virtual bool SupportsLanguage(EN2CCodeLanguage Language) const override { return Language == EN2CCodeLanguage::Python; }
	virtual FN2CVerifyReport Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
		EN2CCodeLanguage Language) const override;
};

/**
 * Language-agnostic coverage (card 0312): every function call, variable read or write and event the captured graph
 * runs must appear in the code generated for it as a whole identifier, compared without case or underscores
 * (PrintString matches print_string; MaxHealth does not cover Health), with the engine's "K2_", an override event's
 * "Receive" and a Boolean's "b" prefix optional. Pseudocode is prose, so there the name may also be written in words.
 * A miss is a NODE_NOT_COVERED warning naming the node. Nodes Blueprint never runs (unused, unreachable, disabled) are
 * not expected; neither are graphs the translation has no code for.
 */
class NODETOCODE_API FN2CCaptureCoverageVerifier : public IN2CCodeVerifier
{
public:
	virtual FString GetName() const override { return TEXT("capture-coverage"); }
	virtual bool SupportsLanguage(EN2CCodeLanguage Language) const override { return true; }
	virtual FN2CVerifyReport Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
		EN2CCodeLanguage Language) const override;

	/** The names a node of this member name may appear under in code, normalized */
	static TArray<FString> CandidateNames(const FString& MemberName);

	/** Lowercase letters and digits only: how names and code are compared */
	static FString Normalize(const FString& Text);

	/** Every identifier (run of letters, digits and underscores) of Code, normalized */
	static TSet<FString> Identifiers(const FString& Code);
};
