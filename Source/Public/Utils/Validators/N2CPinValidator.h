// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CPin.h"
#include "Utils/N2CLogger.h"

namespace N2CDiagnostics
{
	/** Adds one finding to Out and logs it (an error as an error, a warning as a warning), as the validators did before card 0240 */
	NODETOCODE_API void Add(TArray<FN2CDiagnostic>& Out, EN2CDiagnosticSeverity Severity, const TCHAR* Code, const FString& Path,
		const FString& Message);

	/** True when any of Diagnostics is an error */
	NODETOCODE_API bool HasError(const TArray<FN2CDiagnostic>& Diagnostics);

	/** The first error's message, or empty */
	NODETOCODE_API FString FirstError(const TArray<FN2CDiagnostic>& Diagnostics);
}

/**
 * @class FN2CPinValidator
 * @brief Validates pin definitions. Every pin finding is a warning (card 0240): a pin never fails a document.
 */
class NODETOCODE_API FN2CPinValidator
{
public:
    /** Validate a single pin definition; OutError gets the last warning's text. Always true. */
    bool Validate(const FN2CPinDefinition& Pin, FString& OutError);

    /** Adds the pin's findings (all warnings) to OutDiagnostics; Path is the pin's place, e.g. "graphs[0].nodes[2].input_pins[1]" */
    bool Validate(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& OutDiagnostics);

private:
    void ValidateRequired(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& Out);
    void ValidateTypeSpecific(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& Out);
    void ValidateContainerFlags(const FN2CPinDefinition& Pin, const FString& Path, TArray<FN2CDiagnostic>& Out);
};
