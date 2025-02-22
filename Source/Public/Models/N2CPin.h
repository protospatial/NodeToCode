// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

/**
 * @file N2CPin.h
 * @brief Pin-related data type definitions for the Node to Code plugin
 */

#pragma once

#include "CoreMinimal.h"
#include "Utils/N2CLogger.h"
#include "N2CPin.generated.h"

/**
 * @enum EN2CPinType
 * @brief Defines the type of Blueprint pin
 */
UENUM(BlueprintType)
enum class EN2CPinType : uint8
{
    Exec        UMETA(DisplayName = "Exec"),
    Boolean     UMETA(DisplayName = "Boolean"),
    Byte        UMETA(DisplayName = "Byte"),
    Integer     UMETA(DisplayName = "Integer"),
    Integer64   UMETA(DisplayName = "Integer64"),
    Float       UMETA(DisplayName = "Float"),
    Double      UMETA(DisplayName = "Double"),
    Real        UMETA(DisplayName = "Real"),
    String      UMETA(DisplayName = "String"),
    Name        UMETA(DisplayName = "Name"),
    Text        UMETA(DisplayName = "Text"),
    Vector      UMETA(DisplayName = "Vector"),
    Vector2D    UMETA(DisplayName = "Vector2D"),
    Vector4D    UMETA(DisplayName = "Vector4D"),
    Rotator     UMETA(DisplayName = "Rotator"),
    Transform   UMETA(DisplayName = "Transform"),
    Quat        UMETA(DisplayName = "Quat"),
    Object      UMETA(DisplayName = "Object"),
    Class       UMETA(DisplayName = "Class"),
    Interface   UMETA(DisplayName = "Interface"),
    Struct      UMETA(DisplayName = "Struct"),
    Enum        UMETA(DisplayName = "Enum"),
    Delegate    UMETA(DisplayName = "Delegate"),
    MulticastDelegate UMETA(DisplayName = "MulticastDelegate"),
    Array       UMETA(DisplayName = "Array"),
    Set         UMETA(DisplayName = "Set"),
    Map         UMETA(DisplayName = "Map"),
    SoftObject  UMETA(DisplayName = "SoftObject"),
    SoftClass   UMETA(DisplayName = "SoftClass"),
    AssetId     UMETA(DisplayName = "AssetId"),
    Material    UMETA(DisplayName = "Material"),
    Texture     UMETA(DisplayName = "Texture"),
    StaticMesh  UMETA(DisplayName = "StaticMesh"),
    SkeletalMesh UMETA(DisplayName = "SkeletalMesh"),
    Pose        UMETA(DisplayName = "Pose"),
    Animation   UMETA(DisplayName = "Animation"),
    BlendSpace  UMETA(DisplayName = "BlendSpace"),
    FieldPath   UMETA(DisplayName = "FieldPath"),
    Bitmask     UMETA(DisplayName = "Bitmask"),
    Self        UMETA(DisplayName = "Self"),
    Index       UMETA(DisplayName = "Index"),
    Wildcard    UMETA(DisplayName = "Wildcard")
};

/**
 * @struct FN2CPinDefinition
 * @brief Defines a single pin within a node
 */
USTRUCT(BlueprintType)
struct FN2CPinDefinition
{
    GENERATED_BODY()

    /** Short local ID, e.g. "P1" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ID;

    /** The display name of the pin, e.g. "Exec", "Target", "DeltaTime" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Type (e.g. Exec, Float, etc.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CPinType Type;

    /** Checks if this pin type is compatible with another pin type for connections */
    bool IsCompatibleWith(const EN2CPinType& OtherType) const
    {
        // Special handling for wildcards
        if (Type == EN2CPinType::Wildcard || OtherType == EN2CPinType::Wildcard)
        {
            return true;
        }

        // Handle soft references to regular references
        if ((Type == EN2CPinType::SoftObject && OtherType == EN2CPinType::Object) ||
            (Type == EN2CPinType::SoftClass && OtherType == EN2CPinType::Class))
        {
            return true;
        }

        // Handle numeric type compatibility
        if ((Type == EN2CPinType::Integer && OtherType == EN2CPinType::Float) ||
            (Type == EN2CPinType::Float && OtherType == EN2CPinType::Integer))
        {
            return true;
        }

        // Handle vector type compatibility
        if ((Type == EN2CPinType::Vector && OtherType == EN2CPinType::Vector4D) ||
            (Type == EN2CPinType::Vector2D && OtherType == EN2CPinType::Vector))
        {
            return true;
        }

        // Regular type comparison
        return Type == OtherType;
    }

