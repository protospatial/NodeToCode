// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CPin.h"

/**
 * @class FN2CPinTypeCompatibility
 * @brief Provides centralized pin type compatibility checking
 */
class NODETOCODE_API FN2CPinTypeCompatibility
{
public:
    /** Check if two pin types are compatible */
    static bool AreTypesCompatible(EN2CPinType Type1, EN2CPinType Type2);
    
    /** Check if two pins are compatible */
    static bool ArePinsCompatible(const FN2CPinDefinition& Pin1, const FN2CPinDefinition& Pin2);
    
private:
    /** Check if container types are compatible */
    static bool AreContainerTypesCompatible(const FN2CPinDefinition& Pin1, const FN2CPinDefinition& Pin2);
    
    /** Check if object types are compatible */
    static bool AreObjectTypesCompatible(const FN2CPinDefinition& Pin1, const FN2CPinDefinition& Pin2);
};
