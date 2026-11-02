// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/Dependencies/N2CDependencyFixtures.h"
#include "Core/N2CDependencyIndex.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"

/**
 * Card 0318: FN2CDependencyIndex over the checked-in fixtures (N2CDependencyFixtures.h) under
 * /NodeToCodeTestContent/Dependencies: every node with its kind, the typed edges (hard package, parent, interface,
 * calls and macro with their graph and node, cast, variable type), an order that puts the enum, struct, interface and
 * macro library before BP_N2CDep_A and A before its child, and one cycle group {B, C}. With bLoadAssets false only the
 * AssetRegistry edges remain. The ordering itself is also checked on a graph made in code (kinds first, a cycle kept
 * together, dependencies before dependents whatever their kind).
 *
 * With N2C_GENERATE_DEPENDENCY_FIXTURES=1 (UE 5.6 only) the first case regenerates the fixtures first.
 */
BEGIN_DEFINE_SPEC(FN2CDependencyIndexSpec, "NodeToCode.Dependencies.Index", N2C_TEST_FLAGS)

	/** Regenerates the fixtures once per session when asked; false (with errors) when that fails */
	bool GenerateIfAsked()
	{
		static bool bGenerated = false;
		if (!N2CTest::DependencyFixtures::ShouldGenerate() || bGenerated)
		{
			return true;
		}
		bGenerated = true;
		TArray<FString> Errors;
		TArray<FString> Notes;
		const bool bOk = N2CTest::DependencyFixtures::GenerateAll(Errors, Notes);
		for (const FString& Note : Notes)
		{
			AddInfo(TEXT("generate: ") + Note);
		}
		for (const FString& Error : Errors)
		{
			AddError(TEXT("generate: ") + Error);
		}
		// The registry learns of the saved packages from a rescan
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanPathsSynchronous(
			{ N2CTest::DependencyFixtures::FolderPath }, /*bForceRescan*/ true);
		return bOk;
	}

	/** The node of a fixture, or INDEX_NONE with a test error */
	int32 NodeOf(const FN2CDependencyIndex& Index, const TCHAR* AssetName)
	{
		const int32 Node = Index.FindNode(N2CTest::DependencyFixtures::ObjectPath(AssetName));
		TestTrue(FString::Printf(TEXT("%s is a node (Tests/NodeToCodeTestContent is junctioned into the host's Plugins folder by build.ps1)"), AssetName), Node != INDEX_NONE);
		return Node;
	}

	void ExpectEdge(const FN2CDependencyIndex& Index, int32 From, int32 To, EN2CDependencyEdgeKind Kind, const TCHAR* What)
	{
		TestTrue(FString::Printf(TEXT("%s (%s)"), What, *FN2CDependencyIndex::EdgeKindToString(Kind)),
			From != INDEX_NONE && To != INDEX_NONE && Index.HasEdge(From, To, Kind));
	}

	int32 Position(const FN2CDependencyIndex& Index, int32 Node)
	{
		return Index.Order.IndexOfByKey(Node);
	}

	FN2CDependencyIndex BuildFixtures(bool bLoadAssets)
	{
		FN2CDependencyIndexOptions Options;
		Options.bLoadAssets = bLoadAssets;
		return FN2CDependencyIndex::Build({ N2CTest::DependencyFixtures::FolderPath }, Options);
	}
END_DEFINE_SPEC(FN2CDependencyIndexSpec)

