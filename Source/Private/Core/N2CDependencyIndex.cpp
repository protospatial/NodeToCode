// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CDependencyIndex.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"
#include "Utils/N2CLogger.h"

namespace N2CDependencyIndexPrivate
{
	/** Enums, then structs, interfaces, macro libraries, Blueprints: the order among assets that are ready together */
	int32 KindRank(EN2CDependencyNodeKind Kind)
	{
		return static_cast<int32>(Kind);
	}

	/**
	 * The project's own content, by package name alone (nothing is loaded): /Game, or a content plugin the project holds
	 * (FN2CCaptureContext::IsUserContent's rule). Never /Engine, /Script, /Temp or an engine plugin.
	 */
	bool IsUserPackage(const FName PackageName)
	{
		const FString Root = FPackageName::SplitPackageNameRoot(PackageName, nullptr);
		if (Root == TEXT("Game"))
		{
			return true;
		}
		const TSharedPtr<IPlugin> Plugin = Root.IsEmpty() ? nullptr : IPluginManager::Get().FindPlugin(Root);
		return Plugin.IsValid() && Plugin->CanContainContent() && Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project;
	}

	/** Whether the asset is one the index holds, and as what */
	bool Classify(const FAssetData& Asset, EN2CDependencyNodeKind& OutKind)
	{
		// The asset's class, through the class redirects (UUserDefinedStruct moved from Engine to CoreUObject in 5.5, so a
		// package saved before names the old path)
		const UClass* AssetClass = FindObject<UClass>(Asset.AssetClassPath);
		if (!AssetClass)
		{
			const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
				FCoreRedirectObjectName(Asset.AssetClassPath.ToString()));
			AssetClass = FindObject<UClass>(nullptr, *Redirected.ToString());
		}
		if (!AssetClass)
		{
			return false;
		}
		if (AssetClass->IsChildOf(UUserDefinedStruct::StaticClass()))
		{
			OutKind = EN2CDependencyNodeKind::Struct;
			return true;
		}
		if (AssetClass->IsChildOf(UUserDefinedEnum::StaticClass()))
		{
			OutKind = EN2CDependencyNodeKind::Enum;
			return true;
		}
		if (!AssetClass->IsChildOf(UBlueprint::StaticClass()))
		{
			return false;
		}
		// FBlueprintTags::BlueprintType holds the EBlueprintType name, e.g. BPTYPE_Interface
		FString Type;
		Asset.GetTagValue(FBlueprintTags::BlueprintType, Type);
		OutKind = Type.Contains(TEXT("BPTYPE_Interface")) ? EN2CDependencyNodeKind::Interface
			: Type.Contains(TEXT("BPTYPE_MacroLibrary")) ? EN2CDependencyNodeKind::MacroLibrary
			: EN2CDependencyNodeKind::Blueprint;
		return true;
	}

	/** The packages of every Blueprint generated class a tag value names ("/Game/BP_A.BP_A_C" -> "/Game/BP_A") */
	TArray<FName> GeneratedClassPackages(const FString& TagValue)
	{
		TArray<FName> Packages;
		static const FRegexPattern Pattern(TEXT("(/[A-Za-z0-9_/\\-]+)\\.[A-Za-z0-9_\\-]+_C\\b"));
		FRegexMatcher Matcher(Pattern, TagValue);
		while (Matcher.FindNext())
		{
			Packages.AddUnique(FName(*Matcher.GetCaptureGroup(1)));
		}
		return Packages;
	}

	/** The package of the asset behind a referenced type: a Blueprint's class, a user struct or a user enum */
	FName PackageOfType(const UObject* Type)
	{
		if (!Type)
		{
			return NAME_None;
		}
		if (const UClass* Class = Cast<UClass>(Type))
		{
			const UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(Class);
			return Blueprint ? Blueprint->GetOutermost()->GetFName() : NAME_None;
		}
		if (Type->IsA<UUserDefinedStruct>() || Type->IsA<UUserDefinedEnum>())
		{
			return Type->GetOutermost()->GetFName();
		}
		return NAME_None;
	}

	/** Builds the index; one per Build call */
	struct FBuilder
	{
		FN2CDependencyIndex& Index;
		const FN2CDependencyIndexOptions& Options;
		IAssetRegistry& Registry;
		TMap<FName, int32> NodeOfPackage;
		TSet<FString> EdgeKeys;

		int32 AddNode(const FAssetData& Asset, EN2CDependencyNodeKind Kind, bool bRoot)
		{
			if (const int32* Existing = NodeOfPackage.Find(Asset.PackageName))
			{
				Index.Nodes[*Existing].bRoot |= bRoot;
				return *Existing;
			}
			FN2CDependencyNode& Node = Index.Nodes.AddDefaulted_GetRef();
			Node.Path = Asset.GetObjectPathString();
			Node.Name = Asset.AssetName.ToString();
			Node.Kind = Kind;
			Node.bRoot = bRoot;
			return NodeOfPackage.Add(Asset.PackageName, Index.Nodes.Num() - 1);
		}

		/** The node of a package, adding it when it is the project's own content and the options allow; INDEX_NONE */
		int32 NodeFor(const FName Package, bool bMayAdd)
		{
			if (const int32* Existing = NodeOfPackage.Find(Package))
			{
				return *Existing;
			}
			if (!bMayAdd || !Options.bIncludeReferencedUserAssets || !IsUserPackage(Package))
			{
				return INDEX_NONE;
			}
			TArray<FAssetData> Assets;
			Registry.GetAssetsByPackageName(Package, Assets);
			for (const FAssetData& Asset : Assets)
			{
				EN2CDependencyNodeKind Kind;
				if (Classify(Asset, Kind))
				{
					return AddNode(Asset, Kind, false);
				}
			}
			return INDEX_NONE;
		}

		void AddEdge(int32 From, int32 To, EN2CDependencyEdgeKind Kind, const FString& Graph = FString(), const FString& Node = FString())
		{
			if (From == INDEX_NONE || To == INDEX_NONE || From == To)
			{
				return;
			}
			const FString Key = FString::Printf(TEXT("%d>%d:%d:%s:%s"), From, To, static_cast<int32>(Kind), *Graph, *Node);
			if (EdgeKeys.Contains(Key))
			{
				return;
			}
			EdgeKeys.Add(Key);
			FN2CDependencyEdge& Edge = Index.Edges.AddDefaulted_GetRef();
			Edge.From = From;
			Edge.To = To;
			Edge.Kind = Kind;
			Edge.Graph = Graph;
			Edge.Node = Node;
		}

		void AddRoots(const TArray<FString>& Roots)
		{
			for (const FString& RawRoot : Roots)
			{
				FString Root = RawRoot.TrimStartAndEnd();
				while (Root.Len() > 1 && Root.EndsWith(TEXT("/")))
				{
					Root.LeftChopInline(1);
				}
				if (Root.IsEmpty())
				{
					continue;
				}
				const FString PackageName = Root.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(Root) : Root;

				TArray<FAssetData> Assets;
				Registry.GetAssetsByPackageName(FName(*PackageName), Assets);
				if (Assets.Num() == 0)
				{
					Registry.GetAssetsByPath(FName(*Root), Assets, /*bRecursive*/ true);
				}
				int32 Added = 0;
				for (const FAssetData& Asset : Assets)
				{
					EN2CDependencyNodeKind Kind;
					if (Classify(Asset, Kind))
					{
						AddNode(Asset, Kind, true);
						++Added;
					}
				}
				if (Added == 0)
				{
					Index.Warnings.Add(FString::Printf(TEXT("'%s' names no Blueprint, user struct or user enum (an asset path or a content folder)"), *Root));
				}
			}
		}

		/** Hard package dependencies, following the project's own assets the nodes depend on */
		void AddPackageEdges()
		{
			for (int32 NodeIndex = 0; NodeIndex < Index.Nodes.Num(); ++NodeIndex)
			{
				const FName Package(*FPackageName::ObjectPathToPackageName(Index.Nodes[NodeIndex].Path));
				TArray<FName> Dependencies;
				Registry.GetDependencies(Package, Dependencies, UE::AssetRegistry::EDependencyCategory::Package,
					UE::AssetRegistry::EDependencyQuery::Hard);
				for (const FName Dependency : Dependencies)
				{
					AddEdge(NodeIndex, NodeFor(Dependency, /*bMayAdd*/ true), EN2CDependencyEdgeKind::HardPackage);
				}
			}
		}

		/** Parent class and implemented interfaces, from the Blueprints' AssetRegistry tags */
		void AddTagEdges()
		{
			for (int32 NodeIndex = 0; NodeIndex < Index.Nodes.Num(); ++NodeIndex)
			{
				const FN2CDependencyNode& Node = Index.Nodes[NodeIndex];
				if (Node.Kind == EN2CDependencyNodeKind::Struct || Node.Kind == EN2CDependencyNodeKind::Enum)
				{
					continue;
				}
				const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(Node.Path));
				FString Parent;
				if (Asset.GetTagValue(FBlueprintTags::ParentClassPath, Parent))
				{
					for (const FName Package : GeneratedClassPackages(Parent))
					{
						AddEdge(NodeIndex, NodeFor(Package, false), EN2CDependencyEdgeKind::Parent);
					}
				}
				FString Interfaces;
				if (Asset.GetTagValue(FBlueprintTags::ImplementedInterfaces, Interfaces))
				{
					for (const FName Package : GeneratedClassPackages(Interfaces))
					{
						AddEdge(NodeIndex, NodeFor(Package, false), EN2CDependencyEdgeKind::Interface);
					}
				}
			}
		}

		/** The types a pin type refers to: its own and, for a map, its value's */
		void AddTypeEdges(int32 From, const FEdGraphPinType& Type, const FString& Graph, const FString& Name)
		{
			AddEdge(From, NodeFor(PackageOfType(Type.PinSubCategoryObject.Get()), false), EN2CDependencyEdgeKind::VariableType, Graph, Name);
			AddEdge(From, NodeFor(PackageOfType(Type.PinValueType.TerminalSubCategoryObject.Get()), false), EN2CDependencyEdgeKind::VariableType, Graph, Name);
		}

		/** Calls, macros, casts and variable types, from the loaded Blueprints (game thread) */
		void AddLoadedEdges()
		{
			check(IsInGameThread());
			const int32 Count = Index.Nodes.Num();
			for (int32 NodeIndex = 0; NodeIndex < Count; ++NodeIndex)
			{
				const FN2CDependencyNode& Node = Index.Nodes[NodeIndex];
				if (Node.Kind == EN2CDependencyNodeKind::Struct || Node.Kind == EN2CDependencyNodeKind::Enum)
				{
					continue;
				}
				UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Node.Path);
				if (!Blueprint)
				{
					Index.Warnings.Add(FString::Printf(TEXT("%s did not load; its calls, macros, casts and variables are not indexed"), *Node.Path));
					continue;
				}

				for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
				{
					AddTypeEdges(NodeIndex, Variable.VarType, FString(), Variable.VarName.ToString());
				}

				TArray<UEdGraph*> Graphs;
				Blueprint->GetAllGraphs(Graphs);
				for (const UEdGraph* Graph : Graphs)
				{
					if (!Graph)
					{
						continue;
					}
					const FString GraphName = Graph->GetName();
					for (const UEdGraphNode* GraphNode : Graph->Nodes)
					{
						const FString NodeId = GraphNode ? GraphNode->NodeGuid.ToString(EGuidFormats::Digits) : FString();
						if (const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(GraphNode))
						{
							const UEdGraph* MacroGraph = Macro->GetMacroGraph();
							const UBlueprint* Owner = MacroGraph ? FBlueprintEditorUtils::FindBlueprintForGraph(MacroGraph) : nullptr;
							AddEdge(NodeIndex, Owner ? NodeFor(Owner->GetOutermost()->GetFName(), false) : INDEX_NONE,
								EN2CDependencyEdgeKind::Macro, GraphName, NodeId);
						}
						else if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(GraphNode))
						{
							const UFunction* Function = Call->GetTargetFunction();
							const UClass* Owner = Function ? Function->GetOwnerClass()
								: Call->FunctionReference.GetMemberParentClass(Call->GetBlueprintClassFromNode());
							AddEdge(NodeIndex, NodeFor(PackageOfType(Owner), false), EN2CDependencyEdgeKind::Calls, GraphName, NodeId);
						}
						else if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(GraphNode))
						{
							AddEdge(NodeIndex, NodeFor(PackageOfType(CastNode->TargetType), false), EN2CDependencyEdgeKind::Cast, GraphName, NodeId);
						}
						else if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(GraphNode))
						{
							for (const FBPVariableDescription& Local : Entry->LocalVariables)
							{
								AddTypeEdges(NodeIndex, Local.VarType, GraphName, Local.VarName.ToString());
							}
						}
					}
				}
			}
		}
	};
}

