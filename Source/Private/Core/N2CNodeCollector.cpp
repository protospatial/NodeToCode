// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CNodeCollector.h"
#include "Utils/N2CLogger.h"

FN2CNodeCollector& FN2CNodeCollector::Get()
{
    static FN2CNodeCollector Instance;
    return Instance;
}

bool FN2CNodeCollector::CollectNodesFromGraph(UEdGraph* Graph, TArray<UK2Node*>& OutNodes)
{
    if (!Graph)
    {
        FN2CLogger::Get().LogWarning(TEXT("Invalid graph provided to CollectNodesFromGraph"));
        return false;
    }

    // Get Blueprint name for context
    FString BlueprintName = TEXT("Unknown");
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Graph->GetOuter()))
    {
        BlueprintName = Blueprint->GetName();
    }

    // Log collection start
    FString Context = FString::Printf(TEXT("Blueprint: %s, Graph: %s"),
        *BlueprintName,
        *Graph->GetName());
    FN2CLogger::Get().Log(TEXT("Starting node collection"), EN2CLogSeverity::Info, Context);

    // Collect K2 nodes from the graph
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node* K2Node = Cast<UK2Node>(Node))
        {
            OutNodes.Add(K2Node);
        }
    }

    // Log collection results
    FString ResultContext = FString::Printf(TEXT("%d nodes collected from %s"), 
        OutNodes.Num(), 
        *Context);
    FN2CLogger::Get().Log(TEXT("Node collection complete"), EN2CLogSeverity::Info, ResultContext);

    return true;
}

bool FN2CNodeCollector::CollectPinsFromNode(UK2Node* Node, TArray<UEdGraphPin*>& OutInputPins, TArray<UEdGraphPin*>& OutOutputPins)
{
    if (!Node)
    {
        FN2CLogger::Get().LogWarning(TEXT("Invalid node provided to CollectPinsFromNode"));
        return false;
    }

    // Clear output arrays
    OutInputPins.Empty();
    OutOutputPins.Empty();

    // Get all pins from node
    TArray<UEdGraphPin*> AllPins = Node->Pins;
    
    // Sort pins by direction
    for (UEdGraphPin* Pin : AllPins)
    {
        if (!ValidatePin(Pin))
        {
            continue;
        }

        if (Pin->Direction == EGPD_Input)
        {
            OutInputPins.Add(Pin);
        }
        else if (Pin->Direction == EGPD_Output)
        {
            OutOutputPins.Add(Pin);
        }
    }

    // Debug additional metadata for K2 nodes
    if (UK2Node* K2Node = Cast<UK2Node>(Node))
    {
        for (UEdGraphPin* Pin : OutInputPins)
        {
            GetK2PinMetadata(K2Node, Pin);
            CollectPinDefaultValue(Pin);
            CollectPinConnections(Pin);
        }
        for (UEdGraphPin* Pin : OutOutputPins)
        {
            GetK2PinMetadata(K2Node, Pin);
            CollectPinDefaultValue(Pin);
            CollectPinConnections(Pin);
        }
    }

    // Log pin collection results
    FString Context = FString::Printf(TEXT("Node: %s, Input Pins: %d, Output Pins: %d"),
        *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
        OutInputPins.Num(),
        OutOutputPins.Num());
    FN2CLogger::Get().Log(TEXT("Pin collection complete"), EN2CLogSeverity::Info, Context);

    return true;
}

bool FN2CNodeCollector::ValidatePin(UEdGraphPin* Pin) const
{
    if (!Pin)
    {
        return false;
    }

    // Skip orphaned pins
    if (Pin->GetOwningNode() == nullptr)
    {
        return false;
    }

    // Skip broken pins
    if (Pin->Direction == EGPD_MAX)
    {
        return false;
    }

    return true;
}

void FN2CNodeCollector::GetK2PinMetadata(UK2Node* K2Node, UEdGraphPin* Pin)
{
    if (!K2Node || !Pin)
    {
        return;
    }

    // Get pin type information
    FEdGraphPinType PinType = Pin->PinType;

    // Log detailed pin type info
    // Get container type string
    FString ContainerTypeStr;
    switch (PinType.ContainerType)
    {
        case EPinContainerType::Array:
            ContainerTypeStr = TEXT("Array");
            break;
        case EPinContainerType::Map:
            ContainerTypeStr = TEXT("Map");
            break;
        case EPinContainerType::Set:
            ContainerTypeStr = TEXT("Set");
            break;
        default:
            ContainerTypeStr = TEXT("None");
            break;
    }

    FString TypeInfo = FString::Printf(TEXT("Pin: %s, Type: %s, SubCategory: %s, ContainerType: %s, bIsReference: %d"),
        *Pin->GetDisplayName().ToString(),
        *PinType.PinCategory.ToString(),
        *PinType.PinSubCategory.ToString(),
        *ContainerTypeStr,
        PinType.bIsReference ? 1 : 0);
    
    FN2CLogger::Get().Log(TEXT("K2 Pin Details"), EN2CLogSeverity::Debug, TypeInfo);
}

void FN2CNodeCollector::CollectPinDefaultValue(UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return;
    }

    // Get default value if it exists
    if (!Pin->DefaultValue.IsEmpty())
    {
        FString Context = FString::Printf(TEXT("Pin: %s, Default Value: %s"),
            *Pin->GetDisplayName().ToString(),
            *Pin->DefaultValue);
        FN2CLogger::Get().Log(TEXT("Pin Default Value"), EN2CLogSeverity::Debug, Context);
    }
    
    // Handle default object/class values
    if (Pin->DefaultObject)
    {
        FString Context = FString::Printf(TEXT("Pin: %s, Default Object: %s"),
            *Pin->GetDisplayName().ToString(),
            *Pin->DefaultObject->GetName());
        FN2CLogger::Get().Log(TEXT("Pin Default Object"), EN2CLogSeverity::Debug, Context);
    }
    
    // Handle default text value
    if (!Pin->DefaultTextValue.IsEmpty())
    {
        FString Context = FString::Printf(TEXT("Pin: %s, Default Text: %s"),
            *Pin->GetDisplayName().ToString(),
            *Pin->DefaultTextValue.ToString());
        FN2CLogger::Get().Log(TEXT("Pin Default Text"), EN2CLogSeverity::Debug, Context);
    }
}

void FN2CNodeCollector::CollectPinConnections(UEdGraphPin* Pin)
{
    if (!Pin)
    {
        return;
    }

    // Get all connected pins
    TArray<UEdGraphPin*> LinkedPins = Pin->LinkedTo;
    
    if (LinkedPins.Num() > 0)
    {
        FString Context = FString::Printf(TEXT("Pin: %s has %d connections:"),
            *Pin->GetDisplayName().ToString(),
            LinkedPins.Num());
            
        for (UEdGraphPin* LinkedPin : LinkedPins)
        {
            if (LinkedPin && LinkedPin->GetOwningNode())
            {
                Context += FString::Printf(TEXT("\n  - Connected to: %s.%s"),
                    *LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
                    *LinkedPin->GetDisplayName().ToString());
            }
        }
        
        FN2CLogger::Get().Log(TEXT("Pin Connections"), EN2CLogSeverity::Debug, Context);
    }
}
