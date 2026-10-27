// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CTagEventsTest, "NodeToCode.Robustness.TagEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// editor-integration-14: the tag-added event carries the tag just added, not the graph's first tag.
// editor-integration-23: an empty category is stored as Default and is not a wildcard in AddTag's duplicate check.
bool FN2CTagEventsTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
		TEXT("N2CTests"), FString::Printf(TEXT("TagEvents-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FSoftObjectPath Blueprint(TEXT("/Game/N2CTest/BP_Tags.BP_Tags"));

	{
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));

		// The store's tag-added event carries the added tag and its graph (card 0345)
		TArray<FN2CTaggedBlueprintGraph> Events;
		Manager->OnGraphTagAdded.AddLambda([&Events](const FN2CTagEntry& Tag, const FN2CGraphRef& Graph)
		{
			Events.Add(Graph.MakeTaggedGraph(Tag));
		});

		// 1. Adding Combat then AI to one graph reports AI for the second add
		const FGuid EventGraph = FGuid::NewGuid();
		Manager->AddTag(EventGraph, TEXT("EventGraph"), Blueprint, TEXT("Combat"), TEXT("Default"));
		Manager->AddTag(EventGraph, TEXT("EventGraph"), Blueprint, TEXT("AI"), TEXT("Default"));
		if (TestEqual(TEXT("two tag-added events"), Events.Num(), 2))
		{
			TestEqual(TEXT("the first event reports Combat"), Events[0].Tag, FString(TEXT("Combat")));
			TestEqual(TEXT("the second event reports AI, the tag just added"), Events[1].Tag, FString(TEXT("AI")));
			TestEqual(TEXT("the second event names the graph"), Events[1].GraphGuid, EventGraph);
		}

		// 2. Combat with an empty category on a graph that has Combat/AI adds Combat/Default
		const FGuid CategoryGraph = FGuid::NewGuid();
		Manager->AddTag(CategoryGraph, TEXT("CategoryGraph"), Blueprint, TEXT("Combat"), TEXT("AI"));
		Events.Reset();
		TestTrue(TEXT("AddTag with an empty category succeeds"),
			Manager->AddTag(CategoryGraph, TEXT("CategoryGraph"), Blueprint, TEXT("Combat"), TEXT("")));

		const TArray<FN2CTagEntry> Tags = Manager->GetTagsForGraph(CategoryGraph);
		TestEqual(TEXT("the graph now has two tags"), Tags.Num(), 2);
		TestTrue(TEXT("a Combat/Default entry exists"), Tags.ContainsByPredicate([](const FN2CTagEntry& Entry)
		{
			return Entry.Tag == TEXT("Combat") && Entry.Category == TEXT("Default");
		}));
		TestFalse(TEXT("no tag is stored with a blank category"), Tags.ContainsByPredicate([](const FN2CTagEntry& Entry)
		{
			return Entry.Category.TrimStartAndEnd().IsEmpty();
		}));
		if (TestEqual(TEXT("the empty-category add sent one event"), Events.Num(), 1))
		{
			TestEqual(TEXT("the event reports category Default"), Events[0].Category, FString(TEXT("Default")));
		}

		// 3. Adding Combat/Default again is an exact duplicate: still success, nothing added, no event
		Events.Reset();
		TestTrue(TEXT("re-adding Combat with a blank category succeeds"),
			Manager->AddTag(CategoryGraph, TEXT("CategoryGraph"), Blueprint, TEXT("Combat"), TEXT("  ")));
		TestEqual(TEXT("still two tags"), Manager->GetTagsForGraph(CategoryGraph).Num(), 2);
		TestEqual(TEXT("no event for a duplicate"), Events.Num(), 0);

		// 4. Remove keeps its wildcard: an empty category removes a Combat tag in any category
		TestTrue(TEXT("RemoveTag with an empty category still matches any category"),
			Manager->RemoveTag(CategoryGraph, TEXT("Combat"), TEXT("")));
		TestEqual(TEXT("one tag left"), Manager->GetTagsForGraph(CategoryGraph).Num(), 1);
	}

	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
