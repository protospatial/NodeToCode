// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

/** How FN2CSerializer::ToJson lays out its text */
enum class EN2CJsonStyle : uint8
{
    /** Indented, one value per line (Copy JSON, the saved N2C_BP_*.json) */
    Pretty,
    /** No whitespace (what is sent to an LLM, token estimates, MCP results) */
    Condensed
};

/**
 * @class FN2CSerializer
 * @brief Handles serialization of N2CStruct data to JSON format
 *
 * Provides functionality to convert FN2CBlueprint instances and their
 * contained structures into properly formatted JSON output.
 */
class FN2CSerializer
{
public:
    /** Convert an FN2CBlueprint to JSON string. Stateless: the style is per call. */
    static FString ToJson(const FN2CBlueprint& Blueprint, EN2CJsonStyle Style);

    /** Convert JSON string back to FN2CBlueprint */
    static bool FromJson(const FString& JsonString, FN2CBlueprint& OutBlueprint);

    /** Convert a single node to JSON object - useful for tools */
    static TSharedPtr<FJsonObject> NodeToJsonObject(const FN2CNodeDefinition& Node);

private:
    /** Internal JSON conversion helpers */
    static TSharedPtr<FJsonObject> BlueprintToJsonObject(const FN2CBlueprint& Blueprint);
    static TSharedPtr<FJsonObject> GraphToJsonObject(const FN2CGraph& Graph);
    static TSharedPtr<FJsonObject> PinToJsonObject(const FN2CPinDefinition& Pin);
    static TSharedPtr<FJsonObject> FlowsToJsonObject(const FN2CFlows& Flows);
    static TSharedPtr<FJsonObject> StructToJsonObject(const FN2CStruct& Struct);
    static TSharedPtr<FJsonObject> EnumToJsonObject(const FN2CEnum& Enum);

    /** JSON parsing helpers */
    static bool ParseBlueprintFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CBlueprint& OutBlueprint);
    static bool ParseGraphFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CGraph& OutGraph);
    static bool ParseNodeFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CNodeDefinition& OutNode);
    static bool ParsePinFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CPinDefinition& OutPin);
    static bool ParseFlowsFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CFlows& OutFlows);
    static bool ParseStructFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CStruct& OutStruct);
    static bool ParseEnumFromJson(const TSharedPtr<FJsonObject>& JsonObject, FN2CEnum& OutEnum);
};
