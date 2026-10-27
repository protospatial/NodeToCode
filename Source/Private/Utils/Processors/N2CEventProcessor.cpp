// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CEventProcessor.h"
#include "Utils/N2CStableNames.h"

#include "GameFramework/Actor.h"

void FN2CEventProcessor::ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef)
{
    // Most-derived first (card 0239, translation-core-16): custom, component-bound and actor-bound events are all
    // UK2Node_Event, so testing UK2Node_Event first left them with member_name "None" and no binding

    // A custom event: its name, declared by its Blueprint, with its replication (Server, Client or Multicast RPC)
    if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
    {
        OutNodeDef.MemberName = CustomEventNode->CustomFunctionName.ToString();
        if (const UBlueprint* Blueprint = CustomEventNode->GetBlueprint())
        {
            OutNodeDef.MemberParent = Blueprint->GetName();
        }
        const uint32 NetFlags = CustomEventNode->GetNetFlags();
        if ((NetFlags & FUNC_Net) != 0)
        {
            OutNodeDef.Net = (NetFlags & FUNC_NetServer) != 0 ? TEXT("Server")
                : (NetFlags & FUNC_NetClient) != 0 ? TEXT("Client")
                : (NetFlags & FUNC_NetMulticast) != 0 ? TEXT("Multicast")
                : FString();
            OutNodeDef.bReliable = (NetFlags & FUNC_NetReliable) != 0;
        }
        N2C_LOG(Debug, TEXT("Custom Event: %s, Parent: %s, Net: %s"), *OutNodeDef.MemberName, *OutNodeDef.MemberParent, *OutNodeDef.Net);
        return;
    }

    // A component-bound event (OnComponentHit of CollisionComp): the delegate, the class declaring it, and the component
    if (const UK2Node_ComponentBoundEvent* ComponentEventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
    {
        OutNodeDef.MemberName = ComponentEventNode->DelegatePropertyName.ToString();
        if (const UClass* DelegateClass = ComponentEventNode->DelegateOwnerClass)
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(DelegateClass);
        }
        OutNodeDef.BoundTo = ComponentEventNode->GetComponentPropertyName().ToString();
        N2C_LOG(Debug, TEXT("Component Bound Event: %s.%s, Parent: %s"), *OutNodeDef.BoundTo, *OutNodeDef.MemberName, *OutNodeDef.MemberParent);
        return;
    }

    // An actor-bound event (a level actor's delegate in a Level Blueprint): the delegate, its class, and the actor
    if (const UK2Node_ActorBoundEvent* ActorEventNode = Cast<UK2Node_ActorBoundEvent>(Node))
    {
        OutNodeDef.MemberName = ActorEventNode->DelegatePropertyName.ToString();
        if (const UClass* DelegateClass = ActorEventNode->DelegateOwnerClass)
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(DelegateClass);
        }
        if (const AActor* Owner = ActorEventNode->EventOwner)
        {
            OutNodeDef.BoundTo = Owner->GetName();
        }
        N2C_LOG(Debug, TEXT("Actor Bound Event: %s.%s, Parent: %s"), *OutNodeDef.BoundTo, *OutNodeDef.MemberName, *OutNodeDef.MemberParent);
        return;
    }

    // Any other event (an override such as ReceiveBeginPlay): the event function and the class that declares it
    if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
    {
        OutNodeDef.MemberName = EventNode->EventReference.GetMemberName().ToString();
        if (const UClass* EventClass = EventNode->EventReference.GetMemberParentClass())
        {
            OutNodeDef.MemberParent = N2CStableNames::ClassName(EventClass);
        }
        N2C_LOG(Debug, TEXT("Event: %s, Parent: %s"), *OutNodeDef.MemberName, *OutNodeDef.MemberParent);
    }
}
