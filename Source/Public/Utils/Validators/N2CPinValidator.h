// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CPin.h"
#include "Utils/N2CLogger.h"

/**
 * @class FN2CPinValidator
 * @brief Validates pin definitions to ensure they meet requirements
 */
class NODETOCODE_API FN2CPinValidator
{
public:
    /** Validate a single pin definition */
    bool Validate(const FN2CPinDefinition& Pin, FString& OutError);
    
private:
    /** Validate required fields */
    bool ValidateRequired(const FN2CPinDefinition& Pin, FString& OutError);
    
    /** Validate type-specific requirements */
    bool ValidateTypeSpecific(const FN2CPinDefinition& Pin, FString& OutError);
    
    /** Validate container flags */
    bool ValidateContainerFlags(const FN2CPinDefinition& Pin, FString& OutError);
};
