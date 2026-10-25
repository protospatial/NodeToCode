// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Utils/N2CStableNames.h"

bool FN2CBaseNodeProcessor::Process(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    if (!Node)
    {
        FN2CLogger::Get().LogError(TEXT("Null node provided to processor"));
        return false;
    }
    
    // Extract common properties. The title is a hint in source text, the same in every editor language (card 0138)
    OutNodeDef.Title = N2CStableNames::NodeTitle(Node);
    OutNodeDef.bPure = Node->IsNodePure();
    
    // Extract comments if present
    if (Node->NodeComment.Len() > 0)
    {
        OutNodeDef.Comment = Node->NodeComment;
    }
    
    // Extract node-specific properties
    ExtractNodeProperties(Node, OutNodeDef);
    
    return true;
}

void FN2CBaseNodeProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Base implementation does nothing - to be overridden by subclasses
}
