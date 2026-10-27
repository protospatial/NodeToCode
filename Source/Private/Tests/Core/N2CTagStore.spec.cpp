// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CTagTypes.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

/**
 * One tag store (card 0345): UN2CGraphStateManager holds every tag and is what the overlay, the tag panel, the Blueprint
 * library, the subsystem, the Python bridge and the MCP tag tools use (the UN2CTagManager facade, with its static
 * GetAllTags cache, is gone). Checks on an isolated store: the tag-added event carries the tag just added (not the
 * graph's oldest) and the graph with its Blueprint and name; a duplicate add is a no-op without an event; name,
 * category and per-graph queries return one row per (graph, tag); RemoveTagByName returns how many it removed and the
 * first of them; GetAllTags returns the store's current rows each call (no stale static copy). The legacy
 * BlueprintTags.json migration is NodeToCode.GraphState's.
 */
BEGIN_DEFINE_SPEC(FN2CTagStoreSpec, "NodeToCode.Tags.Store", N2C_TEST_FLAGS)
	N2CTest::FScratchDir* Dir = nullptr;
	TStrongObjectPtr<UN2CGraphStateManager> Store;
	const FSoftObjectPath Blueprint = FSoftObjectPath(TEXT("/Game/N2CTest/BP_TagStore.BP_TagStore"));
	const FSoftObjectPath Copy = FSoftObjectPath(TEXT("/Game/N2CTest/BP_TagStoreCopy.BP_TagStoreCopy"));
END_DEFINE_SPEC(FN2CTagStoreSpec)

void FN2CTagStoreSpec::Define()
{
	BeforeEach([this]()
	{
		Dir = new N2CTest::FScratchDir(TEXT("N2CTagStore"));
		Store.Reset(UN2CGraphStateManager::CreateIsolated(Dir->Path));
	});

	AfterEach([this]()
	{
		Store.Reset();
		delete Dir;
		Dir = nullptr;
	});

	It(TEXT("sends the added tag and its graph, and nothing for a duplicate"), [this]()
	{
		TArray<TPair<FN2CTagEntry, FN2CGraphRef>> Events;
		Store->OnGraphTagAdded.AddLambda([&Events](const FN2CTagEntry& Tag, const FN2CGraphRef& Graph)
		{
			Events.Emplace(Tag, Graph);
		});

		const FGuid Graph = FGuid::NewGuid();
		Store->AddTag(Graph, TEXT("EventGraph"), Blueprint, TEXT("Older"), TEXT("Default"));
		Store->AddTag(Graph, TEXT("EventGraph"), Blueprint, TEXT("Newer"), TEXT("Combat"), TEXT("why"));
		if (TestEqual(TEXT("two events"), Events.Num(), 2))
		{
			TestEqual(TEXT("the payload is the tag just added"), Events[1].Key.Tag, FString(TEXT("Newer")));
			TestEqual(TEXT("with its category"), Events[1].Key.Category, FString(TEXT("Combat")));
			TestEqual(TEXT("and description"), Events[1].Key.Description, FString(TEXT("why")));
			TestEqual(TEXT("the graph's GUID"), Events[1].Value.GraphGuid, Graph);
			TestEqual(TEXT("the graph's name"), Events[1].Value.GraphName, FString(TEXT("EventGraph")));
			TestTrue(TEXT("the graph's Blueprint"), Events[1].Value.Blueprint == FN2CGraphKey::NormalizeBlueprintPath(Blueprint));
		}

		Events.Reset();
		TestTrue(TEXT("a duplicate add succeeds"), Store->AddTag(Graph, TEXT("EventGraph"), Blueprint, TEXT("newer"), TEXT("combat")));
		TestEqual(TEXT("a duplicate adds nothing"), Store->GetTaggedGraphs(FN2CGraphKey(Blueprint, Graph)).Num(), 2);
		TestEqual(TEXT("and sends no event"), Events.Num(), 0);
	});

	It(TEXT("answers name, category and graph queries with one row per graph and tag"), [this]()
	{
		const FGuid A = FGuid::NewGuid();
		const FGuid B = FGuid::NewGuid();
		Store->AddTag(A, TEXT("GraphA"), Blueprint, TEXT("Movement"), TEXT("Player"));
		Store->AddTag(A, TEXT("GraphA"), Blueprint, TEXT("Input"), TEXT("Player"));
		Store->AddTag(B, TEXT("GraphB"), Blueprint, TEXT("Movement"), TEXT("AI"));
		// A copy of the Blueprint has the same graph GUID: its tags are its own rows
		Store->AddTag(A, TEXT("GraphA"), Copy, TEXT("Movement"), TEXT("Player"));

		TestEqual(TEXT("Movement in any category"), Store->FindTaggedGraphs(TEXT("movement")).Num(), 3);
		TestEqual(TEXT("Movement in Player"), Store->FindTaggedGraphs(TEXT("Movement"), TEXT("player")).Num(), 2);
		TestEqual(TEXT("the Player category"), Store->FindTaggedGraphsInCategory(TEXT("Player")).Num(), 3);
		TestEqual(TEXT("the AI category"), Store->FindTaggedGraphsInCategory(TEXT("ai")).Num(), 1);
		const TArray<FN2CTaggedBlueprintGraph> OfA = Store->GetTaggedGraphs(FN2CGraphKey(Blueprint, A));
		TestEqual(TEXT("graph A in the original"), OfA.Num(), 2);
		TestTrue(TEXT("rows name their Blueprint"), OfA.Num() == 2 && OfA[0].OwningBlueprint == FN2CGraphKey::NormalizeBlueprintPath(Blueprint));
		TestEqual(TEXT("every row"), Store->GetAllTags().Num(), 4);
		TestEqual(TEXT("unique names"), Store->GetAllTagNames().Num(), 2);
		TestEqual(TEXT("unique categories"), Store->GetAllCategories().Num(), 2);
	});

	It(TEXT("removes by name with a count and the first removed tag, and GetAllTags follows"), [this]()
	{
		const FGuid A = FGuid::NewGuid();
		const FN2CGraphKey Key(Blueprint, A);
		Store->AddTag(A, TEXT("GraphA"), Blueprint, TEXT("Combat"), TEXT("Player"), TEXT("first"));
		Store->AddTag(A, TEXT("GraphA"), Blueprint, TEXT("Combat"), TEXT("AI"));
		Store->AddTag(A, TEXT("GraphA"), Blueprint, TEXT("Other"), TEXT("Player"));
		TestEqual(TEXT("three rows before"), Store->GetAllTags().Num(), 3);

		FN2CTaggedBlueprintGraph Removed;
		TestEqual(TEXT("both Combat tags removed"), Store->RemoveTagByName(Key, TEXT("combat"), Removed), 2);
		TestEqual(TEXT("the first removed tag"), Removed.Category, FString(TEXT("Player")));
		TestEqual(TEXT("with its description"), Removed.Description, FString(TEXT("first")));
		TestEqual(TEXT("one row after: GetAllTags is the store now, not a cached copy"), Store->GetAllTags().Num(), 1);

		FN2CTaggedBlueprintGraph None;
		TestEqual(TEXT("nothing to remove"), Store->RemoveTagByName(Key, TEXT("Combat"), None), 0);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
