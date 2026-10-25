// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/Blueprint.h"
#include "Models/N2CNode.h"
// Forward declarations

class UEdGraph;
struct FBlueprintActionMenuBuilder;

/**
 * MCP Tool for searching Blueprint nodes/actions
 */
class NODETOCODE_API FN2CMcpSearchBlueprintNodesTool : public FN2CMcpToolBase
{
public:
    FN2CMcpSearchBlueprintNodesTool();
    
    // IN2CMcpTool interface
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

    /**
     * Runs the search against an explicit context and returns the result nodes. Read-only: it never spawns,
     * removes or modifies nodes in ContextGraph and never transacts on or dirties ContextBlueprint. Pins come
     * from the node spawner's template node, which lives in FBlueprintNodeTemplateCache's transient graph.
     * Game thread only. Public so automation tests can run the search without a focused editor.
     */
    TArray<TSharedPtr<FJsonValue>> SearchActions(const FString& SearchTerm,
                                                 int32 MaxResults,
                                                 bool bContextSensitive,
                                                 UBlueprint* ContextBlueprint,
                                                 UEdGraph* ContextGraph,
                                                 const FString& CategoryFilter,
                                                 bool bExcludeVMFunctions);

    /**
     * Resolves blueprintContext (owningBlueprintPath, graphPath) to a Blueprint and one of its graphs. A Blueprint
     * that does not load fails with ASSET_NOT_FOUND and a graph that is not in it with GRAPH_NOT_FOUND; it never
     * falls back to another context. Without graphPath: the focused graph if it is this Blueprint's, else its event
     * graph. Public for the automation tests.
     */
    static bool GetContextFromPaths(const TSharedPtr<FJsonObject>& BlueprintContext,
                            UBlueprint*& OutBlueprint,
                            UEdGraph*& OutGraph,
                            FString& OutError);

private:
    // Helper methods
    bool ParseArguments(const TSharedPtr<FJsonObject>& Arguments,
                       FString& OutSearchTerm,
                       bool& OutContextSensitive,
                       int32& OutMaxResults,
                       TSharedPtr<FJsonObject>& OutBlueprintContext,
                       FString& OutCategoryFilter,
                       bool& bOutExcludeVMFunctions,
                       FString& OutError);

    TSharedPtr<FJsonObject> ConvertActionToJson(
        const TSharedPtr<FEdGraphSchemaAction>& Action,
        const FString& NodeId,
        bool bIsContextSensitive,
        UEdGraph* ContextGraph);

    // New helper method to convert UK2Node to N2CJSON format
    static TSharedPtr<FJsonObject> ConvertNodeToN2CJson(UK2Node* Node, const FString& NodeId);
    
    /** The spawner's cached template node for a menu action, or null; never spawns into ContextGraph */
    static UEdGraphNode* GetTemplateNodeForAction(const TSharedPtr<FEdGraphSchemaAction>& Action, UEdGraph* ContextGraph);

    // Helper to create a template node definition from action metadata
    static FN2CNodeDefinition CreateNodeDefinitionFromAction(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& NodeId);
};