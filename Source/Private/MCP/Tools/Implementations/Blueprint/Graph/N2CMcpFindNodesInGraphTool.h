// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Models/N2CNode.h"
#include "Models/N2CBlueprint.h"

class UK2Node;
class FJsonObject;
struct FN2CCapture;

/**
 * MCP tool that searches for specific nodes in the focused Blueprint graph
 * by keywords or node GUIDs and returns them in N2C JSON format.
 */
class FN2CMcpFindNodesInGraphTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Parse and validate input arguments
     */
    bool ParseArguments(
        const TSharedPtr<FJsonObject>& Arguments,
        TArray<FString>& OutSearchTerms,
        FString& OutSearchType,
        bool& OutCaseSensitive,
        int32& OutMaxResults,
        FString& OutError
    );

    /**
     * Check if a node matches the search criteria
     */
    bool DoesNodeMatchSearch(
        UK2Node* Node,
        const TArray<FString>& SearchTerms,
        const FString& SearchType,
        bool bCaseSensitive
    );

    /** Node ID -> the node's N2C JSON object, from one serialization of the call's capture */
    static TMap<FString, TSharedPtr<FJsonObject>> IndexNodesById(const FN2CCapture& Capture);

    /**
     * Enhance a node JSON object with GUID information
     */
    void EnhanceNodeWithGuids(
        TSharedPtr<FJsonObject> NodeObject,
        const FGuid& NodeGuid,
        const FN2CCapture& Capture
    );
};