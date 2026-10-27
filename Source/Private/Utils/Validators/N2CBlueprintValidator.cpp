// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Validators/N2CBlueprintValidator.h"
#include "Models/N2CJsonContract.h"

namespace
{
    void ValidatorError(TArray<FN2CDiagnostic>& Out, const TCHAR* Code, const FString& Path, const FString& Message)
    {
        N2CDiagnostics::Add(Out, EN2CDiagnosticSeverity::Error, Code, Path, Message);
    }

    void ValidatorWarning(TArray<FN2CDiagnostic>& Out, const TCHAR* Code, const FString& Path, const FString& Message)
    {
        N2CDiagnostics::Add(Out, EN2CDiagnosticSeverity::Warning, Code, Path, Message);
    }

    FString ValidatorPath(const FString& Path, const TCHAR* Field, const int32 Index)
    {
        return FString::Printf(TEXT("%s%s%s[%d]"), *Path, Path.IsEmpty() ? TEXT("") : TEXT("."), Field, Index);
    }

    /** OutError from the first error, for the bool overloads */
    bool ValidatorFinish(const bool bValid, const TArray<FN2CDiagnostic>& Diagnostics, FString& OutError)
    {
        if (!bValid)
        {
            OutError = N2CDiagnostics::FirstError(Diagnostics);
        }
        return bValid;
    }
}

bool FN2CBlueprintValidator::Validate(const FN2CBlueprint& Blueprint, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    return ValidatorFinish(Validate(Blueprint, Diagnostics), Diagnostics, OutError);
}

bool FN2CBlueprintValidator::Validate(const FN2CBlueprint& Blueprint, TArray<FN2CDiagnostic>& OutDiagnostics)
{
    if (!ValidateRequired(Blueprint, OutDiagnostics))
    {
        return false;
    }

    // At least one graph has nodes
    if (!Blueprint.Graphs.ContainsByPredicate([](const FN2CGraph& Graph) { return Graph.Nodes.Num() > 0; }))
    {
        ValidatorError(OutDiagnostics, N2CJsonContract::Diagnostic::NoNodes, TEXT("graphs"), TEXT("No nodes found in any graph"));
        return false;
    }

    // Every graph, struct and enum is checked, so one call reports every finding
    bool bValid = true;
    for (int32 Index = 0; Index < Blueprint.Graphs.Num(); ++Index)
    {
        bValid &= ValidateGraph(Blueprint.Graphs[Index], ValidatorPath(FString(), TEXT("graphs"), Index), OutDiagnostics);
    }
    for (int32 Index = 0; Index < Blueprint.Structs.Num(); ++Index)
    {
        bValid &= ValidateStruct(Blueprint.Structs[Index], ValidatorPath(FString(), TEXT("structs"), Index), OutDiagnostics);
    }
    for (int32 Index = 0; Index < Blueprint.Enums.Num(); ++Index)
    {
        bValid &= ValidateEnum(Blueprint.Enums[Index], ValidatorPath(FString(), TEXT("enums"), Index), OutDiagnostics);
    }
    return bValid;
}

bool FN2CBlueprintValidator::ValidateRequired(const FN2CBlueprint& Blueprint, TArray<FN2CDiagnostic>& Out)
{
    // Check version: a version this build reads (1.x and 2.x; FromJson maps 1.x to N2CJsonContract::Version)
    if (Blueprint.Version.Value.IsEmpty() || !N2CJsonContract::IsReadable(Blueprint.Version.Value))
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::InvalidVersion, N2CJsonContract::VersionField,
            FString::Printf(TEXT("Invalid or missing version '%s'"), *Blueprint.Version.Value));
        return false;
    }

    if (Blueprint.Metadata.Name.IsEmpty())
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, TEXT("metadata.name"), TEXT("Missing Blueprint name"));
        return false;
    }

    // A blueprint_type this build does not know (a later 2.x document, card 0445) is accepted with a warning
    if (Blueprint.Metadata.BlueprintType == EN2CBlueprintType::Unknown)
    {
        ValidatorWarning(Out, N2CJsonContract::Diagnostic::UnknownKind, TEXT("metadata.blueprint_type"),
            FString::Printf(TEXT("Blueprint %s has a blueprint_type this build does not know: '%s'"),
                *Blueprint.Metadata.Name, *Blueprint.Metadata.UnknownBlueprintType));
    }

    if (Blueprint.Metadata.BlueprintClass.IsEmpty())
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, TEXT("metadata.blueprint_class"), TEXT("Missing Blueprint class"));
        return false;
    }

    if (Blueprint.Graphs.Num() == 0)
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::NoNodes, TEXT("graphs"), TEXT("No graphs found"));
        return false;
    }
    return true;
}

