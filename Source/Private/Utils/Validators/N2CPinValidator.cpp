// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CPinValidator.h"

bool FN2CPinValidator::Validate(const FN2CPinDefinition& Pin, FString& OutError)
{
    // Log basic pin info
    FString PinInfo = FString::Printf(TEXT("Validating Pin: ID=%s, Name=%s, Type=%s, SubType=%s"),
        *Pin.ID,
        *Pin.Name,
        *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type)),
        *Pin.SubType);
    FN2CLogger::Get().Log(PinInfo, EN2CLogSeverity::Debug);

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
    FString FlagInfo = FString::Printf(TEXT("Pin %s flags: Connected=%d, IsRef=%d, IsConst=%d, IsArray=%d, IsMap=%d, IsSet=%d"),
        *Pin.ID,
        Pin.bConnected ? 1 : 0,
        Pin.bIsReference ? 1 : 0,
        Pin.bIsConst ? 1 : 0,
        Pin.bIsArray ? 1 : 0,
        Pin.bIsMap ? 1 : 0,
        Pin.bIsSet ? 1 : 0);
    FN2CLogger::Get().Log(FlagInfo, EN2CLogSeverity::Debug);

    FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s validation successful"), *Pin.ID), EN2CLogSeverity::Debug);
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
        FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s has empty name"), *Pin.ID), EN2CLogSeverity::Debug);
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

        default:
            // Log other types
            FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s has standard type %s"), 
                *Pin.ID, *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Pin.Type))), EN2CLogSeverity::Debug);
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

    // Const and reference validation - allow for certain types
    if (Pin.bIsConst && Pin.bIsReference)
    {
        // Log this case but don't fail validation
        FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s is both const and reference - this is valid for certain engine types"), *Pin.ID), EN2CLogSeverity::Debug);
    }

    return true;
}
