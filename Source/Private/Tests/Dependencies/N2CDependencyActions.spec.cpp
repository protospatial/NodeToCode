// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/Dependencies/N2CDependencyFixtures.h"
#include "Core/N2CDependencyActions.h"
#include "Core/N2CGraphStateManager.h"
#include "UObject/StrongObjectPtr.h"

/**
 * The tag manager's dependency actions (card 0319) on the checked-in dependency fixtures (N2CDependencyFixtures.h):
 * Tag Dependencies from BP_N2CDep_A tags the graphs of what A depends on (B, and C through B; the interface; the macro
 * library) and lists the struct and enum, which have no graph, while A itself and BP_N2CDep_Child (which depends on A,
 * not the other way round) stay untagged. Every case tags into an isolated graph state store in a scratch folder, so
 * the host's BlueprintGraphState.json is never touched. Opening the Reference Viewer is checked only up to the package
 * names it would open (the real viewer is a tab, which a headless test editor should not open).
 */
BEGIN_DEFINE_SPEC(FN2CDependencyActionsSpec, "NodeToCode.Dependencies.Actions", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	TStrongObjectPtr<UN2CGraphStateManager> Store;

	/** The fixture Blueprints (by asset name) that have at least one graph tagged Tag in the store */
	TSet<FString> TaggedAssets(const FString& Tag) const
	{
		TSet<FString> Names;
		for (const FN2CGraphState& Graph : Store->GetGraphsWithTag(Tag))
		{
			Names.Add(Graph.OwningBlueprint.GetAssetName());
		}
		return Names;
	}
END_DEFINE_SPEC(FN2CDependencyActionsSpec)

void FN2CDependencyActionsSpec::Define()
{
	using namespace N2CTest::DependencyFixtures;

	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CDependencyActions"));
		Store.Reset(UN2CGraphStateManager::CreateIsolated(Scratch->Path));
	});

	AfterEach([this]()
	{
		Store.Reset();
		Scratch.Reset();
	});

	It(TEXT("tags the graphs of everything BP_N2CDep_A depends on, and only those"), [this]()
	{
		const FN2CDependencyTagResult Result = FN2CDependencyActions::TagDependencies({ ObjectPath(AName) },
			TEXT("Migrate"), TEXT("Plan"), *Store);

		// B, C, the interface, the macro library, the struct and the enum
		TestEqual(TEXT("six dependencies (Tests/NodeToCodeTestContent is junctioned into the host by build.ps1)"), Result.Dependencies, 6);
		const TSet<FString> Tagged = TaggedAssets(TEXT("Migrate"));
		TestTrue(TEXT("BP_N2CDep_B is tagged (A calls its DoB)"), Tagged.Contains(BName));
		TestTrue(TEXT("BP_N2CDep_C is tagged (through B, which references C)"), Tagged.Contains(CName));
		TestTrue(TEXT("the macro library is tagged (A runs DepMacro)"), Tagged.Contains(MacroLibraryName));
		TestFalse(TEXT("the root BP_N2CDep_A is not tagged"), Tagged.Contains(AName));
		TestFalse(TEXT("BP_N2CDep_Child depends on A, so it is not a dependency"), Tagged.Contains(ChildName));
		TestTrue(TEXT("the struct has no graph to tag"), Result.Untagged.Contains(StructName));
		TestTrue(TEXT("the enum has no graph to tag"), Result.Untagged.Contains(EnumName));
		TestEqual(TEXT("TaggedBlueprints counts the Blueprints the store shows with the tag"), Result.TaggedBlueprints, Tagged.Num());
		TestEqual(TEXT("TaggedGraphs counts the graphs the store shows with the tag"), Result.TaggedGraphs, Store->GetGraphsWithTag(TEXT("Migrate")).Num());
		for (const FN2CGraphState& Graph : Store->GetGraphsWithTag(TEXT("Migrate")))
		{
			TestTrue(FString::Printf(TEXT("%s keeps the chosen category"), *Graph.GraphName), Store->GetGraphsWithTag(TEXT("Migrate"), TEXT("Plan")).ContainsByPredicate(
				[&Graph](const FN2CGraphState& Other) { return Other.GraphName == Graph.GraphName && Other.OwningBlueprint == Graph.OwningBlueprint; }));
		}
	});

	It(TEXT("is idempotent: a second run tags the same graphs and adds none"), [this]()
	{
		const FN2CDependencyTagResult First = FN2CDependencyActions::TagDependencies({ ObjectPath(AName) }, TEXT("Migrate"), TEXT("Plan"), *Store);
		const int32 After = Store->GetGraphsWithTag(TEXT("Migrate")).Num();
		const FN2CDependencyTagResult Second = FN2CDependencyActions::TagDependencies({ ObjectPath(AName) }, TEXT("Migrate"), TEXT("Plan"), *Store);
		TestEqual(TEXT("the second run reports the same graphs"), Second.TaggedGraphs, First.TaggedGraphs);
		TestEqual(TEXT("and the store holds no more of them"), Store->GetGraphsWithTag(TEXT("Migrate")).Num(), After);
	});

	It(TEXT("tags nothing without a tag or without roots"), [this]()
	{
		const FN2CDependencyTagResult NoTag = FN2CDependencyActions::TagDependencies({ ObjectPath(AName) }, TEXT("  "), TEXT("Plan"), *Store);
		TestEqual(TEXT("a blank tag tags nothing"), NoTag.TaggedGraphs, 0);
		const FN2CDependencyTagResult NoRoots = FN2CDependencyActions::TagDependencies({}, TEXT("Migrate"), TEXT("Plan"), *Store);
		TestEqual(TEXT("no roots, no dependencies"), NoRoots.Dependencies, 0);
		TestEqual(TEXT("and the store stays empty"), Store->GetGraphsWithTag(TEXT("Migrate")).Num(), 0);
	});

	It(TEXT("names the packages the Reference Viewer opens, once each"), [this]()
	{
		const TArray<FName> Packages = FN2CDependencyActions::ToPackageNames({ ObjectPath(AName), FString(FolderPath) / BName,
			ObjectPath(AName), TEXT(""), TEXT("not a path") });
		TestEqual(TEXT("two packages"), Packages.Num(), 2);
		if (Packages.Num() == 2)
		{
			TestEqual(TEXT("an object path becomes its package"), Packages[0].ToString(), FString(FolderPath) / AName);
			TestEqual(TEXT("a package path stays as it is"), Packages[1].ToString(), FString(FolderPath) / BName);
		}
		TestFalse(TEXT("nothing to open is false, and opens nothing"), FN2CDependencyActions::OpenReferenceViewer({ TEXT("not a path") }));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
