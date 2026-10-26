// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Toolsets/N2CBlueprintToolset.h"
#include "Models/N2CLogging.h"

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Engine/World.h"
#include "Engine/MemberReference.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "RigVMEditorAsset.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMLink.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
UBlueprint* LoadBlueprintOrLevelScriptReadOnly(const FString& AssetPath)
{
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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

UEdGraph* FindBlueprintFunctionGraph(UBlueprint* Blueprint, const FName GraphName)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!IsValid(Blueprint) || GraphName.IsNone())
    {
        return nullptr;
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (IsValid(Graph) && Graph->GetFName() == GraphName)
        {
            return Graph;
        }
    }
    return nullptr;
}

FString MakeGraphConnectionKey(
    const FGuid& SourceNodeGuid, const FName SourcePinName,
    const FGuid& TargetNodeGuid, const FName TargetPinName)
{
    N2C_LOG_FUNCTION_ENTRY();
    return FString::Printf(
        TEXT("%s:%s->%s:%s"),
        *SourceNodeGuid.ToString(), *SourcePinName.ToString(),
        *TargetNodeGuid.ToString(), *TargetPinName.ToString());
}

struct FGraphPinNameOverride
{
    FName OldName;
    FName NewName;
};

TSet<FString> CaptureGraphConnectionKeys(
    const UEdGraph* Graph,
    const TMap<const UEdGraphNode*, FGraphPinNameOverride>* PinNameOverrides = nullptr)
{
    N2C_LOG_FUNCTION_ENTRY();
    TSet<FString> Connections;
    if (!IsValid(Graph))
    {
        return Connections;
    }

    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        if (!IsValid(Node))
        {
            continue;
        }
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output)
            {
                continue;
            }
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (!Linked || Linked->Direction != EGPD_Input ||
                    !IsValid(Linked->GetOwningNode()))
                {
                    continue;
                }
                FName SourcePinName = Pin->PinName;
                FName TargetPinName = Linked->PinName;
                if (PinNameOverrides)
                {
                    if (const FGraphPinNameOverride* Override = PinNameOverrides->Find(Node);
                        Override && SourcePinName == Override->OldName)
                    {
                        SourcePinName = Override->NewName;
                    }
                    if (const FGraphPinNameOverride* Override =
                        PinNameOverrides->Find(Linked->GetOwningNode());
                        Override && TargetPinName == Override->OldName)
                    {
                        TargetPinName = Override->NewName;
                    }
                }
                Connections.Add(MakeGraphConnectionKey(
                    Node->NodeGuid, SourcePinName,
                    Linked->GetOwningNode()->NodeGuid, TargetPinName));
            }
        }
    }
    return Connections;
}

bool AreConnectionSetsEqual(const TSet<FString>& Left, const TSet<FString>& Right)
{
    N2C_LOG_FUNCTION_ENTRY();
    if (Left.Num() != Right.Num())
    {
        return false;
    }
    for (const FString& Connection : Left)
    {
        if (!Right.Contains(Connection))
        {
            return false;
        }
    }
    return true;
}

bool CompileAndSaveBlueprint(
    UBlueprint* Blueprint, FString& Error, const TFunction<void()>& AfterCompile = {})
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!IsValid(Blueprint) || !IsValid(Blueprint->GetOutermost()))
    {
        Error = TEXT("A valid Blueprint package is required to compile and save.");
        return false;
    }

    FCompilerResultsLog CompileResults;
    FKismetEditorUtilities::CompileBlueprint(
        Blueprint, EBlueprintCompileOptions::None, &CompileResults);
    if (CompileResults.NumErrors > 0 || Blueprint->Status == BS_Error)
    {
        Error = FString::Printf(
            TEXT("Blueprint compilation failed with %d error(s)."),
            CompileResults.NumErrors);
        return false;
    }

    if (AfterCompile)
    {
        AfterCompile();
    }

    UPackage* Package = Blueprint->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    if (Filename.IsEmpty())
    {
        Error = TEXT("Unreal could not resolve the Blueprint package filename.");
        return false;
    }

    Package->SetDirtyFlag(true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.bSlowTask = false;
    if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
    {
        Error = FString::Printf(
            TEXT("Unreal could not save Blueprint package '%s'."), *Filename);
        return false;
    }
    return true;
}
}


