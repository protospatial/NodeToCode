// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Ticker.h"
#include "Core/N2CGraphStateManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PythonScriptTypes.h"
#include "Subsystems/N2CGraphStateSubsystem.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"

/**
 * The tag store's lifetime and its writes (card 0346). Before, UN2CGraphStateManager was a rooted singleton behind a
 * static pointer that nothing released, and every change rewrote BlueprintGraphState.json at once (100 tags, 100
 * writes). Now UN2CGraphStateSubsystem owns the editor's store and flushes it in Deinitialize, and a store with a save
 * delay writes once per delay however many changes come in. The write cases use isolated managers in scratch folders
 * (CreateIsolated, with a short delay set), so the host project's file is untouched; the corrupt-file backup is
 * NodeToCode.GraphState.Persistence's.
 */
BEGIN_DEFINE_SPEC(FN2CGraphStateSubsystemSpec, "NodeToCode.Core.GraphStateSubsystem", N2C_TEST_FLAGS)

	const FSoftObjectPath Blueprint = FSoftObjectPath(TEXT("/Game/N2CTest/BP_N2CStateSubsystem.BP_N2CStateSubsystem"));

	/** Ticks the core ticker (which runs delayed saves) for Seconds or until Until */
	static void Pump(double Seconds, TFunctionRef<bool()> Until)
	{
		const double End = FPlatformTime::Seconds() + Seconds;
		double Last = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() < End && !Until())
		{
			FPlatformProcess::Sleep(0.005f);
			const double Now = FPlatformTime::Seconds();
			FTSTicker::GetCoreTicker().Tick(static_cast<float>(Now - Last));
			Last = Now;
		}
	}

	void AddTags(UN2CGraphStateManager& Manager, int32 Count, const FString& Tag)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Manager.AddTag(FGuid::NewGuid(), FString::Printf(TEXT("Graph%d"), Index), Blueprint, Tag, TEXT("Default"));
		}
	}

	int32 TagsOnDisk(const UN2CGraphStateManager& Manager, const FString& Tag) const
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Manager.GetStateFilePath());
		int32 Count = 0;
		const FString Needle = FString::Printf(TEXT("\"%s\""), *Tag);
		for (int32 At = Text.Find(Needle); At != INDEX_NONE; At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + 1))
		{
			++Count;
		}
		return Count;
	}

END_DEFINE_SPEC(FN2CGraphStateSubsystemSpec)

