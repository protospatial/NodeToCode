// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CBlueprintValidator.h"

bool FN2CBlueprintValidator::Validate(const FN2CBlueprint& Blueprint, FString& OutError)
{
    // Validate required fields
    if (!ValidateRequired(Blueprint, OutError))
    {
        return false;
    }

    // Validate graphs
    if (!ValidateGraphs(Blueprint, OutError))
    {
        return false;
    }

    // Validate structs
    if (!ValidateStructs(Blueprint, OutError))
    {
        return false;
    }

    // Validate enums
    if (!ValidateEnums(Blueprint, OutError))
    {
        return false;
    }

    return true;
}

bool FN2CBlueprintValidator::ValidateRequired(const FN2CBlueprint& Blueprint, FString& OutError)
{
    // Check version
    if (Blueprint.Version.Value.IsEmpty() || Blueprint.Version.Value != TEXT("1.0.0"))
    {
        OutError = TEXT("Invalid or missing version");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Check metadata
    if (Blueprint.Metadata.Name.IsEmpty())
    {
        OutError = TEXT("Missing Blueprint name");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    if (Blueprint.Metadata.BlueprintClass.IsEmpty())
    {
        OutError = TEXT("Missing Blueprint class");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Check graphs array
    if (Blueprint.Graphs.Num() == 0)
    {
        OutError = TEXT("No graphs found");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    return true;
}

bool FN2CBlueprintValidator::ValidateGraphs(const FN2CBlueprint& Blueprint, FString& OutError)
{
    // Check that at least one graph has nodes
    bool bHasNodes = false;
    for (const FN2CGraph& Graph : Blueprint.Graphs)
    {
        if (Graph.Nodes.Num() > 0)
        {
            bHasNodes = true;
            break;
        }
    }

    if (!bHasNodes)
    {
        OutError = TEXT("No nodes found in any graph");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Validate each graph
    for (const FN2CGraph& Graph : Blueprint.Graphs)
    {
        if (!ValidateGraph(Graph, OutError))
        {
            OutError = FString::Printf(TEXT("Invalid graph: %s - %s"), *Graph.Name, *OutError);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
    }

    return true;
}

bool FN2CBlueprintValidator::ValidateGraph(const FN2CGraph& Graph, FString& OutError)
{
    if (Graph.Name.IsEmpty())
    {
        OutError = TEXT("Empty graph name");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Check nodes array
    if (Graph.Nodes.Num() == 0)
    {
        OutError = FString::Printf(TEXT("No nodes in graph %s"), *Graph.Name);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Build set of node IDs for validation
    TSet<FString> NodeIds;
    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        FString NodeError;
        if (!NodeValidator.Validate(Node, NodeError))
        {
            OutError = FString::Printf(TEXT("Invalid node %s in graph %s: %s"), *Node.ID, *Graph.Name, *NodeError);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }

        // Check for duplicate node IDs
        if (NodeIds.Contains(Node.ID))
        {
            OutError = FString::Printf(TEXT("Duplicate node ID %s in graph %s"), *Node.ID, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
        NodeIds.Add(Node.ID);
    }

    // Log all node IDs for debugging
    FString NodeIdList = TEXT("Valid Node IDs in graph ") + Graph.Name + TEXT(": ");
    for (const FString& Id : NodeIds)
    {
        NodeIdList += Id + TEXT(", ");
    }
    FN2CLogger::Get().Log(NodeIdList, EN2CLogSeverity::Debug);

    // Validate flow references
    if (!ValidateFlowReferences(Graph, OutError))
    {
        return false;
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Graph %s validation successful: %d nodes, %d execution flows, %d data flows"), 
        *Graph.Name, Graph.Nodes.Num(), Graph.Flows.Execution.Num(), Graph.Flows.Data.Num()), EN2CLogSeverity::Debug);

    return true;
}

bool FN2CBlueprintValidator::ValidateFlowReferences(const FN2CGraph& Graph, FString& OutError)
{
    // Build lookup maps for validation
    TSet<FString> NodeIds;
    TMap<FString, TSet<FString>> NodePinIds;

    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        NodeIds.Add(Node.ID);
        
        // Track all pin IDs for this node
        TSet<FString>& PinIds = NodePinIds.Add(Node.ID);
        for (const FN2CPinDefinition& Pin : Node.InputPins)
        {
            PinIds.Add(Pin.ID);
        }
        for (const FN2CPinDefinition& Pin : Node.OutputPins)
        {
            PinIds.Add(Pin.ID);
        }
    }

    // Validate execution flows
    for (const FString& ExecFlow : Graph.Flows.Execution)
    {
        TArray<FString> FlowNodes;
        ExecFlow.ParseIntoArray(FlowNodes, TEXT("->"));
        
        // Each flow must have at least 2 nodes
        if (FlowNodes.Num() < 2)
        {
            OutError = FString::Printf(TEXT("Invalid execution flow %s (needs at least 2 nodes) in graph %s"), *ExecFlow, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }

        // Verify all referenced nodes exist
        for (const FString& NodeId : FlowNodes)
        {
            if (!NodeIds.Contains(NodeId))
            {
                OutError = FString::Printf(TEXT("Execution flow %s references non-existent node %s in graph %s"), *ExecFlow, *NodeId, *Graph.Name);
                FN2CLogger::Get().LogError(OutError);
                return false;
            }
        }
    }

    // Check data flows
    for (const auto& DataFlow : Graph.Flows.Data)
    {
        // Validate source pin format (N#.P#)
        TArray<FString> SourceParts;
        DataFlow.Key.ParseIntoArray(SourceParts, TEXT("."));
        if (SourceParts.Num() != 2 || !NodeIds.Contains(SourceParts[0]))
        {
            OutError = FString::Printf(TEXT("Invalid source pin format %s in graph %s"), *DataFlow.Key, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }

        // Validate target pin format (N#.P#)
        TArray<FString> TargetParts;
        DataFlow.Value.ParseIntoArray(TargetParts, TEXT("."));
        if (TargetParts.Num() != 2 || !NodeIds.Contains(TargetParts[0]))
        {
            OutError = FString::Printf(TEXT("Invalid target pin format %s in graph %s"), *DataFlow.Value, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
    }

    return true;
}

bool FN2CBlueprintValidator::ValidateStructs(const FN2CBlueprint& Blueprint, FString& OutError)
{
    for (const FN2CStruct& Struct : Blueprint.Structs)
    {
        if (!ValidateStruct(Struct, OutError))
        {
            OutError = FString::Printf(TEXT("Invalid struct: %s - %s"), *Struct.Name, *OutError);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
    }
    return true;
}

bool FN2CBlueprintValidator::ValidateStruct(const FN2CStruct& Struct, FString& OutError)
{
    // A struct needs a name at minimum
    if (Struct.Name.IsEmpty())
    {
        OutError = TEXT("Missing name");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }
    
    // Validate all members
    for (int32 i = 0; i < Struct.Members.Num(); ++i)
    {
        const FN2CStructMember& Member = Struct.Members[i];
        
        if (Member.Name.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Member at index %d has no name"), i);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
        
        // For struct/enum/object/class types, verify we have a type name
        if ((Member.Type == EN2CStructMemberType::Struct || 
             Member.Type == EN2CStructMemberType::Enum ||
             Member.Type == EN2CStructMemberType::Object ||
             Member.Type == EN2CStructMemberType::Class) && 
            Member.TypeName.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Member %s requires a type name"), *Member.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
    }
    
    return true;
}

bool FN2CBlueprintValidator::ValidateEnums(const FN2CBlueprint& Blueprint, FString& OutError)
{
    for (const FN2CEnum& Enum : Blueprint.Enums)
    {
        if (!ValidateEnum(Enum, OutError))
        {
            OutError = FString::Printf(TEXT("Invalid enum: %s - %s"), *Enum.Name, *OutError);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
    }
    return true;
}

bool FN2CBlueprintValidator::ValidateEnum(const FN2CEnum& Enum, FString& OutError)
{
    // An enum needs a name at minimum
    if (Enum.Name.IsEmpty())
    {
        OutError = TEXT("Missing name");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }
    
    // Validate all values
    for (int32 i = 0; i < Enum.Values.Num(); ++i)
    {
        const FN2CEnumValue& Value = Enum.Values[i];
        
        if (Value.Name.IsEmpty())
        {
            OutError = FString::Printf(TEXT("Value at index %d has no name"), i);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
    }
    
    return true;
}