bool FN2CBlueprintValidator::ValidateGraph(const FN2CGraph& Graph, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    return ValidatorFinish(ValidateGraph(Graph, FString(), Diagnostics), Diagnostics, OutError);
}

bool FN2CBlueprintValidator::ValidateGraph(const FN2CGraph& Graph, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    if (Graph.Name.IsEmpty())
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, Path, TEXT("Empty graph name"));
        return false;
    }

    // A graph_type this build does not know (a later 2.x document, card 0445) is accepted with a warning
    if (Graph.GraphType == EN2CGraphType::Unknown)
    {
        ValidatorWarning(Out, N2CJsonContract::Diagnostic::UnknownKind, Path + TEXT(".graph_type"),
            FString::Printf(TEXT("Graph %s has a graph_type this build does not know: '%s'"), *Graph.Name, *Graph.UnknownGraphType));
    }

    if (Graph.Nodes.Num() == 0)
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::NoNodes, Path, FString::Printf(TEXT("No nodes in graph %s"), *Graph.Name));
        return false;
    }

    // Every node, and no node ID twice
    bool bValid = true;
    TSet<FString> NodeIds;
    for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
    {
        const FN2CNodeDefinition& Node = Graph.Nodes[Index];
        const FString NodePath = ValidatorPath(Path, TEXT("nodes"), Index);
        if (!NodeValidator.Validate(Node, NodePath, Out))
        {
            bValid = false;
            continue;
        }
        if (NodeIds.Contains(Node.ID))
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::DuplicateId, NodePath, FString::Printf(TEXT("Duplicate node ID %s in graph %s"), *Node.ID, *Graph.Name));
            bValid = false;
        }
        NodeIds.Add(Node.ID);
    }

    bValid &= ValidateFlowReferences(Graph, Path, Out);

    // A comment box lists nodes of its own graph (card 0235)
    for (int32 Index = 0; Index < Graph.Comments.Num(); ++Index)
    {
        for (const FString& NodeID : Graph.Comments[Index].NodeIDs)
        {
            if (!NodeIds.Contains(NodeID))
            {
                ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingComment, ValidatorPath(Path, N2CJsonContract::CommentsField, Index),
                    FString::Printf(TEXT("Comment box in graph %s lists node %s, which is not in the graph"), *Graph.Name, *NodeID));
                bValid = false;
            }
        }
    }

    if (bValid)
    {
        N2C_LOG(Debug, TEXT("Graph %s validation successful: %d nodes, %d execution flows, %d data flows"),
            *Graph.Name, Graph.Nodes.Num(), Graph.Flows.Execution.Num(), Graph.Flows.Data.Num());
    }
    return bValid;
}

bool FN2CBlueprintValidator::ValidateFlowReferences(const FN2CGraph& Graph, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    return ValidatorFinish(ValidateFlowReferences(Graph, FString(), Diagnostics), Diagnostics, OutError);
}

