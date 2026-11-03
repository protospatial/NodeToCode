// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CGraphStateManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

/**
 * Two editors on one project (card 0436). Each manager read BlueprintGraphState.json once and wrote its whole state
 * on every save, so the last writer silently dropped what another editor (a second instance, a headless editor next
 * to the user's) had written since. Now a save that finds the file changed since this manager last read or wrote it
 * merges: the other writer's graphs and tags stay, and this manager's own additions and removals apply on top.
 * Two isolated managers on one scratch folder stand for the two editors; one case edits the file directly.
 */
BEGIN_DEFINE_SPEC(FN2CGraphStateMergeSpec, "NodeToCode.Core.GraphStateMerge", N2C_TEST_FLAGS)

	const FSoftObjectPath Blueprint = FSoftObjectPath(TEXT("/Game/N2CTest/BP_N2CMerge.BP_N2CMerge"));

	/** The tag names on the file (all graphs), sorted */
	static TArray<FString> TagsOnDisk(const FString& Path)
	{
		FString Text;
		FN2CGraphStateFile File;
		TArray<FString> Names;
		if (FFileHelper::LoadFileToString(Text, *Path) && FN2CGraphStateFile::FromJsonString(Text, File))
		{
			for (const FN2CGraphState& State : File.Graphs)
			{
				for (const FN2CTagEntry& Tag : State.Tags)
				{
					Names.Add(Tag.Tag);
				}
			}
		}
		Names.Sort();
		return Names;
	}

	static FString Join(const TArray<FString>& Names)
	{
		return FString::Join(Names, TEXT(","));
	}

END_DEFINE_SPEC(FN2CGraphStateMergeSpec)

