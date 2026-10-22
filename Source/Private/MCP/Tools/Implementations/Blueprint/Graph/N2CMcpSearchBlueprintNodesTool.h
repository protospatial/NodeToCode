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

    /** Check if the action's category matches the filter */
    static bool MatchesCategoryFilter(const FString& ActionSearchText, const FString& CategoryFilter);

    /** Check if the action is a VMFunction that should be excluded */
    static bool IsExcludedVMFunction(const FString& ActionSearchText);
    
    bool GetContextFromPaths(const TSharedPtr<FJsonObject>& BlueprintContext,
                            UBlueprint*& OutBlueprint,
                            UEdGraph*& OutGraph,
                            FString& OutError);
    
    TSharedPtr<FJsonObject> ConvertActionToJson(
        const TSharedPtr<FEdGraphSchemaAction>& Action,
        bool bIsContextSensitive,
        UBlueprint* ContextBlueprint,
        UEdGraph* ContextGraph);
    
    static FString ExtractInternalName(const TSharedPtr<FEdGraphSchemaAction>& Action);
    static TArray<FString> ExtractCategoryPath(const TSharedPtr<FEdGraphSchemaAction>& Action);
    
    // New helper method to convert UK2Node to N2CJSON format
    static TSharedPtr<FJsonObject> ConvertNodeToN2CJson(UK2Node* Node, const FString& NodeId);
    
    // Helper to create a template node definition from action metadata
    static FN2CNodeDefinition CreateNodeDefinitionFromAction(const TSharedPtr<FEdGraphSchemaAction>& Action, const FString& NodeId);
};