bool FN2CBlueprintValidator::ValidateFlowReferences(const FN2CGraph& Graph, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    // Build lookup maps for validation: node IDs, and each node's exec input and exec output pin IDs
    TSet<FString> NodeIds;
    TMap<FString, TSet<FString>> NodeExecOutputs;
    TMap<FString, TSet<FString>> NodeExecInputs;
    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        NodeIds.Add(Node.ID);
        TSet<FString>& ExecInputs = NodeExecInputs.FindOrAdd(Node.ID);
        TSet<FString>& ExecOutputs = NodeExecOutputs.FindOrAdd(Node.ID);
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

    bool bValid = true;
    const FString ExecPath = Path + (Path.IsEmpty() ? TEXT("") : TEXT(".")) + N2CJsonContract::FlowsField + TEXT(".") + N2CJsonContract::ExecutionField;
    const FString DataPath = Path + (Path.IsEmpty() ? TEXT("") : TEXT(".")) + N2CJsonContract::FlowsField + TEXT(".") + N2CJsonContract::DataField;

    // Execution flows: "N3.P5->N4.P1" from an exec output pin to an exec input pin (N2C JSON 2.0.0), or the node-only
    // form "N1->N2" ("N1->N2->N3") of 1.x documents and earlier 2.0 builds, whose nodes must exist
    for (int32 FlowIndex = 0; FlowIndex < Graph.Flows.Execution.Num(); ++FlowIndex)
    {
        const FString& ExecFlow = Graph.Flows.Execution[FlowIndex];
        const FString FlowPath = FString::Printf(TEXT("%s[%d]"), *ExecPath, FlowIndex);
        TArray<N2CJsonContract::FExecEndpoint> Endpoints;
        if (!N2CJsonContract::ParseExecFlow(ExecFlow, Endpoints))
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(
                TEXT("Invalid execution flow %s (expected \"N1.P2->N3.P4\" or \"N1->N2\") in graph %s"), *ExecFlow, *Graph.Name));
            bValid = false;
            continue;
        }

        const bool bPinForm = Endpoints[0].Pin.Len() > 0 || Endpoints[1].Pin.Len() > 0;
        if (bPinForm && (Endpoints.Num() != 2 || Endpoints[0].Pin.IsEmpty() || Endpoints[1].Pin.IsEmpty()))
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(
                TEXT("Execution flow %s mixes pin refs and node IDs (a pin-qualified entry is one \"N1.P2->N3.P4\" link) in graph %s"),
                *ExecFlow, *Graph.Name));
            bValid = false;
            continue;
        }

        for (int32 Index = 0; Index < Endpoints.Num(); ++Index)
        {
            const N2CJsonContract::FExecEndpoint& Endpoint = Endpoints[Index];
            if (!NodeIds.Contains(Endpoint.Node))
            {
                ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(
                    TEXT("Execution flow %s references non-existent node %s in graph %s"), *ExecFlow, *Endpoint.Node, *Graph.Name));
                bValid = false;
                break;
            }
            if (!bPinForm)
            {
                continue;
            }
            const bool bSource = Index == 0;
            const TSet<FString>& ExecPins = bSource ? NodeExecOutputs.FindChecked(Endpoint.Node) : NodeExecInputs.FindChecked(Endpoint.Node);
            if (!ExecPins.Contains(Endpoint.Pin))
            {
                ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(
                    TEXT("Execution flow %s: %s is not an exec %s pin of node %s in graph %s"), *ExecFlow, *Endpoint.Pin,
                    bSource ? TEXT("output") : TEXT("input"), *Endpoint.Node, *Graph.Name));
                bValid = false;
                break;
            }
        }
    }

    // Data flows: every key and target a "N#.P#" pin ref of a node of the graph
    for (const auto& DataFlow : Graph.Flows.Data)
    {
        const FString FlowPath = DataPath + TEXT(".") + DataFlow.Key;
        TArray<FString> SourceParts;
        DataFlow.Key.ParseIntoArray(SourceParts, TEXT("."));
        if (SourceParts.Num() != 2 || !NodeIds.Contains(SourceParts[0]))
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(TEXT("Invalid source pin format %s in graph %s"), *DataFlow.Key, *Graph.Name));
            bValid = false;
            continue;
        }
        if (DataFlow.Value.Targets.Num() == 0)
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(TEXT("Data flow %s has no target in graph %s"), *DataFlow.Key, *Graph.Name));
            bValid = false;
            continue;
        }
        for (const FString& Target : DataFlow.Value.Targets)
        {
            TArray<FString> TargetParts;
            Target.ParseIntoArray(TargetParts, TEXT("."));
            if (TargetParts.Num() != 2 || !NodeIds.Contains(TargetParts[0]))
            {
                ValidatorError(Out, N2CJsonContract::Diagnostic::DanglingFlow, FlowPath, FString::Printf(TEXT("Invalid target pin format %s in graph %s"), *Target, *Graph.Name));
                bValid = false;
            }
        }
    }

    // A connected data input with no data flow into it lost its source (a lossy edge, as before card 0135, or a
    // hand-edited document): readers would have to guess the value (issue #13). A warning, so such a document still reads.
    TSet<FString> DataTargets;
    for (const auto& DataFlow : Graph.Flows.Data)
    {
        DataTargets.Append(DataFlow.Value.Targets);
    }
    TArray<FString> Unfed;
    for (const FN2CNodeDefinition& Node : Graph.Nodes)
    {
        for (const FN2CPinDefinition& Pin : Node.InputPins)
        {
            const FString PinRef = Node.ID + N2CJsonContract::PinRefSeparator + Pin.ID;
            if (Pin.Type != EN2CPinType::Exec && Pin.bConnected && !DataTargets.Contains(PinRef))
            {
                Unfed.Add(PinRef);
            }
        }
    }
    if (Unfed.Num() > 0)
    {
        ValidatorWarning(Out, N2CJsonContract::Diagnostic::UnfedInput, DataPath, FString::Printf(
            TEXT("Graph %s: connected data input(s) %s have no data flow into them; their source is missing"),
            *Graph.Name, *FString::Join(Unfed, TEXT(", "))));
    }
    return bValid;
}

