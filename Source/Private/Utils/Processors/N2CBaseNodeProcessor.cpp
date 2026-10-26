// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Models/N2CLogging.h"

bool FN2CBaseNodeProcessor::Process(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Node)
    {
        FN2CLogger::Get().LogError(TEXT("Null node provided to processor"));
        return false;
    }
    
    // Extract common properties
    OutNodeDef.Name = Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
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
    N2C_LOG_FUNCTION_ENTRY();
    // Base implementation does nothing - to be overridden by subclasses
}

FString FN2CBaseNodeProcessor::GetCleanClassName(const FString& InName)
{
    N2C_LOG_FUNCTION_ENTRY();
    FString CleanName = InName;
    
    // Remove SKEL_ prefix if present
    if (CleanName.StartsWith(TEXT("SKEL_")))
    {
        CleanName.RightChopInline(5);
    }
    
    // Remove _C suffix if present (indicates Blueprint class)
    if (CleanName.EndsWith(TEXT("_C")))
    {
        CleanName.LeftChopInline(2);
    }
    
    return CleanName;
}
