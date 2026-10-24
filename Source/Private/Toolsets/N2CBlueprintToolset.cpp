// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Toolsets/N2CBlueprintToolset.h"

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "RigVMEditorAsset.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"

namespace
{
UBlueprint* LoadBlueprintOrLevelScriptReadOnly(const FString& AssetPath)
{
    if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath))
    {
        return Blueprint;
    }
    // Three inventory entries reference .umap World objects, not UBlueprint assets.
    // bDontCreate=true prevents the read-only inspection from creating an asset.
    if (UWorld* World = LoadObject<UWorld>(nullptr, *AssetPath))
    {
        if (World->PersistentLevel)
        {
            return World->PersistentLevel->GetLevelScriptBlueprint(true);
        }
    }
    return nullptr;
}

FString RigVMPinDirectionToString(const ERigVMPinDirection Direction)
{
    switch (Direction)
    {
        case ERigVMPinDirection::Input: return TEXT("Input");
        case ERigVMPinDirection::Output: return TEXT("Output");
        case ERigVMPinDirection::IO: return TEXT("IO");
        default: return TEXT("Invalid");
    }
}

FString ExportRigVMGraphModelText(const URigVMGraph* Graph)
{
    FString Text = FString::Printf(TEXT("RigVM graph %s [%s]\n"),
        *Graph->GetGraphName(), *Graph->GetPathName());
    for (const FRigVMGraphVariableDescription& Variable : Graph->GetLocalVariables())
    {
        Text += FString::Printf(TEXT("Variable %s : %s = %s\n"),
            *Variable.Name.ToString(), *Variable.CPPType, *Variable.DefaultValue);
    }
    for (const URigVMNode* Node : Graph->GetNodes())
    {
        if (!Node)
        {
            Text += TEXT("Node <null>\n");
            continue;
        }
        Text += FString::Printf(TEXT("Node %s [%s] %s\n"),
            *Node->GetNodePath(), *Node->GetClass()->GetPathName(), *Node->GetNodeTitle());
        for (const URigVMPin* Pin : Node->GetAllPinsRecursively())
        {
            if (Pin)
            {
                Text += FString::Printf(TEXT("  Pin %s %s %s = %s\n"),
                    *Pin->GetPinPath(), *RigVMPinDirectionToString(Pin->GetDirection()),
                    *Pin->GetCPPType(), *Pin->GetDefaultValue());
            }
        }
    }
    for (const URigVMLink* Link : Graph->GetLinks())
    {
        Text += Link
            ? FString::Printf(TEXT("Link %s -> %s\n"), *Link->GetSourcePinPath(), *Link->GetTargetPinPath())
            : TEXT("Link <null>\n");
    }
    return Text;
}
}


FN2CBlueprintExportResult UN2CBlueprintToolset::ExportBlueprintJson(const FString& BlueprintAssetPath)
{
    FN2CBlueprintExportResult Result;
    const FString AssetObjectPath = BlueprintAssetPath.TrimStartAndEnd();
    Result.AssetObjectPath = AssetObjectPath;
    auto Fail = [&Result](const FString& Message)
    {
        Result.Error = Message;
        Result.Errors.Add(Message);
        return Result;
    };

    if (AssetObjectPath.IsEmpty())
    {
        return Fail(TEXT("BlueprintAssetPath must be a non-empty Blueprint object path."));
    }

    if (!IsInGameThread())
    {
        return Fail(TEXT("Blueprint export must run on the Unreal game thread."));
    }

    UBlueprint* Blueprint = LoadBlueprintOrLevelScriptReadOnly(AssetObjectPath);
    if (!Blueprint)
    {
        return Fail(FString::Printf(
            TEXT("Could not load Blueprint at '%s'. Use its object path, such as /Game/Blueprints/BP_Character.BP_Character."),
            *AssetObjectPath));
    }

    FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
    if (!Translator.GenerateFromBlueprint(Blueprint, true))
    {
        return Fail(FString::Printf(TEXT("NodeToCode could not translate Blueprint '%s'."), *AssetObjectPath));
    }

    const FN2CBlueprint& BlueprintData = Translator.GetN2CBlueprint();
    for (const FN2CGraph& Graph : BlueprintData.Graphs)
    {
        if (Graph.SkippedNodeCount > 0)
        {
            bool bOnlyCommentsSkipped = !Graph.SkippedNodeClasses.IsEmpty();
            for (const FString& NodeClass : Graph.SkippedNodeClasses)
            {
                bOnlyCommentsSkipped &= NodeClass.EndsWith(
                    TEXT("EdGraphNode_Comment"), ESearchCase::CaseSensitive);
            }

            const FString GraphIdentity = Graph.GraphPath.IsEmpty() ? Graph.Name : Graph.GraphPath;
            if (bOnlyCommentsSkipped)
            {
                Result.Warnings.Add(FString::Printf(
                    TEXT("Graph '%s' has %d comment node(s) outside the normalized node list; use GraphText for their native text."),
                    *GraphIdentity, Graph.SkippedNodeCount));
            }
            else
            {
                Result.Errors.Add(FString::Printf(
                    TEXT("Graph '%s' has %d untranslated native node(s): %s."),
                    *GraphIdentity,
                    Graph.SkippedNodeCount,
                    *FString::Join(Graph.SkippedNodeClasses, TEXT(", "))));
            }
        }

        for (const FString& GraphError : Graph.ConnectionErrors)
        {
            Result.Errors.Add(FString::Printf(TEXT("Graph '%s': %s"), *Graph.Name, *GraphError));
        }
        if (Graph.UnresolvedConnectionCount > 0 && Graph.ConnectionErrors.Num() == 0)
        {
            Result.Errors.Add(FString::Printf(
                TEXT("Graph '%s' contains %d unresolved pin links."),
                *Graph.Name, Graph.UnresolvedConnectionCount));
        }
    }

    if (Result.Errors.Num() > 0)
    {
        Result.Error = FString::Join(Result.Errors, TEXT("\n"));
        return Result;
    }

    if (!BlueprintData.IsValid())
    {
        return Fail(FString::Printf(TEXT("NodeToCode produced invalid data for Blueprint '%s'."), *AssetObjectPath));
    }

    Result.BlueprintJson = FN2CSerializer::ToJson(BlueprintData);
    if (Result.BlueprintJson.IsEmpty())
    {
        return Fail(FString::Printf(TEXT("NodeToCode could not serialize Blueprint '%s' to JSON."), *AssetObjectPath));
    }

    Result.bSuccess = true;
    return Result;
}

