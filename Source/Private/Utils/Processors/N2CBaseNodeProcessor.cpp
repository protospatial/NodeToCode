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

    if (OutNodeDef.MemberName.IsEmpty())
    {
        ExtractInputActionReference(Node, OutNodeDef);
    }
    
    return true;
}

void FN2CBaseNodeProcessor::ExtractInputActionReference(const UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Enhanced Input's nodes (the action event, Get Input Action Value) name their UInputAction in an object property
    // "InputAction". Read by reflection, so NodeToCode needs no dependency on the InputBlueprintNodes module.
    const FObjectPropertyBase* ActionProperty = FindFProperty<FObjectPropertyBase>(Node->GetClass(), TEXT("InputAction"));
    const UObject* Action = ActionProperty ? ActionProperty->GetObjectPropertyValue_InContainer(Node) : nullptr;
    if (!Action)
    {
        return;
    }
    OutNodeDef.MemberName = Action->GetName();
    OutNodeDef.MemberParent = Action->GetOutermost()->GetName();
}

void FN2CBaseNodeProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Base implementation does nothing - to be overridden by subclasses
}