FN2CDependencyIndex FN2CDependencyIndex::Build(const TArray<FString>& Roots, const FN2CDependencyIndexOptions& Options)
{
	using namespace N2CDependencyIndexPrivate;
	check(IsInGameThread());

	FN2CDependencyIndex Index;
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Still discovering assets (an editor that just started): scan the roots now, so the index is not partial
	if (Registry.IsLoadingAssets())
	{
		TArray<FString> Paths;
		for (const FString& Root : Roots)
		{
			const FString Trimmed = Root.TrimStartAndEnd();
			Paths.AddUnique(Trimmed.Contains(TEXT(".")) ? FPackageName::GetLongPackagePath(FPackageName::ObjectPathToPackageName(Trimmed)) : Trimmed);
		}
		Registry.ScanPathsSynchronous(Paths, /*bForceRescan*/ false);
	}

	FBuilder Builder{ Index, Options, Registry };
	Builder.AddRoots(Roots);
	Builder.AddPackageEdges();
	Builder.AddTagEdges();
	if (Options.bLoadAssets)
	{
		Builder.AddLoadedEdges();
	}
	Index.ComputeOrder();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Dependency index: %d nodes, %d edges, %d cycle groups, %d warnings"),
		Index.Nodes.Num(), Index.Edges.Num(), Index.CycleGroups.Num(), Index.Warnings.Num()), EN2CLogSeverity::Info, TEXT("DependencyIndex"));
	return Index;
}

