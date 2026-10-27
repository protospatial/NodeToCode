// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Utils/N2CStableNames.h"

#include "K2Node_BaseAsyncTask.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Timeline.h"

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

    // Any function call without a processor of its own (Add Component, Spawn Actor, a plugin's call node) still names
    // its function, as the CallFunction processor does (card 0239: unregistered types no longer get that processor)
    const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
    if (Call && OutNodeDef.MemberName.IsEmpty())
    {
        if (const UFunction* Function = Call->GetTargetFunction())
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(Function->GetOwnerClass());
            OutNodeDef.MemberName = Function->GetName();
        }
    }

    // Latent, for every kind of node (card 0239, translation-core-14): a latent function call, an async proxy node
    // (UK2Node_BaseAsyncTask: AI MoveTo, async actions) and a Timeline all finish later
    OutNodeDef.bLatent = OutNodeDef.bLatent || (Call && Call->IsLatentFunction()) || Node->IsA<UK2Node_BaseAsyncTask>()
        || Node->IsA<UK2Node_Timeline>();

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