bool FN2CBlueprintValidator::ValidateStruct(const FN2CStruct& Struct, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    return ValidatorFinish(ValidateStruct(Struct, FString(), Diagnostics), Diagnostics, OutError);
}

bool FN2CBlueprintValidator::ValidateStruct(const FN2CStruct& Struct, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    if (Struct.Name.IsEmpty())
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, Path, TEXT("Missing name"));
        return false;
    }

    bool bValid = true;
    for (int32 Index = 0; Index < Struct.Members.Num(); ++Index)
    {
        const FN2CStructMember& Member = Struct.Members[Index];
        const FString MemberPath = ValidatorPath(Path, TEXT("members"), Index);
        if (Member.Name.IsEmpty())
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, MemberPath, FString::Printf(TEXT("Member at index %d has no name"), Index));
            bValid = false;
            continue;
        }

        // A member of a struct, enum, object or class type without its type name: the struct is kept (it was dropped
        // before card 0240), so a reader sees the member and the warning
        if ((Member.Type == EN2CStructMemberType::Struct ||
             Member.Type == EN2CStructMemberType::Enum ||
             Member.Type == EN2CStructMemberType::Object ||
             Member.Type == EN2CStructMemberType::Class) &&
            Member.TypeName.IsEmpty())
        {
            ValidatorWarning(Out, N2CJsonContract::Diagnostic::MemberType, MemberPath,
                FString::Printf(TEXT("Struct %s: Member %s requires a type name"), *Struct.Name, *Member.Name));
        }
    }
    return bValid;
}

bool FN2CBlueprintValidator::ValidateEnum(const FN2CEnum& Enum, FString& OutError)
{
    TArray<FN2CDiagnostic> Diagnostics;
    return ValidatorFinish(ValidateEnum(Enum, FString(), Diagnostics), Diagnostics, OutError);
}

bool FN2CBlueprintValidator::ValidateEnum(const FN2CEnum& Enum, const FString& Path, TArray<FN2CDiagnostic>& Out)
{
    if (Enum.Name.IsEmpty())
    {
        ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, Path, TEXT("Missing name"));
        return false;
    }

    bool bValid = true;
    for (int32 Index = 0; Index < Enum.Values.Num(); ++Index)
    {
        if (Enum.Values[Index].Name.IsEmpty())
        {
            ValidatorError(Out, N2CJsonContract::Diagnostic::MissingName, ValidatorPath(Path, TEXT("values"), Index), FString::Printf(TEXT("Value at index %d has no name"), Index));
            bValid = false;
        }
    }
    return bValid;
}
