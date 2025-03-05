// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CEventProcessor.h"

void FN2CEventProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Handle standard event nodes
    if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
    {
        OutNodeDef.MemberName = GetCleanClassName(EventNode->EventReference.GetMemberName().ToString());
        if (UClass* EventClass = EventNode->EventReference.GetMemberParentClass())
        {
            OutNodeDef.MemberParent = GetCleanClassName(EventClass->GetPathName());
        }
        
        // Log event details
        FString EventInfo = FString::Printf(TEXT("Event: %s, Parent: %s"),
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(EventInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle custom events
    if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
    {
        OutNodeDef.MemberName = CustomEventNode->CustomFunctionName.ToString();
        if (UBlueprint* BP = CustomEventNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = GetCleanClassName(BP->GetName());
        }
        
        // Log custom event details
        FString EventInfo = FString::Printf(TEXT("Custom Event: %s, Parent: %s"),
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(EventInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle actor bound events
    if (UK2Node_ActorBoundEvent* ActorEventNode = Cast<UK2Node_ActorBoundEvent>(Node))
    {
        OutNodeDef.MemberName = ActorEventNode->DelegatePropertyName.ToString();
        if (UClass* DelegateClass = ActorEventNode->DelegateOwnerClass)
        {
            OutNodeDef.MemberParent = GetCleanClassName(DelegateClass->GetName());
        }
        
        // Log actor bound event details
        FString EventInfo = FString::Printf(TEXT("Actor Bound Event: %s, Parent: %s"),
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(EventInfo, EN2CLogSeverity::Debug);
        return;
    }
    
    // Handle component bound events
    if (UK2Node_ComponentBoundEvent* CompEventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
    {
        OutNodeDef.MemberName = CompEventNode->DelegatePropertyName.ToString();
        if (UClass* DelegateClass = CompEventNode->DelegateOwnerClass)
        {
            OutNodeDef.MemberParent = GetCleanClassName(DelegateClass->GetName());
        }
        
        // Log component bound event details
        FString EventInfo = FString::Printf(TEXT("Component Bound Event: %s, Parent: %s"),
            *OutNodeDef.MemberName,
            *OutNodeDef.MemberParent);
        FN2CLogger::Get().Log(EventInfo, EN2CLogSeverity::Debug);
        return;
    }
}