FN2CBlueprintExportResult UN2CBlueprintToolset::ExportBlueprintJson(const FString& BlueprintAssetPath)
{
    N2C_LOG_FUNCTION_ENTRY();
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
    N2C_LOG_FUNCTION_ENTRY();
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

FN2CBlueprintMemberRemovalResult
UN2CBlueprintToolset::RemoveUnusedBlueprintMemberVariable(
    const FString& BlueprintAssetPath, const FString& VariableName,
    const bool bExternalReferencesReviewed,
    const bool bDefaultDataConfirmedEmpty)
{
    N2C_LOG_FUNCTION_ENTRY();
    const FString AssetPath = BlueprintAssetPath.TrimStartAndEnd();
    if (!IsInGameThread())
    {
        FN2CBlueprintMemberRemovalResult Result;
        Result.AssetObjectPath = AssetPath;
        Result.VariableName = VariableName;
        Result.Error = TEXT("Blueprint variable removal must run on the Unreal game thread.");
        return Result;
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Blueprint)
    {
        FN2CBlueprintMemberRemovalResult Result;
        Result.AssetObjectPath = AssetPath;
        Result.VariableName = VariableName;
        Result.Error = FString::Printf(TEXT("Could not load Blueprint at '%s'."), *AssetPath);
        return Result;
    }

    return RemoveUnusedBlueprintMemberVariableFromBlueprint(
        Blueprint, FName(*VariableName), bExternalReferencesReviewed,
        bDefaultDataConfirmedEmpty);
}

FN2CBlueprintMemberRemovalResult
UN2CBlueprintToolset::RemoveUnusedBlueprintMemberVariableFromBlueprint(
    UBlueprint* Blueprint, const FName VariableName,
    const bool bExternalReferencesReviewed,
    const bool bDefaultDataConfirmedEmpty)
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CBlueprintMemberRemovalResult Result;
    Result.AssetObjectPath = GetPathNameSafe(Blueprint);
    Result.VariableName = VariableName.ToString();
    Result.bExternalReferencesReviewed = bExternalReferencesReviewed;

    auto Fail = [&Result](const TCHAR* Message)
    {
        Result.Error = Message;
        return Result;
    };

    if (!IsValid(Blueprint) || VariableName.IsNone())
    {
        return Fail(TEXT("A valid Blueprint and local member-variable name are required."));
    }
    if (!bExternalReferencesReviewed || !bDefaultDataConfirmedEmpty)
    {
        return Fail(TEXT("Review external Blueprint callers and confirm the variable has no stored default data before removal."));
    }
    const int32 VariableIndex =
        FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName);
    if (VariableIndex == INDEX_NONE)
    {
        return Fail(TEXT("The named member variable is not declared in this Blueprint."));
    }

    const FBPVariableDescription& Variable = Blueprint->NewVariables[VariableIndex];
    if (!Variable.DefaultValue.IsEmpty())
    {
        return Fail(TEXT("The Blueprint variable has a non-empty default value; migrate it before removal."));
    }
    Result.bDefaultValueEmpty = true;

    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
    {
        if (!IsValid(Graph))
        {
            continue;
        }
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
            if (!IsValid(VariableNode))
            {
                continue;
            }

            const FMemberReference& Reference = VariableNode->VariableReference;
            const bool bMatchesGuid = Reference.GetMemberGuid().IsValid() &&
                Reference.GetMemberGuid() == Variable.VarGuid;
            const bool bMatchesSelfName =
                Reference.GetMemberName() == VariableName &&
                Reference.IsSelfContext() && !Reference.IsLocalScope();
            const bool bMatchesClassName = IsValid(Blueprint->GeneratedClass) &&
                Reference.GetMemberName() == VariableName &&
                Reference.GetMemberParentClass() == Blueprint->GeneratedClass &&
                !Reference.IsLocalScope();
            if (bMatchesGuid || bMatchesSelfName || bMatchesClassName)
            {
                ++Result.GraphReferenceCount;
            }
        }
    }
    if (Result.GraphReferenceCount > 0)
    {
        Result.Error = FString::Printf(
            TEXT("The member variable is still referenced by %d Blueprint graph node(s)."),
            Result.GraphReferenceCount);
        return Result;
    }

    Blueprint->Modify();
    FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VariableName);
    if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE)
    {
        return Fail(TEXT("Unreal Editor did not remove the member variable."));
    }

    Result.bSuccess = true;
    Result.bModifiedInMemory = true;
    return Result;
}