void FN2CGraphStateSubsystemSpec::Define()
{
	It(TEXT("writes 100 tags added in a row once, after the save delay"), [this]()
	{
		N2CTest::FScratchDir Dir(TEXT("N2CStateDebounce"));
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir.Path));
		Manager->SetSaveDelay(0.2f);
		const int32 Before = Manager->GetSaveCount();

		AddTags(*Manager, 100, TEXT("N2CDebounceTag"));
		TestEqual(TEXT("nothing written while the tags come in"), Manager->GetSaveCount() - Before, 0);
		TestTrue(TEXT("a save is waiting"), Manager->HasPendingSave());
		TestTrue(TEXT("the store has unsaved changes"), Manager->IsDirty());

		Pump(3.0, [&Manager] { return !Manager->HasPendingSave(); });
		const int32 Writes = Manager->GetSaveCount() - Before;
		TestTrue(FString::Printf(TEXT("a few writes at most (%d)"), Writes), Writes >= 1 && Writes <= 3);
		TestEqual(TEXT("exactly one, in fact"), Writes, 1);
		TestFalse(TEXT("nothing left unsaved"), Manager->IsDirty());
		TestEqual(TEXT("the file holds all 100 tags"), TagsOnDisk(*Manager, TEXT("N2CDebounceTag")), 100);
	});

	It(TEXT("writes a waiting change at once on an explicit save, on reload and on shutdown"), [this]()
	{
		N2CTest::FScratchDir Dir(TEXT("N2CStateFlush"));
		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir.Path));
		Manager->SetSaveDelay(30.0f); // longer than the case: only a flush can write

		AddTags(*Manager, 3, TEXT("N2CFlushSave"));
		TestTrue(TEXT("SaveState writes"), Manager->SaveState());
		TestFalse(TEXT("and the waiting save is dropped"), Manager->HasPendingSave());
		TestEqual(TEXT("saved"), TagsOnDisk(*Manager, TEXT("N2CFlushSave")), 3);

		AddTags(*Manager, 2, TEXT("N2CFlushReload"));
		Manager->LoadState();
		TestEqual(TEXT("a reload writes the waiting change first: both tags are on disk"), TagsOnDisk(*Manager, TEXT("N2CFlushReload")), 2);
		TestEqual(TEXT("and in the store"), Manager->GetAllTags().FilterByPredicate([](const FN2CTaggedBlueprintGraph& Tag)
			{ return Tag.Tag == TEXT("N2CFlushReload"); }).Num(), 2);

		AddTags(*Manager, 4, TEXT("N2CFlushShutdown"));
		Manager->Shutdown();
		TestFalse(TEXT("no save waits after shutdown"), Manager->HasPendingSave());
		TestEqual(TEXT("shutdown wrote the waiting change"), TagsOnDisk(*Manager, TEXT("N2CFlushShutdown")), 4);
	});

	It(TEXT("lives in the editor subsystem: no rooted singleton, delayed saves, Deinitialize-style flush through SaveNow"), [this]()
	{
		UN2CGraphStateSubsystem* Subsystem = UN2CGraphStateSubsystem::Get();
		if (!TestNotNull(TEXT("the graph state subsystem exists"), Subsystem))
		{
			return;
		}
		UN2CGraphStateManager* Manager = Subsystem->GetManager();
		if (!TestNotNull(TEXT("it holds a store"), Manager))
		{
			return;
		}
		TestTrue(TEXT("UN2CGraphStateManager::Get() is the subsystem's store"), &UN2CGraphStateManager::Get() == Manager);
		TestTrue(TEXT("GetIfExists too"), UN2CGraphStateManager::GetIfExists() == Manager);
		TestFalse(TEXT("the store is not rooted"), Manager->IsRooted());
		TestTrue(TEXT("its outer is the subsystem"), Manager->GetOuter() == Subsystem);
		TestEqual(TEXT("its saves are delayed"), Manager->GetSaveDelay(), UN2CGraphStateSubsystem::EditorSaveDelaySeconds);
		TestTrue(TEXT("SaveNow leaves nothing unsaved"), Subsystem->SaveNow() && !Subsystem->HasUnsavedChanges());

		// A spec's isolated store takes its place, and the subsystem's own comes back
		N2CTest::FScratchDir Dir(TEXT("N2CStateOverride"));
		TStrongObjectPtr<UN2CGraphStateManager> Isolated(UN2CGraphStateManager::CreateIsolated(Dir.Path));
		Subsystem->SetManagerOverride(Isolated.Get());
		TestTrue(TEXT("the override is the store"), &UN2CGraphStateManager::Get() == Isolated.Get());
		Subsystem->SetManagerOverride(nullptr);
		TestTrue(TEXT("and the subsystem's own again"), &UN2CGraphStateManager::Get() == Manager);
	});

	It(TEXT("works from Python through unreal.get_editor_subsystem"), [this]()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (!Python || !Python->IsPythonAvailable())
		{
			AddInfo(TEXT("Python is not available; skipping"));
			return;
		}
		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile; // literal code, several statements
		Command.Command = TEXT(
			"import unreal\n"
			"s = unreal.get_editor_subsystem(unreal.N2CGraphStateSubsystem)\n"
			"assert s is not None, 'no subsystem'\n"
			"assert s.get_state_file_path().endswith('BlueprintGraphState.json'), s.get_state_file_path()\n"
			"assert isinstance(s.get_tag_count(), int)\n"
			"assert s.save_now()\n"
			"assert not s.has_unsaved_changes()\n");
		const bool bRan = Python->ExecPythonCommandEx(Command);
		TestTrue(FString::Printf(TEXT("the Python checks pass (%s)"), *Command.CommandResult.Left(1000)), bRan);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
