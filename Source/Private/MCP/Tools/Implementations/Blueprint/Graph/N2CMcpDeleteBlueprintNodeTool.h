// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "MCP/Utils/N2CMcpNodeDeletion.h"

// Forward declarations
class UEdGraph;
class UEdGraphNode;
class UBlueprint;

/**
 * MCP Tool for deleting Blueprint nodes from the currently focused graph
 * Supports batch deletion and optional connection preservation; the deletion itself is N2CNodeDeletion (card 0279)
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
    
    TSharedPtr<FJsonObject> BuildSuccessResult(const TArray<N2CNodeDeletion::FDeletedNode>& DeletedInfo,
                                              const FString& BlueprintName) const;
    
    void ShowDeletionNotification(int32 DeletedCount, bool bSuccess) const;
};
