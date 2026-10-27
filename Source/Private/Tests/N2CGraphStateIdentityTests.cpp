// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestContentFolder.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Core/N2CGraphOverlayInjector.h"
#include "Core/N2CGraphStateManager.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CFileUtils.h"
#include "Widgets/Docking/SDockTab.h"

/**
 * Graph identity in the graph state (card 0192, review editor-integration-2, editor-integration-13,
 * imp-editor-integration-3, later-9). Duplicating a Blueprint copies UEdGraph::GraphGuid, so the state is keyed by
 * {Blueprint, GraphGuid}; the Asset Registry's rename and remove events keep entries on the Blueprint's current path.
 */
namespace N2CGraphIdentityTests
{
	const TCHAR* Folder = TEXT("/Game/__N2CTestIdentity");

	FString MakeTempDir(const TCHAR* Name)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(),
			TEXT("N2CTests"), FString::Printf(TEXT("%s-%s"), Name, *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
		IFileManager::Get().MakeDirectory(*Dir, true);
		return Dir;
	}

	/** An Actor Blueprint asset known to the Asset Registry (in memory, not saved) */
	UBlueprint* MakeBlueprintAsset(const FString& Name)
	{
		UPackage* Package = CreatePackage(*FString::Printf(TEXT("%s/%s"), Folder, *Name));
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(*Name),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (Blueprint)
		{
			FAssetRegistryModule::AssetCreated(Blueprint);
		}
		return Blueprint;
	}

	UEdGraph* EventGraphOf(UBlueprint* Blueprint)
	{
		return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	}

	TArray<FString> TagNames(const FN2CGraphState* State)
	{
		TArray<FString> Names;
		if (State)
		{
			for (const FN2CTagEntry& Tag : State->Tags)
			{
				Names.Add(Tag.Tag);
			}
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CGraphStateIdentityTest, "NodeToCode.Robustness.GraphStateIdentity", N2C_TEST_FLAGS)

bool FN2CGraphStateIdentityTest::RunTest(const FString& Parameters)
{
	using namespace N2CGraphIdentityTests;

	// The GUID-only lookups below are ambiguous on purpose
	AddExpectedMessage(TEXT("is in more than one Blueprint"), EAutomationExpectedMessageFlags::Contains, 0);

	N2CTest::DeleteContentFolder(Folder);
	const FString Dir = MakeTempDir(TEXT("GraphIdentity"));
	ON_SCOPE_EXIT
	{
		N2CTest::DeleteContentFolder(Folder);
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
	};

	TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));
	TestTrue(TEXT("the manager follows the Asset Registry"), Manager->IsFollowingAssetRegistry());

	UBlueprint* Original = MakeBlueprintAsset(TEXT("BP_N2CIdentity"));
	UEdGraph* OriginalGraph = EventGraphOf(Original);
	if (!TestNotNull(TEXT("original Blueprint and its EventGraph"), OriginalGraph))
	{
		return false;
	}
	const FSoftObjectPath OriginalPath(Original);

	// 1. Tag the original's EventGraph
	TestTrue(TEXT("tag the original"), Manager->AddTag(OriginalGraph->GraphGuid, OriginalGraph->GetName(), OriginalPath,
		TEXT("Original"), TEXT("Identity")));

	// 2. Duplicate it as the Content Browser does
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlueprint* Copy = Cast<UBlueprint>(AssetTools.DuplicateAsset(TEXT("BP_N2CIdentityCopy"), Folder, Original));
	UEdGraph* CopyGraph = EventGraphOf(Copy);
	if (!TestNotNull(TEXT("the duplicate and its EventGraph"), CopyGraph))
	{
		return false;
	}
	const FSoftObjectPath CopyPath(Copy);
	TestEqual(TEXT("the engine copies the graph GUID (why the key needs the Blueprint)"), CopyGraph->GraphGuid, OriginalGraph->GraphGuid);

	const FN2CGraphKey OriginalKey = FN2CGraphKey::FromGraph(OriginalGraph);
	const FN2CGraphKey CopyKey = FN2CGraphKey::FromGraph(CopyGraph);
	TestTrue(TEXT("the keys differ"), OriginalKey != CopyKey);
	TestEqual(TEXT("the copy's graph has no tags before it is tagged"), Manager->GetTagsForGraph(CopyKey).Num(), 0);

