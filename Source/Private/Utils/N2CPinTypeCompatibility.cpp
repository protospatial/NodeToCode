// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CPinTypeCompatibility.h"

bool FN2CPinTypeCompatibility::AreTypesCompatible(EN2CPinType Type1, EN2CPinType Type2)
{
    // Special handling for wildcards
    if (Type1 == EN2CPinType::Wildcard || Type2 == EN2CPinType::Wildcard)
    {
        return true;
    }

    // Handle soft references to regular references
    if ((Type1 == EN2CPinType::SoftObject && Type2 == EN2CPinType::Object) ||
        (Type1 == EN2CPinType::SoftClass && Type2 == EN2CPinType::Class) ||
        (Type2 == EN2CPinType::SoftObject && Type1 == EN2CPinType::Object) ||
        (Type2 == EN2CPinType::SoftClass && Type1 == EN2CPinType::Class))
    {
        return true;
    }

    // Handle numeric type compatibility
    if ((Type1 == EN2CPinType::Integer && Type2 == EN2CPinType::Float) ||
        (Type1 == EN2CPinType::Float && Type2 == EN2CPinType::Integer) ||
        (Type1 == EN2CPinType::Integer && Type2 == EN2CPinType::Integer64) ||
        (Type1 == EN2CPinType::Integer64 && Type2 == EN2CPinType::Integer) ||
        (Type1 == EN2CPinType::Float && Type2 == EN2CPinType::Double) ||
        (Type1 == EN2CPinType::Double && Type2 == EN2CPinType::Float) ||
        (Type1 == EN2CPinType::Real && (Type2 == EN2CPinType::Float || Type2 == EN2CPinType::Double)) ||
        (Type2 == EN2CPinType::Real && (Type1 == EN2CPinType::Float || Type1 == EN2CPinType::Double)))
    {
        return true;
    }

    // Handle vector type compatibility
    if ((Type1 == EN2CPinType::Vector && Type2 == EN2CPinType::Vector4D) ||
        (Type1 == EN2CPinType::Vector2D && Type2 == EN2CPinType::Vector) ||
        (Type2 == EN2CPinType::Vector && Type1 == EN2CPinType::Vector4D) ||
        (Type2 == EN2CPinType::Vector2D && Type1 == EN2CPinType::Vector))
    {
        return true;
    }

    // Regular type comparison
    return Type1 == Type2;
}

bool FN2CPinTypeCompatibility::ArePinsCompatible(const FN2CPinDefinition& Pin1, const FN2CPinDefinition& Pin2)
{
    // First check basic type compatibility
    if (!AreTypesCompatible(Pin1.Type, Pin2.Type))
    {
        return false;
    }

    // For container types, check subtypes match
    if ((Pin1.Type == EN2CPinType::Array || Pin1.Type == EN2CPinType::Set || Pin1.Type == EN2CPinType::Map) ||
        (Pin2.Type == EN2CPinType::Array || Pin2.Type == EN2CPinType::Set || Pin2.Type == EN2CPinType::Map))
    {
        return AreContainerTypesCompatible(Pin1, Pin2);
    }

    // For object/class/interface/struct types, check subtypes match
    if ((Pin1.Type == EN2CPinType::Object || Pin1.Type == EN2CPinType::Class || 
         Pin1.Type == EN2CPinType::Interface || Pin1.Type == EN2CPinType::Struct) ||
        (Pin2.Type == EN2CPinType::Object || Pin2.Type == EN2CPinType::Class || 
         Pin2.Type == EN2CPinType::Interface || Pin2.Type == EN2CPinType::Struct))
    {
        return AreObjectTypesCompatible(Pin1, Pin2);
    }

    return true;
}

bool FN2CPinTypeCompatibility::AreContainerTypesCompatible(const FN2CPinDefinition& Pin1, const FN2CPinDefinition& Pin2)
{
    // Container flags must match
    if (Pin1.bIsArray != Pin2.bIsArray || Pin1.bIsMap != Pin2.bIsMap || Pin1.bIsSet != Pin2.bIsSet)
    {
        return false;
    }

    // SubTypes must match for containers
    if (Pin1.SubType != Pin2.SubType)
    {
        // Special case: empty SubType is compatible with any SubType
        if (Pin1.SubType.IsEmpty() || Pin2.SubType.IsEmpty())
        {
            return true;
        }
        return false;
    }

    return true;
}

bool FN2CPinTypeCompatibility::AreObjectTypesCompatible(const FN2CPinDefinition& Pin1, const FN2CPinDefinition& Pin2)
{
    // SubTypes must match for object types
    if (Pin1.SubType != Pin2.SubType)
    {
        // Special case: empty SubType is compatible with any SubType
        if (Pin1.SubType.IsEmpty() || Pin2.SubType.IsEmpty())
        {
            return true;
        }
        
        // TODO: In a more complete implementation, we would check class inheritance
        // to see if one type is derived from the other
        return false;
    }

    return true;
}
