// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CLogging.h"
#include "Utils/Validators/N2CBlueprintValidator.h"

namespace
{
bool IsCommentNodeClass(const FString& NodeClass)
{
    N2C_LOG_FUNCTION_ENTRY();
    return NodeClass.EndsWith(TEXT("EdGraphNode_Comment"), ESearchCase::CaseSensitive);
}

bool IsProvenNonExecutableGraph(const FN2CGraph& Graph)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Graph.bNoExecutableNodes || Graph.Nodes.Num() > 0 ||
        !Graph.Pins.IsEmpty() || !Graph.Connections.IsEmpty() ||
        !Graph.ConnectionErrors.IsEmpty() || Graph.UnresolvedConnectionCount > 0 ||
        !Graph.Flows.Execution.IsEmpty() || !Graph.Flows.Data.IsEmpty())
    {
        return false;
    }

    if (Graph.NativeNodeCount == 0 && Graph.SkippedNodeCount == 0)
    {
        return true;
    }
    if (Graph.NativeNodeCount <= 0 || Graph.SkippedNodeCount != Graph.NativeNodeCount ||
        Graph.SkippedNodeClasses.IsEmpty())
    {
        return false;
    }
    return Graph.SkippedNodeClasses.ContainsByPredicate([](const FString& NodeClass)
    {
        return !IsCommentNodeClass(NodeClass);
    }) == false;
}
}

bool FN2CBlueprintValidator::Validate(const FN2CBlueprint& Blueprint, FString& OutError)
{
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
    // Preserve legitimate empty/comment-only graphs, but never let an unknown
    // untranslated graph stand in for executable Blueprint content.
    bool bHasNonSkeletonGraph = false;
    bool bHasUsableNonSkeletonGraph = false;
    for (const FN2CGraph& Graph : Blueprint.Graphs)
    {
        if (Graph.GraphType != EN2CGraphType::ClassItSelf)
        {
            bHasNonSkeletonGraph = true;
            if (Graph.Nodes.Num() > 0 || IsProvenNonExecutableGraph(Graph))
            {
                bHasUsableNonSkeletonGraph = true;
            }
        }
    }

    if (bHasNonSkeletonGraph && !bHasUsableNonSkeletonGraph)
    {
        OutError = TEXT("No nodes found in any non-ClassItSelf graph");
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
    N2C_LOG_FUNCTION_ENTRY();
    if (Graph.Name.IsEmpty())
    {
        OutError = TEXT("Empty graph name");
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    // Special-case synthetic ClassItSelf graphs: they are allowed to have
    // no nodes, because they only carry class-level structure (UCLASS,
    // UPROPERTY members, ctor/dtor, components) and do not represent an
    // executable flow graph. For these graphs we skip node/flow validation
    // and treat them as structurally valid.
    if (Graph.GraphType == EN2CGraphType::ClassItSelf)
    {
        if (Graph.Nodes.Num() == 0)
        {
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("ClassItSelf graph %s has no nodes (expected for synthetic class skeleton graphs)"), *Graph.Name),
                EN2CLogSeverity::Debug
            );
            return true;
        }
    }

    // A source graph with no executable nodes is valid only when the exporter
    // retained enough native metadata to prove it is empty or comment-only.
    if (Graph.Nodes.Num() == 0)
    {
        if (IsProvenNonExecutableGraph(Graph))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("No nodes in graph %s and no proof that it is non-executable"), *Graph.Name);
        FN2CLogger::Get().LogError(OutError);
        return false;
    }

    if (!Graph.GraphPath.IsEmpty() &&
        Graph.NativeNodeCount != Graph.Nodes.Num() + Graph.SkippedNodeCount)
    {
        OutError = FString::Printf(TEXT("Native, translated, and skipped node counts disagree in graph %s"), *Graph.Name);
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