FN2CBlueprintMutationResult
UN2CBlueprintToolset::MigrateBlueprintBooleanMemberToNativeParent(
    const FString& BlueprintAssetPath, const FString& VariableName,
    const FString& NativePropertyName,
    const bool bExternalReferencesReviewed)
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CBlueprintMutationResult Result;
    Result.AssetObjectPath = BlueprintAssetPath.TrimStartAndEnd();
    auto Fail = [&Result](const FString& Message)
    {
        Result.Error = Message;
        return Result;
    };

    if (!IsInGameThread())
    {
        return Fail(TEXT("Blueprint variable migration must run on the Unreal game thread."));
    }
    if (!bExternalReferencesReviewed)
    {
        return Fail(TEXT("Review external Blueprint callers before migrating the member variable."));
    }

    const FName MemberName(*VariableName.TrimStartAndEnd());
    const FName NativeMemberName(*NativePropertyName.TrimStartAndEnd());
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Result.AssetObjectPath);
    if (!IsValid(Blueprint) || MemberName.IsNone() || NativeMemberName.IsNone() ||
        !IsValid(Blueprint->GeneratedClass) || !IsValid(Blueprint->ParentClass))
    {
        return Fail(TEXT("A valid Blueprint, member name, and parent class are required."));
    }

    FBoolProperty* ParentProperty = FindFProperty<FBoolProperty>(
        Blueprint->ParentClass, NativeMemberName);
    if (!ParentProperty || !IsValid(ParentProperty->GetOwnerClass()) ||
        ParentProperty->GetOwnerClass() == Blueprint->GeneratedClass)
    {
        return Fail(TEXT("The Blueprint parent does not expose a native Boolean property with this name."));
    }

    int32 VariableIndex = FBlueprintEditorUtils::FindNewVariableIndex(
        Blueprint, MemberName);
    TArray<UEdGraph*> VariableGraphs;
    Blueprint->GetAllGraphs(VariableGraphs);
    if (VariableIndex == INDEX_NONE)
    {
        for (const UEdGraph* Graph : VariableGraphs)
        {
            if (!IsValid(Graph))
            {
                continue;
            }
            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
                if (!IsValid(VariableNode))
                {
                    continue;
                }

                const FMemberReference& Reference = VariableNode->VariableReference;
                const FString ReferencedName = Reference.GetMemberName().ToString();
                const FString LocalNamePrefix = MemberName.ToString() + TEXT("_");
                const bool bNameMatches = Reference.GetMemberName() == MemberName ||
                    Reference.GetMemberName() == NativeMemberName ||
                    ReferencedName.StartsWith(LocalNamePrefix, ESearchCase::CaseSensitive);
                if (!Reference.IsSelfContext() || !bNameMatches ||
                    !Reference.GetMemberGuid().IsValid())
                {
                    continue;
                }

                const FGuid ReferencedGuid = Reference.GetMemberGuid();
                VariableIndex = Blueprint->NewVariables.IndexOfByPredicate(
                    [&ReferencedGuid](const FBPVariableDescription& Variable)
                    {
                        return Variable.VarGuid == ReferencedGuid;
                    });
                if (VariableIndex != INDEX_NONE)
                {
                    break;
                }
            }
            if (VariableIndex != INDEX_NONE)
            {
                break;
            }
        }
    }
    const FName LocalVariableName = VariableIndex != INDEX_NONE
        ? Blueprint->NewVariables[VariableIndex].VarName
        : MemberName;
    const FGuid LocalVariableGuid = VariableIndex != INDEX_NONE
        ? Blueprint->NewVariables[VariableIndex].VarGuid
        : FGuid();
    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FName> ReferencerPackages;
    const FName BlueprintPackageName = Blueprint->GetOutermost()->GetFName();
    AssetRegistryModule.Get().GetReferencers(BlueprintPackageName, ReferencerPackages);
    TArray<FString> ExternalReferences;
    for (const FName ReferencerPackage : ReferencerPackages)
    {
        if (ReferencerPackage == BlueprintPackageName)
        {
            continue;
        }

        TArray<FAssetData> ReferencerAssets;
        AssetRegistryModule.Get().GetAssetsByPackageName(
            ReferencerPackage, ReferencerAssets);
        for (const FAssetData& ReferencerAsset : ReferencerAssets)
        {
            UBlueprint* ReferencerBlueprint =
                Cast<UBlueprint>(ReferencerAsset.GetAsset());
            if (!IsValid(ReferencerBlueprint) ||
                !IsValid(ReferencerBlueprint->GeneratedClass))
            {
                continue;
            }

            TArray<UEdGraph*> ReferencerGraphs;
            ReferencerBlueprint->GetAllGraphs(ReferencerGraphs);
            for (const UEdGraph* Graph : ReferencerGraphs)
            {
                if (!IsValid(Graph))
                {
                    continue;
                }
                for (const UEdGraphNode* Node : Graph->Nodes)
                {
                    const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
                    if (!IsValid(VariableNode))
                    {
                        continue;
                    }

                    const FMemberReference& Reference = VariableNode->VariableReference;
                    if ((Reference.GetMemberName() == MemberName ||
                         Reference.GetMemberName() == LocalVariableName ||
                         (LocalVariableGuid.IsValid() &&
                          Reference.GetMemberGuid() == LocalVariableGuid)) &&
                        Reference.GetMemberParentClass() == Blueprint->GeneratedClass)
                    {
                        ExternalReferences.Add(FString::Printf(
                            TEXT("%s:%s:%s"), *ReferencerBlueprint->GetPathName(),
                            *Graph->GetName(), *VariableNode->GetName()));
                    }
                }
            }
        }
    }
    if (!ExternalReferences.IsEmpty())
    {
        return Fail(FString::Printf(
            TEXT("Review %d external Blueprint variable reference(s) before migration: %s"),
            ExternalReferences.Num(), *FString::Join(ExternalReferences, TEXT(", "))));
    }

    UClass* NativeOwnerClass = ParentProperty->GetOwnerClass();
    if (!IsValid(NativeOwnerClass))
    {
        return Fail(TEXT("The native parent Boolean property must resolve."));
    }

    struct FVariableNodeRebind
    {
        struct FPinLink
        {
            FName OriginalPinName;
            EEdGraphPinDirection Direction = EGPD_Input;
            UEdGraphPin* LinkedPin = nullptr;
        };

        UK2Node_Variable* Node = nullptr;
        FMemberReference OriginalReference;
        TEnumAsByte<ESelfContextInfo::Type> OriginalSelfContextInfo;
        FName OriginalValuePinName;
        TArray<FPinLink> OriginalPinLinks;
    };
    TArray<FVariableNodeRebind> Rebinds;
    TArray<UEdGraph*> Graphs;
    TMap<UEdGraph*, TSet<FString>> OriginalConnections;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (!IsValid(Graph))
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
            if (!IsValid(VariableNode))
            {
                continue;
            }

            const FMemberReference& Reference = VariableNode->VariableReference;
            const bool bMatchesLocalVariable =
                (LocalVariableGuid.IsValid() &&
                 Reference.GetMemberGuid() == LocalVariableGuid) ||
                ((Reference.GetMemberName() == MemberName ||
                  Reference.GetMemberName() == LocalVariableName) &&
                 Reference.IsSelfContext() && !Reference.IsLocalScope());
            if (!bMatchesLocalVariable)
            {
                continue;
            }

            FVariableNodeRebind Rebind;
            Rebind.Node = VariableNode;
            Rebind.OriginalReference = Reference;
            Rebind.OriginalSelfContextInfo = VariableNode->SelfContextInfo;
            for (const UEdGraphPin* Pin : VariableNode->Pins)
            {
                if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Boolean ||
                    (Pin->Direction != EGPD_Input &&
                     (Pin->Direction != EGPD_Output || Pin->PinName == TEXT("Output_Get"))))
                {
                    continue;
                }
                Rebind.OriginalValuePinName = Pin->PinName;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    Rebind.OriginalPinLinks.Add(
                        {Pin->PinName, Pin->Direction, LinkedPin});
                }
            }
            Rebinds.Add(MoveTemp(Rebind));
        }
    }

    TMap<const UEdGraphNode*, FGraphPinNameOverride> PinNameOverrides;
    for (const FVariableNodeRebind& Rebind : Rebinds)
    {
        const FName OldPinName = Rebind.OriginalValuePinName;
        if (OldPinName != NativeMemberName)
        {
            PinNameOverrides.Add(
                Rebind.Node, FGraphPinNameOverride{OldPinName, NativeMemberName});
        }
    }
    for (UEdGraph* Graph : Graphs)
    {
        if (IsValid(Graph))
        {
            OriginalConnections.Add(
                Graph, CaptureGraphConnectionKeys(Graph, &PinNameOverrides));
        }
    }

    if (VariableIndex == INDEX_NONE && Rebinds.IsEmpty())
    {
        Result.bSuccess = true;
        Result.bAlreadyImplemented = true;
        return Result;
    }

    FBoolProperty* LocalProperty = CastField<FBoolProperty>(
        Blueprint->GeneratedClass->FindPropertyByName(LocalVariableName));
    if (VariableIndex != INDEX_NONE &&
        (!LocalProperty || LocalProperty->GetOwnerClass() != Blueprint->GeneratedClass))
    {
        LocalProperty = nullptr;
        for (const FVariableNodeRebind& Rebind : Rebinds)
        {
            FBoolProperty* NodeProperty = CastField<FBoolProperty>(
                Rebind.Node->GetPropertyForVariable());
            if (NodeProperty && NodeProperty->GetOwnerClass() == Blueprint->GeneratedClass)
            {
                LocalProperty = NodeProperty;
                break;
            }
        }
    }
    if (!LocalProperty && VariableIndex == INDEX_NONE &&
        LocalVariableName == NativeMemberName)
    {
        LocalProperty = ParentProperty;
    }
    if (!LocalProperty)
    {
        return Fail(TEXT("The local Boolean default value could not be resolved."));
    }

    const bool bOldDefault = LocalProperty->GetPropertyValue_InContainer(
        Blueprint->GeneratedClass->GetDefaultObject());
    const bool bOriginalParentDefault = ParentProperty->GetPropertyValue_InContainer(
        Blueprint->GeneratedClass->GetDefaultObject());
    Blueprint->Modify();
    Result.bModifiedInMemory = true;
    ParentProperty->SetPropertyValue_InContainer(
        Blueprint->GeneratedClass->GetDefaultObject(), bOldDefault);
    auto RestoreReferences = [&]()
    {
        for (FVariableNodeRebind& Rebind : Rebinds)
        {
            if (IsValid(Rebind.Node))
            {
                Rebind.Node->VariableReference = Rebind.OriginalReference;
                Rebind.Node->SelfContextInfo = Rebind.OriginalSelfContextInfo;
                Rebind.Node->ReconstructNode();
                for (const FVariableNodeRebind::FPinLink& Link : Rebind.OriginalPinLinks)
                {
                    UEdGraphPin* OriginalPin = Rebind.Node->FindPin(
                        Link.OriginalPinName, Link.Direction);
                    if (OriginalPin && Link.LinkedPin &&
                        !OriginalPin->LinkedTo.Contains(Link.LinkedPin))
                    {
                        if (const UEdGraphSchema* Schema = Rebind.Node->GetSchema();
                            !Schema || !Schema->TryCreateConnection(OriginalPin, Link.LinkedPin))
                        {
                            OriginalPin->MakeLinkTo(Link.LinkedPin);
                        }
                    }
                }
            }
        }
        ParentProperty->SetPropertyValue_InContainer(
            Blueprint->GeneratedClass->GetDefaultObject(), bOriginalParentDefault);
    };
    for (FVariableNodeRebind& Rebind : Rebinds)
    {
        Rebind.Node->Modify();
        Rebind.Node->SetFromProperty(ParentProperty, true, NativeOwnerClass);
        Rebind.Node->ReconstructNode();
        for (const FVariableNodeRebind::FPinLink& Link : Rebind.OriginalPinLinks)
        {
            if (!Link.LinkedPin)
            {
                RestoreReferences();
                return Fail(TEXT("A linked variable pin target became invalid during rebinding."));
            }

            UEdGraphPin* NativePin = Rebind.Node->FindPin(
                NativeMemberName, Link.Direction);
            if (!NativePin)
            {
                RestoreReferences();
                return Fail(TEXT("Unreal did not create the native property pin."));
            }
            if (NativePin->LinkedTo.Contains(Link.LinkedPin))
            {
                continue;
            }

            UEdGraphPin* OrphanedPin = Rebind.Node->FindPin(
                Link.OriginalPinName, Link.Direction);
            if (OrphanedPin && OrphanedPin != NativePin &&
                OrphanedPin->LinkedTo.Contains(Link.LinkedPin))
            {
                OrphanedPin->BreakLinkTo(Link.LinkedPin);
            }
            const UEdGraphSchema* Schema = Rebind.Node->GetSchema();
            if (!Schema || !Schema->TryCreateConnection(NativePin, Link.LinkedPin))
            {
                if (OrphanedPin)
                {
                    OrphanedPin->MakeLinkTo(Link.LinkedPin);
                }
                RestoreReferences();
                return Fail(TEXT("Unreal could not preserve a variable pin connection during rebinding."));
            }
        }
        TArray<UEdGraphPin*> OrphanedPinsToRemove;
        for (UEdGraphPin* Pin : Rebind.Node->Pins)
        {
            if (Pin && Pin->PinName == Rebind.OriginalValuePinName &&
                Pin->PinName != NativeMemberName && Pin->LinkedTo.IsEmpty())
            {
                OrphanedPinsToRemove.Add(Pin);
            }
        }
        for (UEdGraphPin* Pin : OrphanedPinsToRemove)
        {
            Rebind.Node->RemovePin(Pin);
        }
        const FProperty* ResolvedProperty = Rebind.Node->VariableReference.ResolveMember<FProperty>(
            Blueprint->GeneratedClass);
        if (!Rebind.Node->VariableReference.IsSelfContext() ||
            Rebind.Node->VariableReference.GetMemberName() != NativeMemberName)
        {
            RestoreReferences();
            return Fail(TEXT("A graph variable node did not bind to the inherited native property."));
        }
        if (!ResolvedProperty || ResolvedProperty->GetOwnerClass() != NativeOwnerClass)
        {
            RestoreReferences();
            return Fail(TEXT("A rebound graph node does not resolve to the native parent property."));
        }
    }

    for (const TPair<UEdGraph*, TSet<FString>>& Entry : OriginalConnections)
    {
        if (!AreConnectionSetsEqual(
                CaptureGraphConnectionKeys(Entry.Key), Entry.Value))
        {
            RestoreReferences();
            return Fail(TEXT("Rebinding changed Blueprint graph links; no package was saved."));
        }
    }

    if (VariableIndex != INDEX_NONE)
    {
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, LocalVariableName);
        if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, LocalVariableName) != INDEX_NONE)
        {
            RestoreReferences();
            return Fail(TEXT("Unreal Editor did not remove the local member variable."));
        }
    }
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    for (const TPair<UEdGraph*, TSet<FString>>& Entry : OriginalConnections)
    {
        if (!AreConnectionSetsEqual(
                CaptureGraphConnectionKeys(Entry.Key), Entry.Value))
        {
            return Fail(TEXT("Refreshing Blueprint nodes changed graph links; the package was not saved."));
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    FString SaveError;
    const TFunction<void()> RestoreDefault = [Blueprint, ParentProperty, bOldDefault]()
    {
        if (IsValid(Blueprint) && IsValid(Blueprint->GeneratedClass))
        {
            ParentProperty->SetPropertyValue_InContainer(
                Blueprint->GeneratedClass->GetDefaultObject(), bOldDefault);
        }
    };
    if (!CompileAndSaveBlueprint(Blueprint, SaveError, RestoreDefault))
    {
        return Fail(SaveError);
    }

    Result.bSuccess = true;
    Result.bModifiedInMemory = true;
    Result.bSaved = true;
    Result.ChangeCount = Rebinds.Num() + (VariableIndex != INDEX_NONE ? 1 : 0);
    Result.Changes.Add(FString::Printf(
        TEXT("Asset Registry audit found no external Blueprint references across %d package(s)"),
        ReferencerPackages.Num()));
    Result.Changes.Add(FString::Printf(
        TEXT("Rebound %d graph variable node(s) to native %s::%s"),
        Rebinds.Num(), *NativeOwnerClass->GetName(), *NativeMemberName.ToString()));
    Result.Changes.Add(FString::Printf(
        TEXT("Migrated the child default value (%s)%s"),
        bOldDefault ? TEXT("true") : TEXT("false"),
        VariableIndex != INDEX_NONE ? TEXT(" and removed the local variable") : TEXT("")));
    return Result;
}

FN2CBlueprintMutationResult UN2CBlueprintToolset::RebindBlueprintFunctionCalls(
    const FString& BlueprintAssetPath, const FString& SourceFunctionLibraryPath,
    const TArray<FN2CBlueprintFunctionCallMapping>& Mappings,
    const bool bExternalReferencesReviewed)
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CBlueprintMutationResult Result;
    Result.AssetObjectPath = BlueprintAssetPath.TrimStartAndEnd();
    auto Fail = [&Result](const FString& Message)
    {
        Result.Error = Message;
        return Result;
    };

    if (!IsInGameThread())
    {
        return Fail(TEXT("Blueprint function rebinding must run on the Unreal game thread."));
    }
    if (!bExternalReferencesReviewed || Mappings.IsEmpty())
    {
        return Fail(TEXT("Review external Blueprint callers and provide at least one function mapping before rebinding."));
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Result.AssetObjectPath);
    UBlueprint* SourceLibrary = LoadObject<UBlueprint>(
        nullptr, *SourceFunctionLibraryPath.TrimStartAndEnd());
    if (!IsValid(Blueprint) || !IsValid(SourceLibrary) ||
        !IsValid(SourceLibrary->GeneratedClass))
    {
        return Fail(TEXT("The caller Blueprint and source function library must both load."));
    }

    struct FCallNodeRebind
    {
        struct FPinCast
        {
            FName CallPinName;
            UEdGraphPin* TargetPin = nullptr;
            UClass* CastClass = nullptr;
            UK2Node_DynamicCast* CastNode = nullptr;
        };

        UK2Node_CallFunction* Node = nullptr;
        UFunction* OriginalFunction = nullptr;
        UFunction* ReplacementFunction = nullptr;
        FString ChangeDescription;
        TArray<FPinCast> Casts;
    };
    TArray<FCallNodeRebind> Rebinds;
    TMap<UEdGraph*, TSet<FString>> OriginalConnections;
    TSet<FName> MappedFunctionNames;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);

    for (const FN2CBlueprintFunctionCallMapping& Mapping : Mappings)
    {
        const FName SourceFunctionName(*Mapping.SourceFunctionGraphName.TrimStartAndEnd());
        const FName TargetFunctionName(*Mapping.TargetFunctionName.TrimStartAndEnd());
        if (SourceFunctionName.IsNone() || TargetFunctionName.IsNone() ||
            MappedFunctionNames.Contains(SourceFunctionName))
        {
            return Fail(TEXT("Function mappings must have unique, non-empty source and target names."));
        }
        MappedFunctionNames.Add(SourceFunctionName);

        if (!FindBlueprintFunctionGraph(SourceLibrary, SourceFunctionName))
        {
            return Fail(FString::Printf(
                TEXT("Source function graph '%s' was not found in '%s'."),
                *SourceFunctionName.ToString(), *SourceLibrary->GetPathName()));
        }

        UClass* TargetClass = LoadObject<UClass>(
            nullptr, *Mapping.TargetClassPath.TrimStartAndEnd());
        UFunction* TargetFunction = IsValid(TargetClass)
            ? TargetClass->FindFunctionByName(TargetFunctionName)
            : nullptr;
        constexpr EFunctionFlags RequiredFlags = FUNC_Static | FUNC_BlueprintPure;
        if (!IsValid(TargetFunction) ||
            (TargetFunction->FunctionFlags & RequiredFlags) != RequiredFlags)
        {
            return Fail(FString::Printf(
                TEXT("Replacement '%s.%s' must resolve to a static Blueprint-pure UFUNCTION."),
                *Mapping.TargetClassPath, *TargetFunctionName.ToString()));
        }

        int32 MappingCallCount = 0;
        int32 MappingCastCount = 0;
        for (UEdGraph* Graph : Graphs)
        {
            if (!IsValid(Graph))
            {
                continue;
            }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
                if (!IsValid(CallNode) ||
                    CallNode->FunctionReference.GetMemberName() != SourceFunctionName ||
                    CallNode->FunctionReference.GetMemberParentClass() != SourceLibrary->GeneratedClass)
                {
                    continue;
                }

                UFunction* OriginalFunction = CallNode->GetTargetFunction();
                if (!IsValid(OriginalFunction))
                {
                    return Fail(FString::Printf(
                        TEXT("A call to '%s' has no resolved source UFunction."),
                        *SourceFunctionName.ToString()));
                }

                UK2Node_CallFunction* ReplacementNode =
                    NewObject<UK2Node_CallFunction>(Graph);
                ReplacementNode->SetFromFunction(TargetFunction);
                ReplacementNode->AllocateDefaultPins();
                const UEdGraphSchema* Schema = Graph->GetSchema();
                FCallNodeRebind Rebind;
                Rebind.Node = CallNode;
                Rebind.OriginalFunction = OriginalFunction;
                Rebind.ReplacementFunction = TargetFunction;
                Rebind.ChangeDescription = FString::Printf(
                    TEXT("%s -> %s.%s"),
                    *SourceFunctionName.ToString(),
                    *Mapping.TargetClassPath,
                    *TargetFunctionName.ToString());

                for (const UEdGraphPin* Pin : CallNode->Pins)
                {
                    if (!Pin || Pin->LinkedTo.IsEmpty())
                    {
                        continue;
                    }
                    UEdGraphPin* ReplacementPin =
                        ReplacementNode->FindPin(Pin->PinName, Pin->Direction);
                    if (!ReplacementPin || !Schema)
                    {
                        return Fail(FString::Printf(
                            TEXT("A connected '%s' pin has no matching replacement pin."),
                            *Pin->PinName.ToString()));
                    }
                    for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin)
                        {
                            return Fail(FString::Printf(
                                TEXT("Connected pin '%s' references an invalid graph pin."),
                                *Pin->PinName.ToString()));
                        }
                        const ECanCreateConnectionResponse ConnectionResponse =
                            Schema->CanCreateConnection(
                                ReplacementPin, const_cast<UEdGraphPin*>(LinkedPin)).Response;
                        if (ConnectionResponse == CONNECT_RESPONSE_MAKE)
                        {
                            continue;
                        }

                        const FEdGraphPinType& PreviousPinType = Pin->PinType;
                        const FEdGraphPinType& ReplacementPinType = ReplacementPin->PinType;
                        const bool bExistingReciprocalLink = LinkedPin->LinkedTo.Contains(
                            const_cast<UEdGraphPin*>(Pin));
                        if (bExistingReciprocalLink &&
                            PreviousPinType.PinCategory == ReplacementPinType.PinCategory &&
                            PreviousPinType.PinSubCategory == ReplacementPinType.PinSubCategory &&
                            PreviousPinType.PinSubCategoryObject.Get() == ReplacementPinType.PinSubCategoryObject.Get() &&
                            PreviousPinType.ContainerType == ReplacementPinType.ContainerType)
                        {
                            continue;
                        }

                        UClass* PreviousOutputClass = Pin->Direction == EGPD_Output
                            ? Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get())
                            : nullptr;
                        UClass* ReplacementOutputClass = Pin->Direction == EGPD_Output
                            ? Cast<UClass>(ReplacementPin->PinType.PinSubCategoryObject.Get())
                            : nullptr;
                        UClass* RequiredInputClass = LinkedPin->Direction == EGPD_Input
                            ? Cast<UClass>(LinkedPin->PinType.PinSubCategoryObject.Get())
                            : nullptr;
                        if (Pin->Direction != EGPD_Output || LinkedPin->Direction != EGPD_Input ||
                            Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object ||
                            LinkedPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object ||
                            !IsValid(PreviousOutputClass) || !IsValid(ReplacementOutputClass) ||
                            !IsValid(RequiredInputClass) ||
                            !PreviousOutputClass->IsChildOf(RequiredInputClass) ||
                            !RequiredInputClass->IsChildOf(ReplacementOutputClass))
                        {
                            return Fail(FString::Printf(
                                TEXT("Replacement pin '%s' cannot preserve an existing direct connection."),
                                *Pin->PinName.ToString()));
                        }

                        UK2Node_DynamicCast* CastProbe =
                            NewObject<UK2Node_DynamicCast>(Graph);
                        CastProbe->TargetType = RequiredInputClass;
                        CastProbe->SetPurity(true);
                        CastProbe->AllocateDefaultPins();
                        UEdGraphPin* CastSource = CastProbe->GetCastSourcePin();
                        UEdGraphPin* CastResult = CastProbe->GetCastResultPin();
                        if (!CastSource || !CastResult ||
                            Schema->CanCreateConnection(ReplacementPin, CastSource).Response != CONNECT_RESPONSE_MAKE ||
                            Schema->CanCreateConnection(CastResult, const_cast<UEdGraphPin*>(LinkedPin)).Response != CONNECT_RESPONSE_MAKE)
                        {
                            return Fail(FString::Printf(
                                TEXT("A pure cast to '%s' cannot preserve the connection from pin '%s'."),
                                *RequiredInputClass->GetPathName(), *Pin->PinName.ToString()));
                        }

                        FCallNodeRebind::FPinCast& Cast = Rebind.Casts.AddDefaulted_GetRef();
                        Cast.CallPinName = Pin->PinName;
                        Cast.TargetPin = const_cast<UEdGraphPin*>(LinkedPin);
                        Cast.CastClass = RequiredInputClass;
                    }
                }

                if (!OriginalConnections.Contains(Graph))
                {
                    OriginalConnections.Add(Graph, CaptureGraphConnectionKeys(Graph));
                }
                MappingCastCount += Rebind.Casts.Num();
                Rebinds.Add(MoveTemp(Rebind));
                ++MappingCallCount;
            }
        }

        if (MappingCallCount == 0)
        {
            return Fail(FString::Printf(
                TEXT("No caller nodes for '%s' were found in '%s'."),
                *SourceFunctionName.ToString(), *Blueprint->GetPathName()));
        }
        Result.Changes.Add(FString::Printf(
            TEXT("%s: %d call node(s), %d pure cast(s) inserted"),
            *SourceFunctionName.ToString(), MappingCallCount, MappingCastCount));
        Result.ChangeCount += MappingCallCount + MappingCastCount;
    }

    auto Rollback = [&Rebinds, Blueprint]()
    {
        for (int32 Index = Rebinds.Num() - 1; Index >= 0; --Index)
        {
            FCallNodeRebind& Rebind = Rebinds[Index];
            for (FCallNodeRebind::FPinCast& Cast : Rebind.Casts)
            {
                if (IsValid(Cast.CastNode))
                {
                    Cast.CastNode->GetGraph()->RemoveNode(Cast.CastNode);
                }
            }
            if (IsValid(Rebind.Node) && IsValid(Rebind.OriginalFunction))
            {
                Rebind.Node->SetFromFunction(Rebind.OriginalFunction);
                Rebind.Node->ReconstructNode();
            }
        }
        if (IsValid(Blueprint))
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
    };

    for (FCallNodeRebind& Rebind : Rebinds)
    {
        Rebind.Node->Modify();
        Rebind.Node->SetFromFunction(Rebind.ReplacementFunction);
        Rebind.Node->ReconstructNode();

        for (FCallNodeRebind::FPinCast& Cast : Rebind.Casts)
        {
            UEdGraph* Graph = Rebind.Node->GetGraph();
            UEdGraphPin* CallPin =
                Rebind.Node->FindPin(Cast.CallPinName, EGPD_Output);
            UEdGraphPin* CastObjectInput = nullptr;
            UEdGraphPin* CastObjectOutput = nullptr;
            if (!IsValid(Graph) || !CallPin || !Cast.TargetPin)
            {
                Rollback();
                return Fail(TEXT("A planned pure cast lost its call or target pin; all changes were restored."));
            }
            if (CallPin->LinkedTo.Contains(Cast.TargetPin))
            {
                CallPin->BreakLinkTo(Cast.TargetPin);
            }

            Cast.CastNode = NewObject<UK2Node_DynamicCast>(Graph);
            Cast.CastNode->TargetType = Cast.CastClass;
            Cast.CastNode->SetPurity(true);
            Graph->AddNode(Cast.CastNode, true, false);
            Cast.CastNode->CreateNewGuid();
            Cast.CastNode->PostPlacedNewNode();
            Cast.CastNode->AllocateDefaultPins();
            CastObjectInput = Cast.CastNode->GetCastSourcePin();
            CastObjectOutput = Cast.CastNode->GetCastResultPin();
            const UEdGraphSchema* Schema = Graph->GetSchema();
            if (!Schema || !CastObjectInput || !CastObjectOutput ||
                !Schema->TryCreateConnection(CallPin, CastObjectInput) ||
                !Schema->TryCreateConnection(CastObjectOutput, Cast.TargetPin))
            {
                Rollback();
                return Fail(TEXT("Unreal could not insert a planned pure cast; all changes were restored."));
            }
        }
    }
    Result.bModifiedInMemory = true;

    bool bConnectionsPreserved = true;
    for (const TPair<UEdGraph*, TSet<FString>>& Original : OriginalConnections)
    {
        TSet<FString> LogicalConnections = CaptureGraphConnectionKeys(Original.Key);
        for (const FCallNodeRebind& Rebind : Rebinds)
        {
            if (Rebind.Node->GetGraph() != Original.Key)
            {
                continue;
            }
            for (const FCallNodeRebind::FPinCast& Cast : Rebind.Casts)
            {
                if (!IsValid(Cast.CastNode) || !Cast.TargetPin)
                {
                    bConnectionsPreserved = false;
                    break;
                }
                const UEdGraphPin* CastInput = Cast.CastNode->GetCastSourcePin();
                const UEdGraphPin* CastOutput = Cast.CastNode->GetCastResultPin();
                const UEdGraphPin* CallOutput = Rebind.Node->FindPin(Cast.CallPinName, EGPD_Output);
                if (!CastInput || !CastOutput || !CallOutput || !Cast.TargetPin->GetOwningNode())
                {
                    bConnectionsPreserved = false;
                    break;
                }
                LogicalConnections.Remove(MakeGraphConnectionKey(
                    Rebind.Node->NodeGuid, Cast.CallPinName,
                    Cast.CastNode->NodeGuid, CastInput->PinName));
                LogicalConnections.Remove(MakeGraphConnectionKey(
                    Cast.CastNode->NodeGuid, CastOutput->PinName,
                    Cast.TargetPin->GetOwningNode()->NodeGuid, Cast.TargetPin->PinName));
                LogicalConnections.Add(MakeGraphConnectionKey(
                    Rebind.Node->NodeGuid, Cast.CallPinName,
                    Cast.TargetPin->GetOwningNode()->NodeGuid, Cast.TargetPin->PinName));
            }
        }

        if (!bConnectionsPreserved || LogicalConnections.Num() != Original.Value.Num())
        {
            bConnectionsPreserved = false;
            break;
        }
        for (const FString& Connection : Original.Value)
        {
            if (!LogicalConnections.Contains(Connection))
            {
                bConnectionsPreserved = false;
                break;
            }
        }
        if (!bConnectionsPreserved)
        {
            break;
        }
    }

    if (!bConnectionsPreserved)
    {
        Rollback();
        return Fail(TEXT("Rebinding changed graph links; all call nodes were restored and the asset was not saved."));
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FString SaveError;
    if (!CompileAndSaveBlueprint(Blueprint, SaveError))
    {
        Rollback();
        return Fail(SaveError);
    }

    Result.bSuccess = true;
    Result.bSaved = true;
    return Result;
}