void FN2CDependencyIndexSpec::Define()
{
	using namespace N2CTest::DependencyFixtures;

	Describe(TEXT("over the fixture folder"), [this]()
	{
		It(TEXT("holds every fixture with its kind"), [this]()
		{
			if (!GenerateIfAsked())
			{
				return;
			}
			const FN2CDependencyIndex Index = BuildFixtures(true);
			TestEqual(TEXT("eight nodes"), Index.Nodes.Num(), 8);
			const TPair<const TCHAR*, EN2CDependencyNodeKind> Expected[] = {
				{ EnumName, EN2CDependencyNodeKind::Enum }, { StructName, EN2CDependencyNodeKind::Struct },
				{ InterfaceName, EN2CDependencyNodeKind::Interface }, { MacroLibraryName, EN2CDependencyNodeKind::MacroLibrary },
				{ AName, EN2CDependencyNodeKind::Blueprint }, { BName, EN2CDependencyNodeKind::Blueprint },
				{ CName, EN2CDependencyNodeKind::Blueprint }, { ChildName, EN2CDependencyNodeKind::Blueprint } };
			for (const TPair<const TCHAR*, EN2CDependencyNodeKind>& Each : Expected)
			{
				const int32 Node = NodeOf(Index, Each.Key);
				if (Node != INDEX_NONE)
				{
					TestEqual(FString::Printf(TEXT("%s kind"), Each.Key), FN2CDependencyIndex::NodeKindToString(Index.Nodes[Node].Kind),
						FN2CDependencyIndex::NodeKindToString(Each.Value));
					TestTrue(FString::Printf(TEXT("%s is a root"), Each.Key), Index.Nodes[Node].bRoot);
				}
			}
			TestEqual(TEXT("no warnings: ") + FString::Join(Index.Warnings, TEXT("; ")), Index.Warnings.Num(), 0);
		});

		It(TEXT("finds each typed edge, with the graph and node of node-level edges"), [this]()
		{
			const FN2CDependencyIndex Index = BuildFixtures(true);
			const int32 E = NodeOf(Index, EnumName), S = NodeOf(Index, StructName), I = NodeOf(Index, InterfaceName);
			const int32 M = NodeOf(Index, MacroLibraryName), A = NodeOf(Index, AName), B = NodeOf(Index, BName);
			const int32 C = NodeOf(Index, CName), Child = NodeOf(Index, ChildName);

			ExpectEdge(Index, S, E, EN2CDependencyEdgeKind::HardPackage, TEXT("the struct uses the enum"));
			ExpectEdge(Index, A, S, EN2CDependencyEdgeKind::HardPackage, TEXT("A loads the struct"));
			ExpectEdge(Index, A, S, EN2CDependencyEdgeKind::VariableType, TEXT("A's Data is the struct"));
			ExpectEdge(Index, A, I, EN2CDependencyEdgeKind::Interface, TEXT("A implements the interface"));
			ExpectEdge(Index, A, B, EN2CDependencyEdgeKind::Calls, TEXT("A calls B's DoB"));
			ExpectEdge(Index, A, B, EN2CDependencyEdgeKind::VariableType, TEXT("A's TargetB is a B"));
			ExpectEdge(Index, A, M, EN2CDependencyEdgeKind::Macro, TEXT("A runs the library's macro"));
			ExpectEdge(Index, B, C, EN2CDependencyEdgeKind::VariableType, TEXT("B's OtherC is a C"));
			ExpectEdge(Index, C, B, EN2CDependencyEdgeKind::VariableType, TEXT("C's OtherB is a B"));
			ExpectEdge(Index, C, B, EN2CDependencyEdgeKind::Cast, TEXT("C casts to B"));
			ExpectEdge(Index, B, C, EN2CDependencyEdgeKind::HardPackage, TEXT("B loads C"));
			ExpectEdge(Index, C, B, EN2CDependencyEdgeKind::HardPackage, TEXT("C loads B"));
			ExpectEdge(Index, Child, A, EN2CDependencyEdgeKind::Parent, TEXT("the child's parent is A"));
			TestFalse(TEXT("B calls nothing of A"), Index.HasEdge(B, A, EN2CDependencyEdgeKind::Calls));

			const FN2CDependencyEdge* Call = Index.Edges.FindByPredicate([A, B](const FN2CDependencyEdge& Edge)
			{
				return Edge.From == A && Edge.To == B && Edge.Kind == EN2CDependencyEdgeKind::Calls;
			});
			if (TestNotNull(TEXT("the call edge"), Call))
			{
				TestEqual(TEXT("the call is in A's event graph"), Call->Graph, FString(TEXT("EventGraph")));
				TestEqual(TEXT("it names the call node's GUID"), Call->Node.Len(), 32);
			}
			const FN2CDependencyEdge* Variable = Index.Edges.FindByPredicate([A, S](const FN2CDependencyEdge& Edge)
			{
				return Edge.From == A && Edge.To == S && Edge.Kind == EN2CDependencyEdgeKind::VariableType;
			});
			if (TestNotNull(TEXT("the variable edge"), Variable))
			{
				TestEqual(TEXT("it names the variable"), Variable->Node, FString(TEXT("Data")));
			}
		});

		It(TEXT("orders dependencies first and reports {B, C} as the one cycle group"), [this]()
		{
			const FN2CDependencyIndex Index = BuildFixtures(true);
			const int32 E = NodeOf(Index, EnumName), S = NodeOf(Index, StructName), I = NodeOf(Index, InterfaceName);
			const int32 M = NodeOf(Index, MacroLibraryName), A = NodeOf(Index, AName), B = NodeOf(Index, BName);
			const int32 C = NodeOf(Index, CName), Child = NodeOf(Index, ChildName);
			if (!TestEqual(TEXT("every node once in the order"), Index.Order.Num(), Index.Nodes.Num()))
			{
				return;
			}
			TestTrue(TEXT("the enum before the struct"), Position(Index, E) < Position(Index, S));
			TestTrue(TEXT("the struct before A"), Position(Index, S) < Position(Index, A));
			TestTrue(TEXT("the interface before A"), Position(Index, I) < Position(Index, A));
			TestTrue(TEXT("the macro library before A"), Position(Index, M) < Position(Index, A));
			TestTrue(TEXT("B and C before A"), Position(Index, B) < Position(Index, A) && Position(Index, C) < Position(Index, A));
			TestTrue(TEXT("A before its child"), Position(Index, A) < Position(Index, Child));
			TestEqual(TEXT("B and C next to each other"), FMath::Abs(Position(Index, B) - Position(Index, C)), 1);
			if (TestEqual(TEXT("one cycle group"), Index.CycleGroups.Num(), 1))
			{
				TArray<int32> Expected = { B, C };
				Expected.Sort([&Index](int32 X, int32 Y) { return Index.Nodes[X].Path < Index.Nodes[Y].Path; });
				TestEqual(TEXT("the cycle group is {B, C}"), Index.CycleGroups[0], Expected);
			}
		});

		It(TEXT("keeps only the AssetRegistry edges without loading"), [this]()
		{
			const FN2CDependencyIndex Index = BuildFixtures(false);
			const int32 A = NodeOf(Index, AName), B = NodeOf(Index, BName), I = NodeOf(Index, InterfaceName), Child = NodeOf(Index, ChildName);
			TestFalse(TEXT("no call edges"), Index.Edges.ContainsByPredicate([](const FN2CDependencyEdge& Edge)
			{
				return Edge.Kind == EN2CDependencyEdgeKind::Calls || Edge.Kind == EN2CDependencyEdgeKind::Macro
					|| Edge.Kind == EN2CDependencyEdgeKind::Cast || Edge.Kind == EN2CDependencyEdgeKind::VariableType;
			}));
			ExpectEdge(Index, A, B, EN2CDependencyEdgeKind::HardPackage, TEXT("A loads B"));
			ExpectEdge(Index, A, I, EN2CDependencyEdgeKind::Interface, TEXT("A implements the interface (tag)"));
			ExpectEdge(Index, Child, A, EN2CDependencyEdgeKind::Parent, TEXT("the child's parent is A (tag)"));
			TestEqual(TEXT("still the one cycle group"), Index.CycleGroups.Num(), 1);
		});

		It(TEXT("takes a single asset path as a root, pulls in what it depends on, and warns about a root that names nothing"), [this]()
		{
			const FN2CDependencyIndex Index = FN2CDependencyIndex::Build({ FString::Printf(TEXT("%s/%s"), FolderPath, AName), TEXT("/Game/N2CNoSuchFolder") });
			const int32 A = NodeOf(Index, AName), S = NodeOf(Index, StructName), B = NodeOf(Index, BName);
			if (A != INDEX_NONE && S != INDEX_NONE && B != INDEX_NONE)
			{
				TestTrue(TEXT("A is the root"), Index.Nodes[A].bRoot);
				TestFalse(TEXT("the struct was pulled in"), Index.Nodes[S].bRoot);
				TestFalse(TEXT("B was pulled in"), Index.Nodes[B].bRoot);
			}
			TestEqual(TEXT("the child is not indexed (A does not depend on it)"), Index.FindNode(ObjectPath(ChildName)), INDEX_NONE);
			TestTrue(TEXT("a warning names the empty root"), Index.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("N2CNoSuchFolder")); }));
		});
	});

	Describe(TEXT("ComputeOrder"), [this]()
	{
		It(TEXT("puts kinds first among ready nodes, never before their dependencies, and keeps a cycle together"), [this]()
		{
			FN2CDependencyIndex Index;
			auto Add = [&Index](const TCHAR* Path, EN2CDependencyNodeKind Kind)
			{
				FN2CDependencyNode& Node = Index.Nodes.AddDefaulted_GetRef();
				Node.Path = Path;
				Node.Kind = Kind;
				return Index.Nodes.Num() - 1;
			};
			auto Edge = [&Index](int32 From, int32 To)
			{
				FN2CDependencyEdge& New = Index.Edges.AddDefaulted_GetRef();
				New.From = From;
				New.To = To;
			};
			const int32 Leaf = Add(TEXT("/Game/A_Leaf.A_Leaf"), EN2CDependencyNodeKind::Blueprint);
			const int32 Struct = Add(TEXT("/Game/Z_Struct.Z_Struct"), EN2CDependencyNodeKind::Struct);
			const int32 X = Add(TEXT("/Game/X.X"), EN2CDependencyNodeKind::Blueprint);
			const int32 Y = Add(TEXT("/Game/Y.Y"), EN2CDependencyNodeKind::Blueprint);
			const int32 Late = Add(TEXT("/Game/Late_Struct.Late_Struct"), EN2CDependencyNodeKind::Struct);
			Edge(X, Y);
			Edge(Y, X);
			Edge(X, Struct);
			Edge(Late, Leaf);  // a struct with a member of a Blueprint's class comes after that Blueprint
			Index.ComputeOrder();
			TestEqual(TEXT("order"), Index.Order, TArray<int32>({ Struct, Leaf, Late, X, Y }));
			if (TestEqual(TEXT("one cycle group"), Index.CycleGroups.Num(), 1))
			{
				TestEqual(TEXT("{X, Y}"), Index.CycleGroups[0], TArray<int32>({ X, Y }));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
