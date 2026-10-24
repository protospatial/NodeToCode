// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Toolsets/N2CBlueprintToolset.h"

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"

FN2CBlueprintExportResult UN2CBlueprintToolset::ExportBlueprintJson(const FString& BlueprintAssetPath)
{
	FN2CBlueprintExportResult Result;
	const FString AssetObjectPath = BlueprintAssetPath.TrimStartAndEnd();
	if (AssetObjectPath.IsEmpty())
	{
		Result.Error = TEXT("BlueprintAssetPath must be a non-empty Blueprint object path.");
		return Result;
	}

	if (!IsInGameThread())
	{
		Result.Error = TEXT("Blueprint export must run on the Unreal game thread.");
		return Result;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
	if (!Blueprint)
	{
		Result.Error = FString::Printf(
			TEXT("Could not load Blueprint at '%s'. Use its object path, such as /Game/Blueprints/BP_Character.BP_Character."),
			*AssetObjectPath);
		return Result;
	}

	FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
	if (!Translator.GenerateFromBlueprint(Blueprint, true))
	{
		Result.Error = FString::Printf(TEXT("NodeToCode could not translate Blueprint '%s'."), *AssetObjectPath);
		return Result;
	}

	const FN2CBlueprint& BlueprintData = Translator.GetN2CBlueprint();
	if (!BlueprintData.IsValid())
	{
		Result.Error = FString::Printf(TEXT("NodeToCode produced invalid data for Blueprint '%s'."), *AssetObjectPath);
		return Result;
	}

	Result.BlueprintJson = FN2CSerializer::ToJson(BlueprintData);
	if (Result.BlueprintJson.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("NodeToCode could not serialize Blueprint '%s' to JSON."), *AssetObjectPath);
		return Result;
	}

    for (const FN2CGraph& Graph : BlueprintData.Graphs)
    {
        if (Graph.UnresolvedConnectionCount > 0)
        {
            Result.Error = FString::Printf(
                TEXT("Graph '%s' contains %d malformed pin links; check the returned JSON."),
                *Graph.Name, Graph.UnresolvedConnectionCount);
            return Result;
        }
    }
    Result.bSuccess = true;
    return Result;
}

FN2CBlueprintGraphTextResult UN2CBlueprintToolset::ExportBlueprintGraphText(
    const FString& BlueprintAssetPath, const FString& GraphName)
{
    FN2CBlueprintGraphTextResult Result;
    if (!IsInGameThread())
    {
        Result.Error = TEXT("Blueprint graph export must run on the Unreal game thread.");
        return Result;
    }

    const FString AssetPath = BlueprintAssetPath.TrimStartAndEnd();
    const FString RequestedName = GraphName.TrimStartAndEnd();
    if (AssetPath.IsEmpty() || RequestedName.IsEmpty())
    {
        Result.Error = TEXT("BlueprintAssetPath and GraphName are required.");
        return Result;
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Blueprint)
    {
        Result.Error = FString::Printf(TEXT("Unable to load Blueprint '%s'."), *AssetPath);
        return Result;
    }

    TArray<UEdGraph*> Graphs;
    Graphs.Append(Blueprint->UbergraphPages);
    Graphs.Append(Blueprint->FunctionGraphs);
    Graphs.Append(Blueprint->MacroGraphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph && Graph->GetName() == RequestedName)
        {
            TSet<UObject*> Nodes;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node)
                {
                    Nodes.Add(Node);
                }
            }
            if (Nodes.Num() == 0)
            {
                Result.Error = TEXT("The requested graph contains no nodes.");
                return Result;
            }

            FEdGraphUtilities::ExportNodesToText(Nodes, Result.GraphText);
            Result.bSuccess = !Result.GraphText.IsEmpty();
            if (!Result.bSuccess)
            {
                Result.Error = TEXT("UnrealEd returned empty graph text.");
            }
            return Result;
        }
    }

    Result.Error = FString::Printf(
        TEXT("Graph '%s' was not found in Blueprint '%s'."),
        *RequestedName, *AssetPath);
    return Result;
}
