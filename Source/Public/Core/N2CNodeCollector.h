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

private:
    /** Constructor */
    FN2CNodeCollector() = default;
};
