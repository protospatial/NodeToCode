// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphNode_Comment;
class UBlueprint;

/**
 * MCP tool for creating comment nodes around specified Blueprint nodes.
 * Requires the get-focused-blueprint tool to be called first to obtain node GUIDs.
 */
class FN2CMcpCreateCommentNodeTool : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    virtual bool RequiresGameThread() const override { return true; }

private:
    /**
     * Represents a comment node creation request
     */
    struct FCommentNodeRequest
    {
        TArray<FString> NodeGuids;
        FString CommentText = TEXT("Comment");
        FLinearColor Color = FLinearColor::White;
        int32 FontSize = 18;
        bool bGroupMovement = true;
        float Padding = 50.0f;
    };

    /**
     * Parses and validates the input arguments
     */
    bool ParseArguments(const TSharedPtr<FJsonObject>& Arguments, FCommentNodeRequest& OutRequest, FString& OutError) const;

    /**
     * Finds nodes by their GUIDs in the given graph
     */
    TArray<UEdGraphNode*> FindNodesByGuids(UEdGraph* Graph, const TArray<FString>& NodeGuids, TArray<FString>& OutMissingGuids) const;

    /**
     * Calculates the bounding box that encompasses all given nodes
     */
    FSlateRect CalculateBoundingBox(const TArray<UEdGraphNode*>& Nodes, float Padding) const;

    /**
     * Builds the JSON result for the tool response
     */
    TSharedPtr<FJsonObject> BuildResultJson(
        bool bSuccess,
        UEdGraphNode_Comment* CommentNode,
        const TArray<UEdGraphNode*>& IncludedNodes,
        const TArray<FString>& MissingGuids,
        const FString& ErrorMessage = TEXT("")
    ) const;
};