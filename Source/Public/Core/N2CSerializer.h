// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CCapture.h"
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

/** Whether ToJson(const FN2CCapture&, ...) adds the capture's ID-to-GUID table */
enum class EN2CJsonGuids : uint8
{
    /** The N2C JSON only (LLM prompts never get GUIDs) */
    None,
    /**
     * Adds a top-level "guids" object for agents: "graphs" (each graph's GraphGuid, in "graphs" order), "nodes" (node
     * ID -> NodeGuid) and "pins" (pin ID -> {"node": node ID, "guid": PinId}), one entry per captured node and pin
     */
    Table
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

    /** A capture's Blueprint as JSON, with its ID-to-GUID table when Guids is Table */
    static FString ToJson(const FN2CCapture& Capture, EN2CJsonStyle Style, EN2CJsonGuids Guids);

    /** The "guids" table of a capture (see EN2CJsonGuids::Table) */
    static TSharedPtr<FJsonObject> GuidTableToJsonObject(const FN2CCapture& Capture);

    /** Convert JSON string back to FN2CBlueprint */
    static bool FromJson(const FString& JsonString, FN2CBlueprint& OutBlueprint);

    /** Convert a single node to JSON object - useful for tools */
    static TSharedPtr<FJsonObject> NodeToJsonObject(const FN2CNodeDefinition& Node);

private:
    /** Writes a JSON object in the given style; empty on failure */
    static FString WriteJson(const TSharedPtr<FJsonObject>& JsonObject, EN2CJsonStyle Style);

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