	// 3. Tag the copy: a second entry, and the original keeps its own
	TestTrue(TEXT("tag the copy"), Manager->AddTag(CopyGraph->GraphGuid, CopyGraph->GetName(), CopyPath, TEXT("Copy"), TEXT("Identity")));
	TestEqual(TEXT("two entries"), Manager->GetAllGraphStates().Num(), 2);
	{
		const FN2CGraphState* OriginalState = Manager->FindGraphState(OriginalKey);
		const FN2CGraphState* CopyState = Manager->FindGraphState(CopyKey);
		if (TestNotNull(TEXT("the original's entry"), OriginalState) && TestNotNull(TEXT("the copy's entry"), CopyState))
		{
			TestTrue(TEXT("the original's entry keeps its path"), OriginalState->OwningBlueprint == OriginalPath);
			TestTrue(TEXT("the copy's entry has its own path"), CopyState->OwningBlueprint == CopyPath);
			TestTrue(TEXT("the original has only its tag"), TagNames(OriginalState) == TArray<FString>{ TEXT("Original") });
			TestTrue(TEXT("the copy has only its tag"), TagNames(CopyState) == TArray<FString>{ TEXT("Copy") });
		}
	}
	TestNull(TEXT("a bare GUID is ambiguous now"), Manager->FindGraphState(OriginalGraph->GraphGuid));
	TestEqual(TEXT("FindKeysForGuid lists both"), Manager->FindKeysForGuid(OriginalGraph->GraphGuid).Num(), 2);

	// 4. Rename the original: its entry follows, the copy's stays
	const FString RenamedName = TEXT("BP_N2CIdentityRenamed");
	TArray<FAssetRenameData> Renames;
	Renames.Emplace(Original, Folder, RenamedName);
	TestTrue(TEXT("rename the original"), AssetTools.RenameAssets(Renames));
	const FSoftObjectPath RenamedPath(FString::Printf(TEXT("%s/%s.%s"), Folder, *RenamedName, *RenamedName));
	TestTrue(TEXT("the original now lives at the new path"), FSoftObjectPath(Original) == RenamedPath);
	{
		const FN2CGraphState* Renamed = Manager->FindGraphState(FN2CGraphKey(RenamedPath, OriginalGraph->GraphGuid));
		if (TestNotNull(TEXT("an entry at the new path"), Renamed))
		{
			TestTrue(TEXT("it holds the original's tag"), TagNames(Renamed) == TArray<FString>{ TEXT("Original") });
		}
		TestNull(TEXT("nothing left at the old path"), Manager->FindGraphState(OriginalKey));
		const FN2CGraphState* CopyState = Manager->FindGraphState(CopyKey);
		TestTrue(TEXT("the copy's entry is untouched"), CopyState && CopyState->OwningBlueprint == CopyPath && TagNames(CopyState) == TArray<FString>{ TEXT("Copy") });
	}

	// 5. Delete the copy: its entry is marked missing, and Prune Missing removes it
	ObjectTools::ForceDeleteObjects({ Copy }, /*ShowConfirmation*/ false);
	{
		const FN2CGraphState* CopyState = Manager->FindGraphState(CopyKey);
		TestTrue(TEXT("the deleted copy's entry is marked missing"), CopyState && CopyState->bMissing);
		TestEqual(TEXT("one missing entry"), Manager->GetMissingCount(), 1);
	}
	TestEqual(TEXT("Prune Missing removes one entry"), Manager->PruneMissing(), 1);
	TestNull(TEXT("the copy's entry is gone"), Manager->FindGraphState(CopyKey));
	TestNotNull(TEXT("the renamed original's entry stays"), Manager->FindGraphState(FN2CGraphKey(RenamedPath, OriginalGraph->GraphGuid)));
	TestNotNull(TEXT("a bare GUID finds the one entry left"), Manager->FindGraphState(OriginalGraph->GraphGuid));

