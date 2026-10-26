// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CPinValidator.h"

bool FN2CPinValidator::Validate(const FN2CPinDefinition& Pin, FString& OutError)
{
    // Log basic pin info
    N2C_LOG(Debug, TEXT("Validating Pin: ID=%s, Name=%s, Type=%s, SubType=%s"),
        *Pin.ID,
        *Pin.Name,
        *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)),
        *Pin.SubType);

    // Validate required fields
    if (!ValidateRequired(Pin, OutError))
    {
        return false;
    }

    // Validate type-specific requirements
    if (!ValidateTypeSpecific(Pin, OutError))
    {
        return false;
    }

    // Validate container flags
    if (!ValidateContainerFlags(Pin, OutError))
    {
        return false;
    }

    // Log pin flags
    N2C_LOG(Debug, TEXT("Pin %s flags: Connected=%d, IsRef=%d, IsConst=%d, IsArray=%d, IsMap=%d, IsSet=%d"),
        *Pin.ID,
        Pin.bConnected ? 1 : 0,
        Pin.bIsReference ? 1 : 0,
        Pin.bIsConst ? 1 : 0,
        Pin.bIsArray ? 1 : 0,
        Pin.bIsMap ? 1 : 0,
        Pin.bIsSet ? 1 : 0);

    N2C_LOG(Debug, TEXT("Pin %s validation successful"), *Pin.ID);
    return true;
}

bool FN2CPinValidator::ValidateRequired(const FN2CPinDefinition& Pin, FString& OutError)
{
    // Check required fields
    if (Pin.ID.IsEmpty())
    {
        OutError = FString::Printf(TEXT("Pin validation warning: Empty ID for pin %s"), *Pin.Name);
        FN2CLogger::Get().LogWarning(OutError);
        // Continue despite warning - generate a default ID if needed
    }

    // Allow empty names for all pins
    if (Pin.Name.IsEmpty())
    {
        N2C_LOG(Debug, TEXT("Pin %s has empty name"), *Pin.ID);
    }

    return true;
}

bool FN2CPinValidator::ValidateTypeSpecific(const FN2CPinDefinition& Pin, FString& OutError)
{
    // Validate type-specific requirements
    switch (Pin.Type)
    {
        case EN2CPinType::Array:
        case EN2CPinType::Set:
        case EN2CPinType::Map:
            // Container types require valid SubType
            if (Pin.SubType.IsEmpty())
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Container type %s missing SubType for pin %s"), 
                    *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            // Container flags must match type
            if (Pin.Type == EN2CPinType::Array && !Pin.bIsArray)
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Array type without array flag for pin %s"), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            if (Pin.Type == EN2CPinType::Map && !Pin.bIsMap)
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Map type without map flag for pin %s"), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            if (Pin.Type == EN2CPinType::Set && !Pin.bIsSet)
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Set type without set flag for pin %s"), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            break;

        case EN2CPinType::Struct:
        case EN2CPinType::Object:
        case EN2CPinType::Class:
        case EN2CPinType::Interface:
            // These types require valid SubType for type information
            if (Pin.SubType.IsEmpty())
            {
                OutError = FString::Printf(TEXT("Pin validation warning: %s type missing SubType for pin %s"), 
                    *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            break;

        case EN2CPinType::Exec:
            // Exec pins can't have default values or be const/reference
            if (!Pin.DefaultValue.IsEmpty() || Pin.bIsConst || Pin.bIsReference)
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Invalid Exec pin configuration for pin %s"), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            break;

        case EN2CPinType::Delegate:
        case EN2CPinType::MulticastDelegate:
            // Delegates can't be const
            if (Pin.bIsConst)
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Const delegate pin %s"), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            break;

        case EN2CPinType::SoftObject:
        case EN2CPinType::SoftClass:
            // Soft references require class path in SubType
            if (Pin.SubType.IsEmpty())
            {
                OutError = FString::Printf(TEXT("Pin validation warning: Soft reference missing class path for pin %s"), *Pin.ID);
                FN2CLogger::Get().LogWarning(OutError);
                // Continue despite warning
            }
            break;

        case EN2CPinType::Unknown:
            // A pin type this build does not know (a later 2.x document, card 0445): accepted with a warning
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Pin %s has a pin type this build does not know: '%s'"),
                *Pin.ID, *Pin.UnknownType));
            break;

        default:
            // Log other types
            N2C_LOG(Debug, TEXT("Pin %s has standard type %s"),
                *Pin.ID, *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)));
            break;
    }

    return true;
}

bool FN2CPinValidator::ValidateContainerFlags(const FN2CPinDefinition& Pin, FString& OutError)
{
    // Validate container flags - only one container type allowed
    if ((Pin.bIsArray && Pin.bIsMap) || (Pin.bIsArray && Pin.bIsSet) || (Pin.bIsMap && Pin.bIsSet))
    {
        OutError = FString::Printf(TEXT("Pin validation warning: Pin %s has multiple container types"), *Pin.ID);
        FN2CLogger::Get().LogWarning(OutError);
        // Continue despite warning - LLM will need to determine the most appropriate container type
    }

    // Only a map has a value type (card 0238): "type" is then its key's
    if (Pin.HasValueType() && !Pin.bIsMap)
    {
        OutError = FString::Printf(TEXT("Pin validation warning: Pin %s has a value_type but is not a map"), *Pin.ID);
        FN2CLogger::Get().LogWarning(OutError);
        // Continue despite warning
    }

    // Const and reference validation - allow for certain types
    if (Pin.bIsConst && Pin.bIsReference)
    {
        // Log this case but don't fail validation
        N2C_LOG(Debug, TEXT("Pin %s is both const and reference - this is valid for certain engine types"), *Pin.ID);
    }

    return true;
}
