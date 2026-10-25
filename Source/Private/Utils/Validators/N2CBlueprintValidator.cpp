// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CBlueprintValidator.h"
#include "Models/N2CJsonContract.h"

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
    // Check version: a version this build reads (1.x and 2.x; FromJson maps 1.x to N2CJsonContract::Version)
    if (Blueprint.Version.Value.IsEmpty() || !N2CJsonContract::IsReadable(Blueprint.Version.Value))
    {
        OutError = FString::Printf(TEXT("Invalid or missing version '%s'"), *Blueprint.Version.Value);
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
    // Build lookup maps for validation: node IDs, and each node's exec input and exec output pin IDs
    TSet<FString> NodeIds;
    TMap<FString, TSet<FString>> NodeExecOutputs;
    TMap<FString, TSet<FString>> NodeExecInputs;

    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        NodeIds.Add(Node.ID);
        
        TSet<FString>& ExecInputs = NodeExecInputs.Add(Node.ID);
        TSet<FString>& ExecOutputs = NodeExecOutputs.Add(Node.ID);
        for (const FN2CPinDefinition& Pin : Node.InputPins)
        {
            if (Pin.Type == EN2CPinType::Exec)
            {
                ExecInputs.Add(Pin.ID);
            }
        }
        for (const FN2CPinDefinition& Pin : Node.OutputPins)
        {
            if (Pin.Type == EN2CPinType::Exec)
            {
                ExecOutputs.Add(Pin.ID);
            }
        }
    }

    // Validate execution flows: "N3.P5->N4.P1" from an exec output pin to an exec input pin (N2C JSON 2.0.0), or the
    // node-only form "N1->N2" ("N1->N2->N3") of 1.x documents and earlier 2.0 builds, whose nodes must exist
    for (const FString& ExecFlow : Graph.Flows.Execution)
    {
        TArray<N2CJsonContract::FExecEndpoint> Endpoints;
        if (!N2CJsonContract::ParseExecFlow(ExecFlow, Endpoints))
        {
            OutError = FString::Printf(TEXT("Invalid execution flow %s (expected \"N1.P2->N3.P4\" or \"N1->N2\") in graph %s"), *ExecFlow, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }

        const bool bPinForm = Endpoints[0].Pin.Len() > 0 || Endpoints[1].Pin.Len() > 0;
        if (bPinForm && (Endpoints.Num() != 2 || Endpoints[0].Pin.IsEmpty() || Endpoints[1].Pin.IsEmpty()))
        {
            OutError = FString::Printf(TEXT("Execution flow %s mixes pin refs and node IDs (a pin-qualified entry is one \"N1.P2->N3.P4\" link) in graph %s"), *ExecFlow, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }

        for (int32 Index = 0; Index < Endpoints.Num(); ++Index)
        {
            const N2CJsonContract::FExecEndpoint& Endpoint = Endpoints[Index];
            if (!NodeIds.Contains(Endpoint.Node))
            {
                OutError = FString::Printf(TEXT("Execution flow %s references non-existent node %s in graph %s"), *ExecFlow, *Endpoint.Node, *Graph.Name);
                FN2CLogger::Get().LogError(OutError);
                return false;
            }
            if (!bPinForm)
            {
                continue;
            }
            const bool bSource = Index == 0;
            const TSet<FString>& ExecPins = bSource ? NodeExecOutputs.FindChecked(Endpoint.Node) : NodeExecInputs.FindChecked(Endpoint.Node);
            if (!ExecPins.Contains(Endpoint.Pin))
            {
                OutError = FString::Printf(TEXT("Execution flow %s: %s is not an exec %s pin of node %s in graph %s"), *ExecFlow,
                    *Endpoint.Pin, bSource ? TEXT("output") : TEXT("input"), *Endpoint.Node, *Graph.Name);
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

        // Validate every target pin's format (N#.P#)
        if (DataFlow.Value.Targets.Num() == 0)
        {
            OutError = FString::Printf(TEXT("Data flow %s has no target in graph %s"), *DataFlow.Key, *Graph.Name);
            FN2CLogger::Get().LogError(OutError);
            return false;
        }
        for (const FString& Target : DataFlow.Value.Targets)
        {
            TArray<FString> TargetParts;
            Target.ParseIntoArray(TargetParts, TEXT("."));
            if (TargetParts.Num() != 2 || !NodeIds.Contains(TargetParts[0]))
            {
                OutError = FString::Printf(TEXT("Invalid target pin format %s in graph %s"), *Target, *Graph.Name);
                FN2CLogger::Get().LogError(OutError);
                return false;
            }
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
