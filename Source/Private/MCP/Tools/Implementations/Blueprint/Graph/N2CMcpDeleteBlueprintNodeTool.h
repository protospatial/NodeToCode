// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

// Forward declarations
class UEdGraph;
class UEdGraphNode;
class UBlueprint;
struct FN2CDeletedNodeInfo;

/**
 * MCP Tool for deleting Blueprint nodes from the currently focused graph
 * Supports batch deletion and optional connection preservation
 */
class NODETOCODE_API FN2CMcpDeleteBlueprintNodeTool : public FN2CMcpToolBase
{
public:
    FN2CMcpDeleteBlueprintNodeTool() = default;
    
    // IN2CMcpTool interface
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }
    
private:
    /**
     * Represents information about a connection that was preserved
     */
    struct FPreservedConnection
    {
        FString FromNodeGuid;
        FString FromPinName;
        FString ToNodeGuid;
        FString ToPinName;
        FString PinType;
    };
    
    /**
     * Represents information about a deleted node
     */
    struct FDeletedNodeInfo
    {
        FString NodeGuid;
        FString NodeTitle;
        FString NodeType;
        FString GraphName;
        TArray<FPreservedConnection> PreservedConnections;
    };
    
    // Helper methods
    bool ParseArguments(const TSharedPtr<FJsonObject>& Arguments,
                       TArray<FGuid>& OutNodeGuids,
                       bool& OutPreserveConnections,
                       bool& OutForce,
                       FString& OutError);
    
    bool ValidateNodes(UEdGraph* Graph,
                      const TArray<FGuid>& NodeGuids,
                      TArray<UEdGraphNode*>& OutValidNodes,
                      bool bForce,
                      FString& OutError);
    
    bool IsNodeDeletable(UEdGraphNode* Node, bool bForce) const;
    
    bool PreserveNodeConnections(UEdGraphNode* NodeToDelete,
                                TArray<FPreservedConnection>& OutPreservedConnections);
    
    void CollectNodeInfo(UEdGraphNode* Node,
                        FDeletedNodeInfo& OutInfo);
    
    bool DeleteNodes(UBlueprint* Blueprint,
                    UEdGraph* Graph,
                    const TArray<UEdGraphNode*>& NodesToDelete,
                    bool bPreserveConnections,
                    TArray<FDeletedNodeInfo>& OutDeletedInfo);
    
    TSharedPtr<FJsonObject> BuildSuccessResult(const TArray<FDeletedNodeInfo>& DeletedInfo,
                                              const FString& BlueprintName) const;
    
    void ShowDeletionNotification(int32 DeletedCount, bool bSuccess) const;
};