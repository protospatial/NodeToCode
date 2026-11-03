// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"

/**
 * One finding of a code verifier (card 0312): where in the generated code, how bad, and what.
 */
struct NODETOCODE_API FN2CVerifyDiagnostic
{
	/** The translated graph (FN2CGraphTranslation::GraphName) */
	FString Graph;
	/** Which code of the graph: "declaration" or "implementation"; empty when the finding is about the graph as a whole */
	FString File;
	/** 1-based line and column in that code; 0 when the finding has no position */
	int32 Line = 0;
	int32 Column = 0;
	EN2CDiagnosticSeverity Severity = EN2CDiagnosticSeverity::Error;
	/** N2CVerify code: SYNTAX_ERROR, NODE_NOT_COVERED, VERIFIER_UNAVAILABLE */
	FString Code;
	/** A sentence for people (and for the model that repairs the code) */
	FString Message;
};

/** What one verifier found in one translation */
struct NODETOCODE_API FN2CVerifyReport
{
	/** The verifier's name (IN2CCodeVerifier::GetName) */
	FString Verifier;
	/** False when the verifier could not run here (the language is not its own, or Python is not available) */
	bool bRan = false;
	TArray<FN2CVerifyDiagnostic> Diagnostics;

	bool HasErrors() const
	{
		return Diagnostics.ContainsByPredicate([](const FN2CVerifyDiagnostic& Diagnostic)
		{
			return Diagnostic.Severity == EN2CDiagnosticSeverity::Error;
		});
	}
};

/** Diagnostic codes */
namespace N2CVerify
{
	/** The code does not parse: the language's own parser gave the line, column and message */
	inline constexpr const TCHAR* SyntaxError = TEXT("SYNTAX_ERROR");
	/** A function call, variable or event of the captured graph does not appear in the code generated for it */
	inline constexpr const TCHAR* NodeNotCovered = TEXT("NODE_NOT_COVERED");
	/** The verifier could not run (for example Python is not available in this editor) */
	inline constexpr const TCHAR* VerifierUnavailable = TEXT("VERIFIER_UNAVAILABLE");
}

/**
 * The verifier seam (card 0312, arch-arch-core-llm-ui-13): checks a translation's generated code against the language
 * and against the capture it was generated from, and reports findings with positions. Two adapters:
 * FN2CPythonSyntaxVerifier (Python's own parser, in process) and FN2CCaptureCoverageVerifier (language-agnostic: every
 * call, variable and event the graph runs appears in its code), plus FN2CCppScratchModuleVerifier (card 0315).
 * Verify and Refine (FN2CVerifyRefineLoop, card 0313) runs them on each answer of a translation session or batch item
 * and feeds the diagnostics back to the model.
 *
 * Verify runs on the game thread (the Python adapter executes Python).
 */
class NODETOCODE_API IN2CCodeVerifier
{
public:
	virtual ~IN2CCodeVerifier() = default;

	/** A short stable name, e.g. "python-syntax" */
	virtual FString GetName() const = 0;

	/** Whether the verifier checks code of this target language */
	virtual bool SupportsLanguage(EN2CCodeLanguage Language) const = 0;

	/**
	 * Checks each graph of Translation. Capture is the N2C model the translation was generated from (the graphs are
	 * matched by name); a report for a language the verifier does not support has bRan false and no diagnostic.
	 */
	virtual FN2CVerifyReport Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
		EN2CCodeLanguage Language) const = 0;
};