    /** Checks if this pin is compatible with another pin for connections */
    bool IsCompatibleWith(const FN2CPinDefinition& OtherPin) const
    {
        // First check basic type compatibility
        if (!IsCompatibleWith(OtherPin.Type))
        {
            return false;
        }

        // For container types, check subtypes match
        if ((Type == EN2CPinType::Array || Type == EN2CPinType::Set || Type == EN2CPinType::Map) &&
            SubType != OtherPin.SubType)
        {
            return false;
        }

        // For object/class/interface/struct types, check subtypes match
        if ((Type == EN2CPinType::Object || Type == EN2CPinType::Class || 
             Type == EN2CPinType::Interface || Type == EN2CPinType::Struct) &&
            SubType != OtherPin.SubType)
        {
            return false;
        }

        return true;
    }

    /** Validates the pin definition and its enum */
    bool IsValid() const
    {
        // Log basic pin info
        FString PinInfo = FString::Printf(TEXT("Validating Pin: ID=%s, Name=%s, Type=%s, SubType=%s"),
            *ID,
            *Name,
            *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Type)),
            *SubType);
        FN2CLogger::Get().Log(PinInfo, EN2CLogSeverity::Debug);

        // Check required fields
        if (ID.IsEmpty())
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Empty ID for pin %s"), *Name));
            return false;
        }

        // Allow empty names for all pins
        if (Name.IsEmpty())
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s has empty name"), *ID), EN2CLogSeverity::Debug);
        }

        // Log pin flags
        FString FlagInfo = FString::Printf(TEXT("Pin %s flags: Connected=%d, IsRef=%d, IsConst=%d, IsArray=%d, IsMap=%d, IsSet=%d"),
            *ID,
            bConnected ? 1 : 0,
            bIsReference ? 1 : 0,
            bIsConst ? 1 : 0,
            bIsArray ? 1 : 0,
            bIsMap ? 1 : 0,
            bIsSet ? 1 : 0);
        FN2CLogger::Get().Log(FlagInfo, EN2CLogSeverity::Debug);

        // Validate type-specific requirements
        switch (Type)
        {
            case EN2CPinType::Array:
            case EN2CPinType::Set:
            case EN2CPinType::Map:
                // Container types require valid SubType
                if (SubType.IsEmpty())
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Container type %s missing SubType for pin %s"), 
                        *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Type)), *ID));
                    return false;
                }
                // Container flags must match type
                if (Type == EN2CPinType::Array && !bIsArray)
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Array type without array flag for pin %s"), *ID));
                    return false;
                }
                if (Type == EN2CPinType::Map && !bIsMap)
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Map type without map flag for pin %s"), *ID));
                    return false;
                }
                if (Type == EN2CPinType::Set && !bIsSet)
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Set type without set flag for pin %s"), *ID));
                    return false;
                }
                break;

            case EN2CPinType::Struct:
            case EN2CPinType::Object:
            case EN2CPinType::Class:
            case EN2CPinType::Interface:
                // These types require valid SubType for type information
                if (SubType.IsEmpty())
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: %s type missing SubType for pin %s"), 
                        *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Type)), *ID));
                    return false;
                }
                break;

            case EN2CPinType::Exec:
                // Exec pins can't have default values or be const/reference
                if (!DefaultValue.IsEmpty() || bIsConst || bIsReference)
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Invalid Exec pin configuration for pin %s"), *ID));
                    return false;
                }
                break;

            case EN2CPinType::Delegate:
            case EN2CPinType::MulticastDelegate:
                // Delegates can't be const
                if (bIsConst)
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Const delegate pin %s"), *ID));
                    return false;
                }
                break;

            case EN2CPinType::SoftObject:
            case EN2CPinType::SoftClass:
                // Soft references require class path in SubType
                if (SubType.IsEmpty())
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Soft reference missing class path for pin %s"), *ID));
                    return false;
                }
                break;

            default:
                // Log other types
                FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s has standard type %s"), 
                    *ID, *StaticEnum<EN2CPinType>()->GetNameStringByValue(static_cast<int64>(Type))), EN2CLogSeverity::Debug);
                break;
        }

        // Validate container flags - only one container type allowed
        if ((bIsArray && bIsMap) || (bIsArray && bIsSet) || (bIsMap && bIsSet))
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Pin validation failed: Pin %s cannot have multiple container types"), *ID));
            return false;
        }

        // Const and reference validation - allow for certain types
        if (bIsConst && bIsReference)
        {
            // Log this case but don't fail validation
            FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s is both const and reference - this is valid for certain engine types"), *ID), EN2CLogSeverity::Debug);
        }


        FN2CLogger::Get().Log(FString::Printf(TEXT("Pin %s validation successful"), *ID), EN2CLogSeverity::Debug);
        return true;
    }

    /** Optional subtype or subcategory from UE (like a struct name) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SubType;

    /** Default value (if any). Could hold numeric, string, or JSON-like data. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;

    /** Whether this pin is currently connected */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bConnected = false;

    /** Some pin-level metadata (ref, const, array, etc.) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsReference = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsConst = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsArray = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsMap = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsSet = false;

    FN2CPinDefinition()
        : ID(TEXT(""))
        , Name(TEXT(""))
        , Type(EN2CPinType::Exec)
        , SubType(TEXT(""))
        , DefaultValue(TEXT(""))
    {
    }
};
