// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Utils/Validators/N2CNodeValidator.h"
#include "Utils/N2CLogger.h"

/**
 * @class FN2CBlueprintValidator
 * @brief Validates N2C documents as diagnostics (card 0240, imp-translation-core-8)
 *
 * Every finding is an FN2CDiagnostic. Errors are structural, so the document cannot be used as it is: no readable
 * version, a missing name or class, no graph or no nodes, a node without an ID, a duplicate node or pin ID, a flow or
 * comment box naming a node or pin the graph does not have. Everything else is a warning, and the document is still
 * used: pin shapes, node flag combinations, unknown kinds, a connected input with no data flow, and a struct member
 * without its type name (the struct is kept). The bool overloads return false only on an error.
 */
class NODETOCODE_API FN2CBlueprintValidator
{
public:
    /** Validate a blueprint definition: false on an error, whose text goes to OutError */
    bool Validate(const FN2CBlueprint& Blueprint, FString& OutError);

    /** Every finding into OutDiagnostics (appended); true when none is an error */
    bool Validate(const FN2CBlueprint& Blueprint, TArray<FN2CDiagnostic>& OutDiagnostics);

    /** Validate a single graph */
    bool ValidateGraph(const FN2CGraph& Graph, FString& OutError);

    /** Validate flow references in a graph */
    bool ValidateFlowReferences(const FN2CGraph& Graph, FString& OutError);

    /** Validate a struct definition */
    bool ValidateStruct(const FN2CStruct& Struct, FString& OutError);

    /** Validate an enum definition */
    bool ValidateEnum(const FN2CEnum& Enum, FString& OutError);

private:
    bool ValidateRequired(const FN2CBlueprint& Blueprint, TArray<FN2CDiagnostic>& Out);
    bool ValidateGraph(const FN2CGraph& Graph, const FString& Path, TArray<FN2CDiagnostic>& Out);
    bool ValidateFlowReferences(const FN2CGraph& Graph, const FString& Path, TArray<FN2CDiagnostic>& Out);
    bool ValidateStruct(const FN2CStruct& Struct, const FString& Path, TArray<FN2CDiagnostic>& Out);
    bool ValidateEnum(const FN2CEnum& Enum, const FString& Path, TArray<FN2CDiagnostic>& Out);

    /** Node validator instance */
    FN2CNodeValidator NodeValidator;
};
