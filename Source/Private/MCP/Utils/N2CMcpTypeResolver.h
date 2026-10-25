// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h" // For FEdGraphPinType

/**
 * Turns the type strings the MCP tools and the Python bridge receive into FEdGraphPinType, the way the editor's
 * variable type picker would. Every accepted identifier resolves to a pin type a Blueprint variable or parameter can
 * have; anything else is refused with an error code and a reason, before any caller touches a Blueprint.
 *
 * Accepted forms (case-insensitive where noted):
 * - Primitives: bool, byte/uint8, int/int32, int64, float, double, string/FString, text/FText, name/FName.
 * - The math structs by short name, with or without F: Vector (vector3), Vector2D, Vector4, Rotator, Transform,
 *   Quat, Color, LinearColor.
 * - softobject, softclass: a soft reference to UObject.
 * - A short type name, looked up in memory (FindFirstObject, native types first) and then in the asset registry
 *   (user structs, user enums and Blueprints): HitResult, Guid, ECollisionChannel, Actor, BP_Enemy, S_Loot. A C++
 *   prefix is accepted for the kind it names: FHitResult (struct), AActor or UObject (class), IMyInterface (interface
 *   class), EMyEnum (enum). A trailing '*' (AActor*) is an object reference.
 * - A path starting with '/': /Script/Engine.HitResult, /Game/Data/S_Loot.S_Loot, /MyPlugin/E_State, /Engine/...
 *   Unloaded assets are checked in the asset registry first; only classes (a Blueprint stands for its generated
 *   class), structs and enums are loaded.
 * - The generic categories object, class, struct, enum, interface, softobject and softclass with a SubTypeIdentifier
 *   (a short name or a path), as create-blueprint-function parameters send them.
 *
 * Classes, structs and enums must pass UEdGraphSchema_K2::IsAllowableBlueprintVariableType. Enums, native or user,
 * become byte pins with the enum as sub-type. wildcard, exec, delegate and multicastdelegate are refused, as are C++
 * template spellings (TArray<...>, TSubclassOf<...>): containers go through ContainerTypeStr. Set elements and map
 * keys must be hashable (FBlueprintEditorUtils::HasGetTypeHash).
 */
class NODETOCODE_API FN2CMcpTypeResolver
{
public:
    /**
     * Resolves a type identifier string (and associated sub-type, container info) into an FEdGraphPinType.
     *
     * @param TypeIdentifier The primary type string (e.g., "bool", "FVector", "HitResult", "/Script/Engine.Actor", "object").
     * @param SubTypeIdentifier The sub-type for the generic categories object, class, struct, enum, interface,
     *        softobject and softclass (e.g., "Actor", "/Game/MyStruct.MyStruct"); ignored otherwise.
     * @param ContainerTypeStr String representation of the container type ("none", "array", "set", "map").
     * @param KeyTypeIdentifierStr The key type for map containers (TypeIdentifier is then the value type).
     * @param bIsReference Whether the pin is a reference.
     * @param bIsConst Whether the pin is const.
     * @param OutPinType The FEdGraphPinType to populate.
     * @param OutErrorMsg Error message if resolution fails, starting with an error code (INVALID_TYPE, TYPE_NOT_FOUND, ...).
     * @return True if resolution was successful, false otherwise.
     */
    static bool ResolvePinType(
        const FString& TypeIdentifier,
        const FString& SubTypeIdentifier,
        const FString& ContainerTypeStr,
        const FString& KeyTypeIdentifierStr,
        bool bIsReference,
        bool bIsConst,
        FEdGraphPinType& OutPinType,
        FString& OutErrorMsg
    );

    /**
     * Checks that a pin type is one a Blueprint variable can have: object-like categories carry a UClass sub-type,
     * struct pins a UScriptStruct, byte pins no sub-type or a UEnum; no enum, wildcard, exec or delegate category; set
     * elements and map keys hashable. ResolvePinType runs it on every result, so a pin type it returns never reaches
     * the engine's checks in FBlueprintEditorUtils::AddMemberVariable with a mismatched sub-type.
     */
    static bool ValidateVariablePinType(const FEdGraphPinType& PinType, FString& OutErrorMsg);
};