void FN2CDependencyIndex::ComputeOrder()
{
	using namespace N2CDependencyIndexPrivate;
	Order.Reset();
	CycleGroups.Reset();
	const int32 Count = Nodes.Num();

	TArray<TArray<int32>> DependsOn;
	DependsOn.SetNum(Count);
	for (const FN2CDependencyEdge& Edge : Edges)
	{
		if (Nodes.IsValidIndex(Edge.From) && Nodes.IsValidIndex(Edge.To) && Edge.From != Edge.To)
		{
			DependsOn[Edge.From].AddUnique(Edge.To);
		}
	}

	// Tarjan's strongly connected components, iteratively (a project's chains can be deep)
	TArray<int32> IndexOf, LowLink, Component;
	IndexOf.Init(INDEX_NONE, Count);
	LowLink.Init(0, Count);
	Component.Init(INDEX_NONE, Count);
	TArray<bool> OnStack;
	OnStack.Init(false, Count);
	TArray<int32> Stack;
	TArray<TArray<int32>> Components;
	int32 NextIndex = 0;
	for (int32 Start = 0; Start < Count; ++Start)
	{
		if (IndexOf[Start] != INDEX_NONE)
		{
			continue;
		}
		TArray<TPair<int32, int32>> Work;  // node, next dependency to visit
		Work.Add({ Start, 0 });
		IndexOf[Start] = LowLink[Start] = NextIndex++;
		Stack.Push(Start);
		OnStack[Start] = true;
		while (Work.Num() > 0)
		{
			TPair<int32, int32>& Top = Work.Last();
			const int32 V = Top.Key;
			if (Top.Value < DependsOn[V].Num())
			{
				const int32 W = DependsOn[V][Top.Value++];
				if (IndexOf[W] == INDEX_NONE)
				{
					IndexOf[W] = LowLink[W] = NextIndex++;
					Stack.Push(W);
					OnStack[W] = true;
					Work.Add({ W, 0 });
				}
				else if (OnStack[W])
				{
					LowLink[V] = FMath::Min(LowLink[V], IndexOf[W]);
				}
				continue;
			}
			if (LowLink[V] == IndexOf[V])
			{
				TArray<int32>& Members = Components.AddDefaulted_GetRef();
				int32 W;
				do
				{
					W = Stack.Pop(EAllowShrinking::No);
					OnStack[W] = false;
					Component[W] = Components.Num() - 1;
					Members.Add(W);
				} while (W != V);
			}
			Work.Pop(EAllowShrinking::No);
			if (Work.Num() > 0)
			{
				const int32 Parent = Work.Last().Key;
				LowLink[Parent] = FMath::Min(LowLink[Parent], LowLink[V]);
			}
		}
	}

	// Each component's members by kind and path; its key for the ready queue is its first member's
	auto Before = [this](int32 A, int32 B)
	{
		const int32 RankA = KindRank(Nodes[A].Kind), RankB = KindRank(Nodes[B].Kind);
		return RankA != RankB ? RankA < RankB : Nodes[A].Path < Nodes[B].Path;
	};
	for (TArray<int32>& Members : Components)
	{
		Members.Sort(Before);
	}

	// Kahn's algorithm over the components: a component is ready when every component it depends on is placed
	TArray<TSet<int32>> ComponentDeps;
	ComponentDeps.SetNum(Components.Num());
	TArray<TArray<int32>> Dependents;
	Dependents.SetNum(Components.Num());
	for (int32 V = 0; V < Count; ++V)
	{
		for (const int32 W : DependsOn[V])
		{
			if (Component[V] != Component[W] && !ComponentDeps[Component[V]].Contains(Component[W]))
			{
				ComponentDeps[Component[V]].Add(Component[W]);
				Dependents[Component[W]].Add(Component[V]);
			}
		}
	}
	TArray<int32> Remaining;
	Remaining.SetNum(Components.Num());
	auto ComponentBefore = [&Components, &Before](int32 A, int32 B) { return Before(Components[A][0], Components[B][0]); };
	TArray<int32> Ready;
	for (int32 C = 0; C < Components.Num(); ++C)
	{
		Remaining[C] = ComponentDeps[C].Num();
		if (Remaining[C] == 0)
		{
			Ready.HeapPush(C, ComponentBefore);
		}
	}
	while (Ready.Num() > 0)
	{
		int32 C;
		Ready.HeapPop(C, ComponentBefore, EAllowShrinking::No);
		Order.Append(Components[C]);
		if (Components[C].Num() > 1)
		{
			TArray<int32> Group = Components[C];
			Group.Sort([this](int32 A, int32 B) { return Nodes[A].Path < Nodes[B].Path; });
			CycleGroups.Add(MoveTemp(Group));
		}
		for (const int32 D : Dependents[C])
		{
			if (--Remaining[D] == 0)
			{
				Ready.HeapPush(D, ComponentBefore);
			}
		}
	}
	check(Order.Num() == Count);
}

