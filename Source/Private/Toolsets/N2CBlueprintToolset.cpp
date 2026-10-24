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

    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    TArray<UEdGraph*> Matches;
    TSet<UEdGraph*> SeenGraphs;
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph || SeenGraphs.Contains(Graph))
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

    if (Matches.IsEmpty())
    {
        Result.Error = FString::Printf(
            TEXT("Graph '%s' at path '%s' was not found in Blueprint '%s'."),
            *RequestedName, *RequestedPath, *AssetPath);
        return Result;
    }
    if (Matches.Num() > 1)
    {
        TArray<FString> CandidatePaths;
        for (const UEdGraph* Graph : Matches)
        {
            CandidatePaths.Add(Graph->GetPathName());
        }
        Result.Error = FString::Printf(
            TEXT("Graph name '%s' is ambiguous in Blueprint '%s'; pass graphPath from NodeToCode JSON. Candidates: %s"),
            *RequestedName, *AssetPath, *FString::Join(CandidatePaths, TEXT(", ")));
        return Result;
    }

    UEdGraph* Graph = Matches[0];
    Result.GraphName = Graph->GetName();
    Result.GraphGuid = Graph->GraphGuid.IsValid() ? Graph->GraphGuid.ToString() : FString();
    Result.GraphPath = Graph->GetPathName();
    Result.NativeNodeCount = Graph->Nodes.Num();
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
