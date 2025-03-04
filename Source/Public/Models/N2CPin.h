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
    bool IsCompatibleWith(const EN2CPinType& OtherType) const;

    /** Checks if this pin is compatible with another pin for connections */
    bool IsCompatibleWith(const FN2CPinDefinition& OtherPin) const;

    /** Validates the pin definition and its enum */
    bool IsValid() const;

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
