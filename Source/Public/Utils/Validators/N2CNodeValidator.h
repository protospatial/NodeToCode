// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"
#include "Utils/Validators/N2CPinValidator.h"
#include "Utils/N2CLogger.h"

/**
 * @class FN2CNodeValidator
 * @brief Validates node definitions. A missing node ID or a duplicate pin ID is an error; flag combinations and pin
 *        findings are warnings (card 0240).
 */
class NODETOCODE_API FN2CNodeValidator
{
public:
    /** Validate a single node definition: false on an error, whose text goes to OutError */
    bool Validate(const FN2CNodeDefinition& Node, FString& OutError);

    /** Adds the node's and its pins' findings to OutDiagnostics; Path is the node's place, e.g. "graphs[0].nodes[2]". False on an error. */
    bool Validate(const FN2CNodeDefinition& Node, const FString& Path, TArray<FN2CDiagnostic>& OutDiagnostics);

private:
    /** Check if node has exec pins */
    bool HasExecPins(const FN2CNodeDefinition& Node) const;

    /** Pin validator instance */
    FN2CPinValidator PinValidator;
};