void FN2CGraphStateMergeSpec::Define()
{
	It(TEXT("keeps both editors' tags, and each editor's removals, whoever writes last"), [this]()
	{
		N2CTest::FScratchDir Dir(TEXT("N2CStateMerge"));
		// Both editors start on the same (empty) project
		TStrongObjectPtr<UN2CGraphStateManager> A(UN2CGraphStateManager::CreateIsolated(Dir.Path));
		TStrongObjectPtr<UN2CGraphStateManager> B(UN2CGraphStateManager::CreateIsolated(Dir.Path));
		const FString Path = A->GetStateFilePath();
		const FGuid One = FGuid::NewGuid();
		const FGuid Two = FGuid::NewGuid();

		A->AddTag(One, TEXT("GraphOne"), Blueprint, TEXT("X"), TEXT("Default"));
		B->AddTag(Two, TEXT("GraphTwo"), Blueprint, TEXT("Y"), TEXT("Default"));
		TestEqual(TEXT("B's save keeps A's tag"), Join(TagsOnDisk(Path)), FString(TEXT("X,Y")));
		TestEqual(TEXT("B merged once"), B->GetMergeCount(), 1);
		TestTrue(TEXT("B now holds A's tag too"), B->GraphHasTag(FN2CGraphKey(Blueprint, One), TEXT("X")));

		A->AddTag(One, TEXT("GraphOne"), Blueprint, TEXT("Z"), TEXT("Default"));
		TestEqual(TEXT("A's next save keeps B's tag"), Join(TagsOnDisk(Path)), FString(TEXT("X,Y,Z")));

		B->RemoveTag(FN2CGraphKey(Blueprint, One), TEXT("X"), TEXT("Default"));
		TestEqual(TEXT("B removes A's X"), Join(TagsOnDisk(Path)), FString(TEXT("Y,Z")));

		A->AddTag(Two, TEXT("GraphTwo"), Blueprint, TEXT("W"), TEXT("Default"));
		TestEqual(TEXT("A's save does not bring back the X B removed"), Join(TagsOnDisk(Path)), FString(TEXT("W,Y,Z")));
		TestFalse(TEXT("and A no longer holds it"), A->GraphHasTag(FN2CGraphKey(Blueprint, One), TEXT("X")));

		// A save with nothing written in between merges nothing
		const int32 Merges = A->GetMergeCount();
		A->AddTag(Two, TEXT("GraphTwo"), Blueprint, TEXT("V"), TEXT("Default"));
		TestEqual(TEXT("no merge when the file is A's own"), A->GetMergeCount(), Merges);
		TestEqual(TEXT("V added"), Join(TagsOnDisk(Path)), FString(TEXT("V,W,Y,Z")));
	});

	It(TEXT("keeps a tag written into the file from outside between two changes"), [this]()
	{
		N2CTest::FScratchDir Dir(TEXT("N2CStateMergeFile"));
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir.Path));
		const FString Path = Manager->GetStateFilePath();
		const FGuid Graph = FGuid::NewGuid();
		Manager->AddTag(Graph, TEXT("Graph"), Blueprint, TEXT("First"), TEXT("Default"));

		// Another editor's write: the file with one more graph and one more tag on the existing one
		FString Text;
		FN2CGraphStateFile File;
		if (!TestTrue(TEXT("the file reads"), FFileHelper::LoadFileToString(Text, *Path) && FN2CGraphStateFile::FromJsonString(Text, File)))
		{
			return;
		}
		FN2CTagEntry Outside;
		Outside.Tag = TEXT("FromOutside");
		File.Graphs[0].Tags.Add(Outside);
		FN2CGraphState Other;
		Other.GraphGuid = FGuid::NewGuid();
		Other.GraphName = TEXT("OtherGraph");
		Other.OwningBlueprint = Blueprint;
		FN2CTagEntry OtherTag;
		OtherTag.Tag = TEXT("OtherEditor");
		Other.Tags.Add(OtherTag);
		File.Graphs.Add(Other);
		FFileHelper::SaveStringToFile(File.ToJsonString(true), *Path);

		int32 Reloads = 0;
		Manager->OnGraphStatesReloaded.AddLambda([&Reloads]() { ++Reloads; });
		Manager->AddTag(Graph, TEXT("Graph"), Blueprint, TEXT("Second"), TEXT("Default"));
		TestEqual(TEXT("every tag survives"), Join(TagsOnDisk(Path)), FString(TEXT("First,FromOutside,OtherEditor,Second")));
		TestEqual(TEXT("the manager now lists the other editor's graph"), Manager->GetAllGraphStates().Num(), 2);
		TestEqual(TEXT("and told its listeners to re-read"), Reloads, 1);
	});

	It(TEXT("MergeStates: their translation and our tag change on one graph both stay; a graph they removed stays removed"), [this]()
	{
		const FGuid Kept = FGuid::NewGuid();
		const FGuid Dropped = FGuid::NewGuid();
		FN2CGraphStateFile Base;
		for (const FGuid& Guid : { Kept, Dropped })
		{
			FN2CGraphState& State = Base.Graphs.AddDefaulted_GetRef();
			State.GraphGuid = Guid;
			State.OwningBlueprint = Blueprint;
			State.GraphName = TEXT("Graph");
			FN2CTagEntry Tag;
			Tag.Tag = TEXT("Base");
			State.Tags.Add(Tag);
		}

		FN2CGraphStateFile Ours = Base;
		FN2CTagEntry Added;
		Added.Tag = TEXT("Ours");
		Ours.Graphs[0].Tags.Add(Added);

		FN2CGraphStateFile Theirs = Base;
		Theirs.Graphs[0].Translation.bExists = true;
		Theirs.Graphs[0].Translation.Model = TEXT("their-model");
		Theirs.Graphs.RemoveAt(1);

		const FN2CGraphStateFile Merged = UN2CGraphStateManager::MergeStates(Base, Ours, Theirs);
		if (!TestEqual(TEXT("one graph"), Merged.Graphs.Num(), 1))
		{
			return;
		}
		TestTrue(TEXT("their translation"), Merged.Graphs[0].Translation.bExists && Merged.Graphs[0].Translation.Model == TEXT("their-model"));
		TestTrue(TEXT("our tag"), Merged.Graphs[0].HasTag(TEXT("Ours")));
		TestTrue(TEXT("the shared tag"), Merged.Graphs[0].HasTag(TEXT("Base")));
		TestEqual(TEXT("the graph they removed (and we did not change) stays removed"),
			Merged.Graphs.IndexOfByPredicate([&Dropped](const FN2CGraphState& State) { return State.GraphGuid == Dropped; }), static_cast<int32>(INDEX_NONE));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
