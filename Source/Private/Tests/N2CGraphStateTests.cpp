// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CGraphStateManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CFileUtils.h"

namespace N2CGraphStateTests
{
	FString MakeTempDir(const TCHAR* Name)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(),
			TEXT("N2CTests"), FString::Printf(TEXT("%s-%s"), Name, *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
		IFileManager::Get().MakeDirectory(*Dir, true);
		return Dir;
	}

	TArray<FString> CorruptCopies(const FString& StateFile)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(StateFile + TEXT(".corrupt-*")), true, false);
		return Found;
	}

	FString LegacyTagJson(const FGuid& Guid, const TCHAR* GraphName, const TCHAR* Tag)
	{
		return FString::Printf(
			TEXT("{\"tag\":\"%s\",\"category\":\"Default\",\"description\":\"\",\"graphGuid\":\"%s\",\"graphName\":\"%s\",\"owningBlueprint\":\"/Game/N2CTest/BP_Legacy.BP_Legacy\"}"),
			Tag, *Guid.ToString(EGuidFormats::DigitsWithHyphens), GraphName);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CGraphStatePersistenceTest, "NodeToCode.Robustness.GraphStatePersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CGraphStatePersistenceTest::RunTest(const FString& Parameters)
{
	using namespace N2CGraphStateTests;
	const FSoftObjectPath Blueprint(TEXT("/Game/N2CTest/BP_State.BP_State"));
	TArray<FString> TempDirs;

	AddExpectedError(TEXT("could not read its tag and translation history file"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedMessage(TEXT("Graph state was not saved"), EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Finished an interrupted save"), EAutomationExpectedMessageFlags::Contains, 1);

	// 1. A truncated state file is set aside, never overwritten, and the legacy tags are not migrated over it
	{
		const FString Dir = MakeTempDir(TEXT("Corrupt"));
		TempDirs.Add(Dir);
		const FString StateFile = FPaths::Combine(Dir, TEXT("BlueprintGraphState.json"));
		const FString Truncated = TEXT("{\"version\":\"2.0\",\"graphs\":[{\"graphGuid\":\"0123");
		FFileHelper::SaveStringToFile(Truncated, *StateFile);
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"tags\":[%s]}"),
			*LegacyTagJson(FGuid::NewGuid(), TEXT("OldGraph"), TEXT("Stale"))), *FPaths::Combine(Dir, TEXT("Tags"), TEXT("BlueprintTags.json")));

		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));
		TestTrue(TEXT("saving is blocked after a parse failure"), Manager->IsSavingBlocked());
		TestEqual(TEXT("the stale legacy tags were not migrated"), Manager->GetAllGraphStates().Num(), 0);
		TestFalse(TEXT("the state file was moved away"), FPaths::FileExists(StateFile));

		const TArray<FString> Copies = CorruptCopies(StateFile);
		if (TestEqual(TEXT("one .corrupt- copy exists"), Copies.Num(), 1))
		{
			const FString CorruptPath = FPaths::Combine(Dir, Copies[0]);
			TestEqual(TEXT("the manager reports the copy"), FPaths::ConvertRelativePathToFull(Manager->GetQuarantinedFilePath()), CorruptPath);

			// A tag change afterwards must not write anything
			Manager->AddTag(FGuid::NewGuid(), TEXT("NewGraph"), Blueprint, TEXT("Fresh"), TEXT("Default"));
			FString CorruptContent;
			FFileHelper::LoadFileToString(CorruptContent, *CorruptPath);
			TestEqual(TEXT("the .corrupt- copy is unchanged after a tag add"), CorruptContent, Truncated);
			TestFalse(TEXT("no new state file was written over it"), FPaths::FileExists(StateFile));
			TestEqual(TEXT("still one .corrupt- copy"), CorruptCopies(StateFile).Num(), 1);
			TestEqual(TEXT("nothing was saved"), Manager->GetSaveCount(), 0);
		}
		TestTrue(TEXT("the legacy file was left alone"), FPaths::FileExists(FPaths::Combine(Dir, TEXT("Tags"), TEXT("BlueprintTags.json"))));

		// Reloading turns saving back on
		Manager->LoadState();
		TestFalse(TEXT("saving is on after a reload"), Manager->IsSavingBlocked());
		Manager->AddTag(FGuid::NewGuid(), TEXT("AfterReload"), Blueprint, TEXT("Fresh"), TEXT("Default"));
		TestTrue(TEXT("a tag add after the reload is saved"), FPaths::FileExists(StateFile));
	}

	// 2. ClearAllTags removes every tag in one pass, saves once and broadcasts once
	{
		const FString Dir = MakeTempDir(TEXT("ClearAll"));
		TempDirs.Add(Dir);
		const FString StateFile = FPaths::Combine(Dir, TEXT("BlueprintGraphState.json"));
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));

		for (int32 i = 0; i < 3; ++i)
		{
			Manager->AddTag(FGuid::NewGuid(), FString::Printf(TEXT("Graph%d"), i), Blueprint, TEXT("Tagged"), TEXT("Default"));
		}
		const FGuid TranslatedGuid = FGuid::NewGuid();
		Manager->SetTranslationState(TranslatedGuid, TEXT("Translated"), Blueprint, TEXT("Saved/NodeToCode/Translations/x"),
			TEXT("Anthropic"), TEXT("model"), TEXT("CPP"), FN2CTranslationSummary());
		Manager->AddTag(TranslatedGuid, TEXT("Translated"), Blueprint, TEXT("Tagged"), TEXT("Default"));
		TestEqual(TEXT("four graphs before Clear All"), Manager->GetAllGraphStates().Num(), 4);
		TestTrue(TEXT("the previous file is kept as .bak"), FPaths::FileExists(FN2CFileUtils::BackupPathFor(StateFile)));
		TestFalse(TEXT("no temporary file is left"), FPaths::FileExists(FN2CFileUtils::TempPathFor(StateFile)));

		int32 Reloads = 0;
		int32 PerTagEvents = 0;
		Manager->OnGraphStatesReloaded.AddLambda([&Reloads]() { ++Reloads; });
		Manager->OnGraphTagRemoved.AddLambda([&PerTagEvents](const FGuid&, const FString&) { ++PerTagEvents; });

		const int32 SavesBefore = Manager->GetSaveCount();
		const int32 Removed = Manager->ClearAllTags();
		TestEqual(TEXT("all 4 tags removed"), Removed, 4);
		TestEqual(TEXT("one save"), Manager->GetSaveCount() - SavesBefore, 1);
		TestEqual(TEXT("one reload broadcast"), Reloads, 1);
		TestEqual(TEXT("no per-tag broadcasts"), PerTagEvents, 0);
		TestEqual(TEXT("only the translated graph is left"), Manager->GetAllGraphStates().Num(), 1);
		TestEqual(TEXT("it has no tags"), Manager->GetTagsForGraph(TranslatedGuid).Num(), 0);
		TestTrue(TEXT("it keeps its translation"), Manager->HasTranslation(TranslatedGuid));

		FString OnDisk;
		FN2CGraphStateFile Parsed;
		TestTrue(TEXT("the saved file parses"), FFileHelper::LoadFileToString(OnDisk, *StateFile) && FN2CGraphStateFile::FromJsonString(OnDisk, Parsed));
		TestEqual(TEXT("the saved file has one graph"), Parsed.Graphs.Num(), 1);
	}

	// 3. Legacy migration saves once and renames the legacy file
	{
		const FString Dir = MakeTempDir(TEXT("Migrate"));
		TempDirs.Add(Dir);
		const FString LegacyFile = FPaths::Combine(Dir, TEXT("Tags"), TEXT("BlueprintTags.json"));
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"tags\":[%s,%s,%s]}"),
			*LegacyTagJson(FGuid::NewGuid(), TEXT("A"), TEXT("One")),
			*LegacyTagJson(FGuid::NewGuid(), TEXT("B"), TEXT("Two")),
			*LegacyTagJson(FGuid::NewGuid(), TEXT("C"), TEXT("Three"))), *LegacyFile);

		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));
		TestEqual(TEXT("three legacy graphs migrated"), Manager->GetAllGraphStates().Num(), 3);
		TestEqual(TEXT("the migration saved once"), Manager->GetSaveCount(), 1);
		TestFalse(TEXT("the legacy file was renamed"), FPaths::FileExists(LegacyFile));
		TestTrue(TEXT("to .migrated"), FPaths::FileExists(LegacyFile + TEXT(".migrated")));
	}

	// 4. A save interrupted after the temporary file was complete is finished on the next load
	{
		const FString Dir = MakeTempDir(TEXT("Interrupted"));
		TempDirs.Add(Dir);
		const FString StateFile = FPaths::Combine(Dir, TEXT("BlueprintGraphState.json"));
		FN2CGraphStateFile Good;
		FN2CGraphState Graph;
		Graph.GraphGuid = FGuid::NewGuid();
		Graph.GraphName = TEXT("FromTemp");
		Good.Graphs.Add(Graph);
		FFileHelper::SaveStringToFile(Good.ToJsonString(true), *FN2CFileUtils::TempPathFor(StateFile));

		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));
		TestEqual(TEXT("the graph in the temporary file was loaded"), Manager->GetAllGraphStates().Num(), 1);
		TestTrue(TEXT("the temporary file became the state file"), FPaths::FileExists(StateFile));
		TestFalse(TEXT("and is gone"), FPaths::FileExists(FN2CFileUtils::TempPathFor(StateFile)));
	}

	for (const FString& Dir : TempDirs)
	{
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
