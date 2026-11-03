// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CDependencyActions.h"
#include "Core/N2CDependencyIndex.h"
#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CGraphUtils.h"
#include "AssetRegistry/AssetData.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"

namespace N2CDependencyActionsPrivate
{
	/** The index nodes the roots reach over the index's edges (From depends on To), roots left out */
	TArray<int32> Dependencies(const FN2CDependencyIndex& Index)
	{
		TArray<int32> Stack;
		TSet<int32> Reached;
		for (int32 Node = 0; Node < Index.Nodes.Num(); ++Node)
		{
			if (Index.Nodes[Node].bRoot)
			{
				Stack.Add(Node);
				Reached.Add(Node);
			}
		}
		TArray<int32> Out;
		while (Stack.Num() > 0)
		{
			const int32 From = Stack.Pop(EAllowShrinking::No);
			for (const FN2CDependencyEdge& Edge : Index.Edges)
			{
				if (Edge.From == From && Edge.To != INDEX_NONE && !Reached.Contains(Edge.To))
				{
					Reached.Add(Edge.To);
					Stack.Add(Edge.To);
					Out.Add(Edge.To);
				}
			}
		}
		// Index order (dependencies first) reads better in a list than discovery order
		Out.Sort([&Index](int32 A, int32 B) { return Index.Order.IndexOfByKey(A) < Index.Order.IndexOfByKey(B); });
		return Out;
	}

	/** The Blueprint's own top-level graphs: event graph pages, functions (interface functions too) and macros */
	TArray<UEdGraph*> TopLevelGraphs(const UBlueprint& Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		for (const TArray<TObjectPtr<UEdGraph>>* List : { &Blueprint.UbergraphPages, &Blueprint.FunctionGraphs, &Blueprint.MacroGraphs })
		{
			for (UEdGraph* Graph : *List)
			{
				if (Graph)
				{
					Graphs.Add(Graph);
				}
			}
		}
		return Graphs;
	}
}

FN2CDependencyTagResult FN2CDependencyActions::TagDependencies(const TArray<FString>& RootBlueprintPaths, const FString& Tag,
	const FString& Category, UN2CGraphStateManager& StateManager)
{
	using namespace N2CDependencyActionsPrivate;

	FN2CDependencyTagResult Result;
	if (RootBlueprintPaths.Num() == 0 || Tag.TrimStartAndEnd().IsEmpty())
	{
		return Result;
	}

	const FN2CDependencyIndex Index = FN2CDependencyIndex::Build(RootBlueprintPaths);
	Result.Warnings = Index.Warnings;
	const TArray<int32> Deps = Dependencies(Index);
	Result.Dependencies = Deps.Num();

	for (const int32 NodeIndex : Deps)
	{
		const FN2CDependencyNode& Node = Index.Nodes[NodeIndex];
		const bool bHasGraphs = Node.Kind == EN2CDependencyNodeKind::Blueprint || Node.Kind == EN2CDependencyNodeKind::Interface
			|| Node.Kind == EN2CDependencyNodeKind::MacroLibrary;
		UBlueprint* Blueprint = bHasGraphs ? LoadObject<UBlueprint>(nullptr, *Node.Path) : nullptr;
		if (!Blueprint)
		{
			Result.Untagged.Add(Node.Name);
			if (bHasGraphs)
			{
				Result.Warnings.Add(FString::Printf(TEXT("%s did not load"), *Node.Path));
			}
			continue;
		}

		int32 Tagged = 0;
		for (UEdGraph* Graph : TopLevelGraphs(*Blueprint))
		{
			if (FN2CGraphUtils::CanTranslateGraph(Graph) && StateManager.AddTag(Graph->GraphGuid, Graph->GetName(),
				FSoftObjectPath(Blueprint), Tag, Category))
			{
				++Tagged;
			}
		}
		if (Tagged > 0)
		{
			++Result.TaggedBlueprints;
			Result.TaggedGraphs += Tagged;
		}
		else
		{
			Result.Untagged.Add(Node.Name);
		}
	}
	return Result;
}

TArray<FName> FN2CDependencyActions::ToPackageNames(const TArray<FString>& AssetPaths)
{
	TArray<FName> Packages;
	for (const FString& Path : AssetPaths)
	{
		const FString Trimmed = Path.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}
		const FString PackageName = Trimmed.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(Trimmed) : Trimmed;
		if (FPackageName::IsValidLongPackageName(PackageName))
		{
			Packages.AddUnique(FName(*PackageName));
		}
	}
	return Packages;
}

bool FN2CDependencyActions::OpenReferenceViewer(const TArray<FString>& AssetPaths)
{
	TArray<FAssetIdentifier> Assets;
	for (const FName Package : ToPackageNames(AssetPaths))
	{
		Assets.Emplace(Package);
	}
	if (Assets.Num() == 0 || !FEditorDelegates::OnOpenReferenceViewer.IsBound())
	{
		return false;
	}
	FEditorDelegates::OnOpenReferenceViewer.Broadcast(Assets, FReferenceViewerParams());
	return true;
}
