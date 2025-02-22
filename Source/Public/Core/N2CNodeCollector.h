// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

/**
 * @class FN2CNodeCollector
 * @brief Collects Blueprint nodes directly from the Blueprint Editor
 *
 * Interfaces with the active Blueprint Editor instance to gather live nodes,
 * maintaining direct access to node properties and relationships.
 */
class FN2CNodeCollector
{
public:
    /** Get the singleton instance */
    static FN2CNodeCollector& Get();

    /**
     * @brief Collects nodes from a specific graph
     * @param Graph The graph to collect from
     * @param[out] OutNodes Array to store collected nodes
     * @return True if collection succeeded
     */
    bool CollectNodesFromGraph(UEdGraph* Graph, TArray<UK2Node*>& OutNodes);

    /**
     * @brief Collects detailed pin information from a node
     * @param Node The node to collect pins from
     * @param[out] OutInputPins Array to store input pins
     * @param[out] OutOutputPins Array to store output pins
     * @return True if pin collection succeeded
     */
    bool CollectPinsFromNode(UK2Node* Node, TArray<UEdGraphPin*>& OutInputPins, TArray<UEdGraphPin*>& OutOutputPins);

private:
    /** Constructor */
    FN2CNodeCollector() = default;

    /** Validates a pin is properly configured */
    bool ValidatePin(UEdGraphPin* Pin) const;

    /** Gets additional pin metadata for K2 nodes */
    void GetK2PinMetadata(UK2Node* K2Node, UEdGraphPin* Pin);

    /** Collects default value information from a pin */
    void CollectPinDefaultValue(UEdGraphPin* Pin);

    /** Collects connection information from a pin */
    void CollectPinConnections(UEdGraphPin* Pin);

};
