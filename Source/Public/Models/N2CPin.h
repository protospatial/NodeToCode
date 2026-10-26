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
    Wildcard    UMETA(DisplayName = "Wildcard"),
    /**
     * A pin "type" this build does not know (a later 2.x document), read by FN2CSerializer::FromJson and kept with its
     * text in FN2CPinDefinition::UnknownType so ToJson writes it back unchanged (card 0445). Never captured.
     */
    Unknown     UMETA(DisplayName = "Unknown")
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

    /**
     * The pin's name, UEdGraphPin::PinName ("execute", "then", "self", "ReturnValue"): a stable identifier, the same in
     * every editor language and with friendly names on or off (card 0138). A pin read from a 1.x document keeps the
     * display name that document holds.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /**
     * Hint only (N2C JSON "label"): the pin's display label in source (English) text, empty when it says no more than
     * Name (N2CJsonContract::IsRedundantHint)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Label;

    /** Type (e.g. Exec, Float, etc.); a map pin's key type, with ValueType its value's */
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

    /**
     * A map pin's value type (N2C JSON "value_type", card 0238): for a map, Type and SubType describe the key and these
     * the value (FEdGraphPinType::PinValueType). Unknown with an empty UnknownValueType means none, as on every pin that
     * is not a map and on a map read from a document written before card 0238; "value_type" is then left out.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CPinType ValueType = EN2CPinType::Unknown;

    /** A map pin's value struct, enum, class or object type (N2C JSON "value_sub_type"), like SubType for the key */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ValueSubType;

    /** The "value_type" text as read when it names a type this build does not know (card 0445's policy) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownValueType;

    /** True when the pin has a value type to write: a map pin captured since card 0238, or read with one */
    bool HasValueType() const { return ValueType != EN2CPinType::Unknown || !UnknownValueType.IsEmpty(); }

    /** The "type" text as read when Type is Unknown (card 0445); ToJson writes it in place of "Unknown" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownType;

    /**
     * Fields of the pin's JSON object this build does not know, by name, each value as condensed JSON text, in the
     * order read. ToJson writes them back after the known fields (card 0445).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> UnknownFields;

    FN2CPinDefinition()
        : ID(TEXT(""))
        , Name(TEXT(""))
        , Label(TEXT(""))
        , Type(EN2CPinType::Exec)
        , SubType(TEXT(""))
        , DefaultValue(TEXT(""))
    {
    }
};
