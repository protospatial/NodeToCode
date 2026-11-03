// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "LLM/Verify/IN2CCodeVerifier.h"
#include "LLM/Verify/N2CCppCompileDriver.h"

/**
 * Compiles a C++ translation (card 0315): FN2CCppPlacement assembles the graphs into one UCLASS header and .cpp (as
 * place-cpp-translation does), and FN2CCppCompileDriver compiles them in a scratch module (N2CEvalScratch, with the
 * modules the placement says the code needs) of a scratch project, never in the user's modules. Each compiler error or
 * warning becomes a diagnostic: File "declaration" (the header) or "implementation" (the .cpp), with the line and column
 * in the assembled file, Code COMPILE_ERROR or COMPILE_WARNING, and the compiler's code and message. When the compile is
 * switched off (n2c.Verify.CppCompile) or cannot run, bRan is false and one VERIFIER_UNAVAILABLE warning says why.
 *
 * Blocks while UnrealBuildTool runs (seconds once the scratch project is built, about a minute and a half the first
 * time). Game thread (the placement looks types up).
 */
class NODETOCODE_API FN2CCppScratchModuleVerifier : public IN2CCodeVerifier
{
public:
	virtual FString GetName() const override { return TEXT("cpp-scratch-module"); }
	virtual bool SupportsLanguage(EN2CCodeLanguage Language) const override { return Language == EN2CCodeLanguage::Cpp; }
	virtual FN2CVerifyReport Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
		EN2CCodeLanguage Language) const override;

	/** The scratch project folder (empty: the driver's default); specs point it at their own folder */
	FString ScratchRoot;
	/** The last compile, for callers that want the compiler output */
	mutable FN2CCppCompileResult LastResult;
};

/** Diagnostic codes of the C++ compile verifier */
namespace N2CVerify
{
	inline constexpr const TCHAR* CompileError = TEXT("COMPILE_ERROR");
	inline constexpr const TCHAR* CompileWarning = TEXT("COMPILE_WARNING");
}