FN2CBlueprintGraphTextResult UN2CBlueprintToolset::ExportBlueprintGraphText(
    const FString& BlueprintAssetPath, const FString& GraphName, const FString& GraphPath)
{
    FN2CBlueprintGraphTextResult Result;
    if (!IsInGameThread())
    {
        Result.Error = TEXT("Blueprint graph export must run on the Unreal game thread.");
        return Result;
    }

    const FString AssetPath = BlueprintAssetPath.TrimStartAndEnd();
    const FString RequestedName = GraphName.TrimStartAndEnd();
    const FString RequestedPath = GraphPath.TrimStartAndEnd();
    if (AssetPath.IsEmpty() || (RequestedName.IsEmpty() && RequestedPath.IsEmpty()))
    {
        Result.Error = TEXT("BlueprintAssetPath and a graph name or path are required.");
        return Result;
    }

    UBlueprint* Blueprint = LoadBlueprintOrLevelScriptReadOnly(AssetPath);
    if (!Blueprint)
    {
        Result.Error = FString::Printf(TEXT("Unable to load Blueprint '%s'."), *AssetPath);
        return Result;
    }

    IRigVMEditorAssetInterface* RigVMAsset = Cast<IRigVMEditorAssetInterface>(Blueprint);
    TArray<UEdGraph*> Matches;
    TArray<URigVMGraph*> RigVMMatches;
    TSet<UEdGraph*> SeenGraphs;
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph || SeenGraphs.Contains(Graph) || (RigVMAsset && RigVMAsset->GetModel(Graph)))
        {
            continue;
        }
        SeenGraphs.Add(Graph);
        const bool bMatches = RequestedPath.IsEmpty()
            ? Graph->GetName() == RequestedName
            : Graph->GetPathName() == RequestedPath;
        if (bMatches && (RequestedName.IsEmpty() || Graph->GetName() == RequestedName))
        {
            Matches.Add(Graph);
        }
    }
    if (RigVMAsset)
    {
        for (URigVMGraph* Model : RigVMAsset->GetAllModels())
        {
            if (!Model)
            {
                continue;
            }
            const FString ModelName = Model->GetGraphName().IsEmpty() ? Model->GetName() : Model->GetGraphName();
            const bool bMatches = RequestedPath.IsEmpty()
                ? ModelName == RequestedName
                : Model->GetPathName() == RequestedPath;
            if (bMatches && (RequestedName.IsEmpty() || ModelName == RequestedName))
            {
                RigVMMatches.Add(Model);
            }
        }
    }

    if (Matches.IsEmpty() && RigVMMatches.IsEmpty())
    {
        Result.Error = FString::Printf(
            TEXT("Graph '%s' at path '%s' was not found in Blueprint '%s'."),
            *RequestedName, *RequestedPath, *AssetPath);
        return Result;
    }
    if (Matches.Num() + RigVMMatches.Num() > 1)
    {
        TArray<FString> CandidatePaths;
        for (const UEdGraph* Graph : Matches)
        {
            CandidatePaths.Add(Graph->GetPathName());
        }
        for (const URigVMGraph* Model : RigVMMatches)
        {
            CandidatePaths.Add(Model->GetPathName());
        }
        Result.Error = FString::Printf(
            TEXT("Graph name '%s' is ambiguous in Blueprint '%s'; pass graphPath from NodeToCode JSON. Candidates: %s"),
            *RequestedName, *AssetPath, *FString::Join(CandidatePaths, TEXT(", ")));
        return Result;
    }

    if (!RigVMMatches.IsEmpty())
    {
        const URigVMGraph* Model = RigVMMatches[0];
        Result.GraphName = Model->GetGraphName().IsEmpty() ? Model->GetName() : Model->GetGraphName();
        Result.GraphGuid = FGuid::NewDeterministicGuid(Model->GetPathName()).ToString();
        Result.GraphPath = Model->GetPathName();
        Result.NativeNodeCount = Model->GetNodes().Num();
        Result.LinkIdentity = TEXT("rigvm_pin_path");
        TSet<FString> SeenLinks;
        for (const URigVMNode* Node : Model->GetNodes())
        {
            if (!Node)
            {
                continue;
            }
            Result.NativePinCount += Node->GetAllPinsRecursively().Num();
        }
        for (const URigVMLink* Link : Model->GetLinks())
        {
            if (!Link || !Link->GetSourcePin() || !Link->GetTargetPin())
            {
                Result.ConnectionErrors.Add(TEXT("RigVM link has a missing endpoint."));
                continue;
            }
            const FString Edge = Link->GetSourcePin()->GetPinPath() + TEXT(" -> ") +
                Link->GetTargetPin()->GetPinPath();
            if (!SeenLinks.Contains(Edge))
            {
                SeenLinks.Add(Edge);
                Result.DirectLinks.Add(Edge);
            }
        }
        Result.NativeLinkCount = Result.DirectLinks.Num();
        Result.GraphText = ExportRigVMGraphModelText(Model);
        Result.bSuccess = true;
        return Result;
    }

    UEdGraph* Graph = Matches[0];
    Result.GraphName = Graph->GetName();
    Result.GraphGuid = Graph->GraphGuid.IsValid() ? Graph->GraphGuid.ToString() : FString();
    Result.GraphPath = Graph->GetPathName();
    Result.NativeNodeCount = Graph->Nodes.Num();
    Result.LinkIdentity = TEXT("node_pin_guid");
    TSet<FString> SeenDirectLinks;
    auto Endpoint = [](const UEdGraphNode* Node, const UEdGraphPin* Pin)
    {
        const FString NodeGuid = Node && Node->NodeGuid.IsValid()
            ? Node->NodeGuid.ToString() : (Node ? Node->GetPathName() : TEXT("<null-node>"));
        const FString PinGuid = Pin && Pin->PinId.IsValid()
            ? Pin->PinId.ToString() : (Pin ? Pin->GetName() : TEXT("<null-pin>"));
        return NodeGuid + TEXT(":") + PinGuid;
    };
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            Result.ConnectionErrors.Add(TEXT("UEdGraph contains a null node."));
            continue;
        }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                Result.ConnectionErrors.Add(FString::Printf(TEXT("Node '%s' contains a null pin."), *Node->GetName()));
                continue;
            }
            ++Result.NativePinCount;
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin || !LinkedPin->GetOwningNode())
                {
                    Result.ConnectionErrors.Add(FString::Printf(
                        TEXT("Pin '%s' on node '%s' has an invalid linked endpoint."),
                        *Pin->GetName(), *Node->GetName()));
                    continue;
                }

                UEdGraphPin* SourcePin = Pin;
                UEdGraphPin* TargetPin = LinkedPin;
                UEdGraphNode* SourceNode = Node;
                UEdGraphNode* TargetNode = LinkedPin->GetOwningNode();
                if (TargetPin->Direction == EGPD_Output && SourcePin->Direction != EGPD_Output)
                {
                    Swap(SourcePin, TargetPin);
                    Swap(SourceNode, TargetNode);
                }

                if (SourcePin->Direction != EGPD_Output || TargetPin->Direction != EGPD_Input)
                {
                    Result.ConnectionErrors.Add(FString::Printf(
                        TEXT("Linked pins have invalid directions in graph '%s'."), *Graph->GetName()));
                    const FString A = Endpoint(SourceNode, SourcePin);
                    const FString B = Endpoint(TargetNode, TargetPin);
                    if (A.Compare(B, ESearchCase::CaseSensitive) > 0)
                    {
                        Swap(SourceNode, TargetNode);
                        Swap(SourcePin, TargetPin);
                    }
                }

                const FString Edge = Endpoint(SourceNode, SourcePin) + TEXT(" -> ") +
                    Endpoint(TargetNode, TargetPin);
                if (!SeenDirectLinks.Contains(Edge))
                {
                    SeenDirectLinks.Add(Edge);
                    Result.DirectLinks.Add(Edge);
                }
            }
        }
    }
    Result.NativeLinkCount = Result.DirectLinks.Num();
    if (Graph->Nodes.IsEmpty())
    {
        Result.bSuccess = true;
        return Result;
    }

    TSet<UObject*> Nodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node)
        {
            Nodes.Add(Node);
        }
    }

    FEdGraphUtilities::ExportNodesToText(Nodes, Result.GraphText);
    Result.bSuccess = !Result.GraphText.IsEmpty();
    if (!Result.bSuccess)
    {
        Result.Error = TEXT("UnrealEd returned empty graph text for a graph containing native nodes.");
    }
    return Result;
}
