// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"
#include "Utils/Validators/N2CPinValidator.h"
#include "Utils/N2CLogger.h"

/**
 * @class FN2CNodeValidator
 * @brief Validates node definitions to ensure they meet requirements
 */
class NODETOCODE_API FN2CNodeValidator
{
public:
    /** Validate a single node definition */
    bool Validate(const FN2CNodeDefinition& Node, FString& OutError);
    
private:
    /** Validate required fields */
    bool ValidateRequired(const FN2CNodeDefinition& Node, FString& OutError);
    
    /** Validate node flags */
    bool ValidateFlags(const FN2CNodeDefinition& Node, FString& OutError);
    
    /** Validate all pins on the node */
    bool ValidatePins(const FN2CNodeDefinition& Node, FString& OutError);
    
    /** Check if node has valid exec pins */
    bool HasExecPins(const FN2CNodeDefinition& Node) const;
    
    /** Pin validator instance */
    FN2CPinValidator PinValidator;
};