int32 FN2CDependencyIndex::FindNode(const FString& Path) const
{
	const FString Package = Path.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(Path) : Path;
	return Nodes.IndexOfByPredicate([&](const FN2CDependencyNode& Node)
	{
		return Node.Path == Path || FPackageName::ObjectPathToPackageName(Node.Path) == Package;
	});
}

bool FN2CDependencyIndex::HasEdge(int32 From, int32 To, EN2CDependencyEdgeKind Kind) const
{
	return Edges.ContainsByPredicate([&](const FN2CDependencyEdge& Edge)
	{
		return Edge.From == From && Edge.To == To && Edge.Kind == Kind;
	});
}

FString FN2CDependencyIndex::EdgeKindToString(EN2CDependencyEdgeKind Kind)
{
	switch (Kind)
	{
	case EN2CDependencyEdgeKind::HardPackage: return TEXT("hard-package");
	case EN2CDependencyEdgeKind::Parent: return TEXT("parent");
	case EN2CDependencyEdgeKind::Interface: return TEXT("interface");
	case EN2CDependencyEdgeKind::Calls: return TEXT("calls");
	case EN2CDependencyEdgeKind::Macro: return TEXT("macro");
	case EN2CDependencyEdgeKind::Cast: return TEXT("cast");
	case EN2CDependencyEdgeKind::VariableType: return TEXT("variable-type");
	}
	return TEXT("unknown");
}

FString FN2CDependencyIndex::NodeKindToString(EN2CDependencyNodeKind Kind)
{
	switch (Kind)
	{
	case EN2CDependencyNodeKind::Enum: return TEXT("enum");
	case EN2CDependencyNodeKind::Struct: return TEXT("struct");
	case EN2CDependencyNodeKind::Interface: return TEXT("interface");
	case EN2CDependencyNodeKind::MacroLibrary: return TEXT("macro-library");
	case EN2CDependencyNodeKind::Blueprint: return TEXT("blueprint");
	}
	return TEXT("unknown");
}