FN2CBlueprintMutationResult UN2CBlueprintToolset::RemoveBlueprintFunctionGraphs(
    const FString& BlueprintAssetPath, const TArray<FString>& FunctionGraphNames,
    const bool bExternalReferencesReviewed)
{
    N2C_LOG_FUNCTION_ENTRY();
    FN2CBlueprintMutationResult Result;
    Result.AssetObjectPath = BlueprintAssetPath.TrimStartAndEnd();
    auto Fail = [&Result](const FString& Message)
    {
        Result.Error = Message;
        return Result;
    };

    if (!IsInGameThread())
    {
        return Fail(TEXT("Blueprint function graph removal must run on the Unreal game thread."));
    }
    if (!bExternalReferencesReviewed || FunctionGraphNames.IsEmpty())
    {
        return Fail(TEXT("Review external Blueprint callers and name at least one function graph before removal."));
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Result.AssetObjectPath);
    if (!IsValid(Blueprint) || !IsValid(Blueprint->GeneratedClass))
    {
        return Fail(TEXT("The function library Blueprint must load and have a generated class."));
    }

    TSet<FName> NamesToRemove;
    TArray<UEdGraph*> GraphsToRemove;
    for (const FString& Name : FunctionGraphNames)
    {
        const FName GraphName(*Name.TrimStartAndEnd());
        if (GraphName.IsNone() || NamesToRemove.Contains(GraphName))
        {
            return Fail(TEXT("Function graph names must be unique and non-empty."));
        }
        UEdGraph* FunctionGraph = FindBlueprintFunctionGraph(Blueprint, GraphName);
        if (!IsValid(FunctionGraph))
        {
            return Fail(FString::Printf(TEXT("Function graph '%s' was not found."), *GraphName.ToString()));
        }
        NamesToRemove.Add(GraphName);
        GraphsToRemove.Add(FunctionGraph);
    }

    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!IsValid(Graph))
        {
            continue;
        }
        const bool bCallerWillBeRemoved = NamesToRemove.Contains(Graph->GetFName());
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
            if (!IsValid(CallNode) ||
                CallNode->FunctionReference.GetMemberParentClass() != Blueprint->GeneratedClass ||
                !NamesToRemove.Contains(CallNode->FunctionReference.GetMemberName()))
            {
                continue;
            }
            if (!bCallerWillBeRemoved)
            {
                return Fail(FString::Printf(
                    TEXT("Surviving graph '%s' still calls '%s'."),
                    *Graph->GetName(), *CallNode->FunctionReference.GetMemberName().ToString()));
            }
        }
    }

    for (UEdGraph* Graph : GraphsToRemove)
    {
        FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Default);
        Result.Changes.Add(Graph->GetName());
    }
    Result.ChangeCount = GraphsToRemove.Num();
    Result.bModifiedInMemory = true;

    FString SaveError;
    if (!CompileAndSaveBlueprint(Blueprint, SaveError))
    {
        return Fail(SaveError);
    }
    Result.bSuccess = true;
    Result.bSaved = true;
    return Result;
}