	// 6. What was saved is version 3 with the renamed path
	FString Saved;
	FFileHelper::LoadFileToString(Saved, *Manager->GetStateFilePath());
	FN2CGraphStateFile File;
	if (TestTrue(TEXT("the saved file parses"), FN2CGraphStateFile::FromJsonString(Saved, File)))
	{
		TestEqual(TEXT("saved as version 3"), File.GetMajorVersion(), 3);
		TestTrue(TEXT("saved with the new path"), File.Graphs.Num() == 1 && File.Graphs[0].OwningBlueprint == RenamedPath);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CGraphStateVersionTest, "NodeToCode.Robustness.GraphStateVersion", N2C_TEST_FLAGS)

bool FN2CGraphStateVersionTest::RunTest(const FString& Parameters)
{
	using namespace N2CGraphIdentityTests;
	TArray<FString> Dirs;
	ON_SCOPE_EXIT
	{
		for (const FString& Dir : Dirs)
		{
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		}
	};

	AddExpectedError(TEXT("newer than this NodeToCode reads"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedMessage(TEXT("Graph state was not saved"), EAutomationExpectedMessageFlags::Contains, 1);

	const FGuid GuidA = FGuid::NewGuid();
	const FGuid GuidB = FGuid::NewGuid();
	const auto Entry = [](const FGuid& Guid, const TCHAR* Blueprint, const TCHAR* Tag)
	{
		return FString::Printf(TEXT("{\"graphGuid\":\"%s\",\"graphName\":\"EventGraph\",\"owningBlueprint\":\"%s\",\"tags\":[{\"tag\":\"%s\",\"category\":\"Default\"}]}"),
			*Guid.ToString(), Blueprint, Tag);
	};

	// 1. A version 2 file is migrated: every entry keeps its OwningBlueprint (a package path is normalised), the file is
	//    rewritten as version 3, and the version 2 file is in the backup
	{
		const FString Dir = MakeTempDir(TEXT("StateV2"));
		Dirs.Add(Dir);
		const FString StateFile = FPaths::Combine(Dir, TEXT("BlueprintGraphState.json"));
		const FString V2 = FString::Printf(TEXT("{\"version\":\"2.0\",\"graphs\":[%s,%s]}"),
			*Entry(GuidA, TEXT("/Game/AI/BP_Bot.BP_Bot"), TEXT("Bot")), *Entry(GuidB, TEXT("/Game/AI/BP_Boss"), TEXT("Boss")));
		FFileHelper::SaveStringToFile(V2, *StateFile);

		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));
		const FN2CGraphState* Bot = Manager->FindGraphState(FN2CGraphKey(FSoftObjectPath(TEXT("/Game/AI/BP_Bot.BP_Bot")), GuidA));
		const FN2CGraphState* Boss = Manager->FindGraphState(FN2CGraphKey(FSoftObjectPath(TEXT("/Game/AI/BP_Boss.BP_Boss")), GuidB));
		TestTrue(TEXT("the v2 entry keeps its Blueprint and tag"), Bot && TagNames(Bot) == TArray<FString>{ TEXT("Bot") });
		TestTrue(TEXT("a package path is read as the asset's object path"), Boss && TagNames(Boss) == TArray<FString>{ TEXT("Boss") });
		TestTrue(TEXT("a bare GUID still finds an unambiguous entry"), Manager->FindGraphState(GuidA) == Bot);

		FString Written;
		FFileHelper::LoadFileToString(Written, *StateFile);
		FN2CGraphStateFile File;
		TestTrue(TEXT("the migrated file parses"), FN2CGraphStateFile::FromJsonString(Written, File));
		TestEqual(TEXT("the file is version 3 now"), File.Version, FString(TEXT("3.0")));
		FString Backup;
		FFileHelper::LoadFileToString(Backup, *FN2CFileUtils::BackupPathFor(StateFile));
		TestTrue(TEXT("the version 2 file is in the backup"), Backup.Contains(TEXT("\"2.0\"")));
	}

	// 2. A file without a version is the oldest format and is migrated too
	{
		FN2CGraphStateFile File;
		TestTrue(TEXT("a version-less file parses"), FN2CGraphStateFile::FromJsonString(TEXT("{\"graphs\":[]}"), File));
		TestEqual(TEXT("and reads as version 1"), File.GetMajorVersion(), 1);
	}

	// 3. A file from a newer NodeToCode is not loaded, and nothing is saved over it
	{
		const FString Dir = MakeTempDir(TEXT("StateV9"));
		Dirs.Add(Dir);
		const FString StateFile = FPaths::Combine(Dir, TEXT("BlueprintGraphState.json"));
		const FString V9 = FString::Printf(TEXT("{\"version\":\"9.0\",\"graphs\":[%s]}"), *Entry(GuidA, TEXT("/Game/AI/BP_Bot.BP_Bot"), TEXT("Future")));
		FFileHelper::SaveStringToFile(V9, *StateFile);

		TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(Dir));
		TestEqual(TEXT("nothing loaded from the newer file"), Manager->GetAllGraphStates().Num(), 0);
		TestTrue(TEXT("saving is blocked"), Manager->IsSavingBlocked());
		Manager->AddTag(GuidB, TEXT("EventGraph"), FSoftObjectPath(TEXT("/Game/AI/BP_Other.BP_Other")), TEXT("New"), TEXT("Default"));
		FString After;
		FFileHelper::LoadFileToString(After, *StateFile);
		TestEqual(TEXT("the newer file is untouched"), After, V9);
	}

