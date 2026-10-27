// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/Blueprint.h"

// Forward declarations
class UEdGraph;
struct FBlueprintActionMenuBuilder;

/**
 * MCP Tool for adding Blueprint nodes to the active graph
 * Uses node name and actionIdentifier to find and spawn specific nodes
 */
class NODETOCODE_API FN2CMcpAddBlueprintNodeTool : public FN2CMcpToolBase
{
public:
    FN2CMcpAddBlueprintNodeTool();
    
    // IN2CMcpTool interface
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

    /**
     * Resolves ActionIdentifier (a search result's stable ID, or a pre-0277 search-text identifier) in Graph's action
     * list and spawns that action into Graph. Game thread only. Public so automation tests can add nodes to an
     * explicit graph without a focused editor.
     */
    bool FindAndSpawnNode(const FString& NodeName,
                         const FString& ActionIdentifier,
                         UBlueprint* Blueprint,
                         UEdGraph* Graph,
                         const FVector2D& Location,
                         UEdGraphNode*& OutSpawnedNode,
                         FString& OutError);

private:
    // Helper methods
    bool ParseArguments(const TSharedPtr<FJsonObject>& Arguments,
                       FString& OutNodeName,
                       FString& OutActionIdentifier,
                       FVector2D& OutLocation,
                       FString& OutError);
};