	// 4. Keys: one spelling per Blueprint
	{
		const FGuid Guid = FGuid::NewGuid();
		const FN2CGraphKey Object(FSoftObjectPath(TEXT("/Game/AI/BP_Bot.BP_Bot")), Guid);
		TestTrue(TEXT("package path"), FN2CGraphKey::FromStrings(TEXT("/Game/AI/BP_Bot"), Guid.ToString()) == Object);
		TestTrue(TEXT("export text"), FN2CGraphKey::FromStrings(TEXT("Blueprint'/Game/AI/BP_Bot.BP_Bot'"), Guid.ToString()) == Object);
		TestTrue(TEXT("hyphenated GUID and blanks"), FN2CGraphKey::FromStrings(TEXT(" /Game/AI/BP_Bot.BP_Bot "),
			Guid.ToString(EGuidFormats::DigitsWithHyphens)) == Object);
		TestFalse(TEXT("another Blueprint is another key"), FN2CGraphKey(FSoftObjectPath(TEXT("/Game/AI/BP_Bot2.BP_Bot2")), Guid) == Object);
		TestFalse(TEXT("an unparsable GUID gives an invalid key"), FN2CGraphKey::FromStrings(TEXT("/Game/AI/BP_Bot"), TEXT("nope")).IsValid());
	}
	return true;
}

/**
 * Two Blueprint editors on graphs with one GUID (a Blueprint and its Content Browser copy) each get an overlay
 * (lane-ui's gate 0387 reproduction). Overlays are tracked per UEdGraph, not per GraphGuid.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CGraphOverlayIdentityTest, "NodeToCode.Editor.OverlayPerGraph", N2C_TEST_FLAGS)

bool FN2CGraphOverlayIdentityTest::RunTest(const FString& Parameters)
{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
	// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor's window (see NodeToCode.Editor.Focus). This case
	// runs on 5.8; the same behaviour is checked live on the others.
	AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
	return true;
#else
	UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors))
	{
		return false;
	}

	N2CTest::FTestBlueprint A(TEXT("BP_N2COverlayOriginal"), TEXT("/Game"));
	N2CTest::FTestBlueprint B(TEXT("BP_N2COverlayCopy"), TEXT("/Game"));
	N2CTest::FTestBlueprint* const Both[] = { &A, &B };
	ON_SCOPE_EXIT
	{
		for (N2CTest::FTestBlueprint* Each : Both)
		{
			Each->GetPackage()->SetDirtyFlag(false);
			AssetEditors->CloseAllEditorsForAsset(Each->GetBlueprint());
		}
	};

	// What a Content Browser duplicate looks like: the copy's EventGraph has the original's GUID
	UEdGraph* GraphA = A.EventGraph();
	UEdGraph* GraphB = B.EventGraph();
	if (!TestTrue(TEXT("two EventGraphs"), GraphA && GraphB))
	{
		return false;
	}
	GraphB->GraphGuid = GraphA->GraphGuid;

	FN2CGraphOverlayInjector& Injector = FN2CGraphOverlayInjector::Get();
	for (N2CTest::FTestBlueprint* Each : Both)
	{
		TSharedPtr<IBlueprintEditor> Opened;
		FString OpenError;
		if (!TestTrue(FString::Printf(TEXT("%s opens: %s"), *Each->GetBlueprint()->GetName(), *OpenError),
			FN2CMcpBlueprintUtils::OpenBlueprintEditor(Each->GetBlueprint(), Opened, OpenError)))
		{
			return false;
		}
		const TSharedPtr<FBlueprintEditor> Editor = StaticCastSharedPtr<FBlueprintEditor>(Opened);
		UEdGraph* Graph = Each->EventGraph();
		Editor->JumpToHyperlink(Graph, false);

		TArray<TSharedPtr<SDockTab>> Tabs;
		Editor->FindOpenTabsContainingDocument(Graph, Tabs);
		if (!TestTrue(FString::Printf(TEXT("%s's EventGraph tab is open"), *Each->GetBlueprint()->GetName()), Tabs.Num() > 0))
		{
			return false;
		}
		Injector.InjectIntoTab(Tabs[0], Graph, Editor);
	}

	TestTrue(TEXT("the original's EventGraph has an overlay"), Injector.HasLiveOverlay(GraphA));
	TestTrue(TEXT("the copy's EventGraph (same GUID) has its own overlay"), Injector.HasLiveOverlay(GraphB));
	return true;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
