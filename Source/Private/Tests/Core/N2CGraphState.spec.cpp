// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CFileUtils.h"
#include "Utils/N2CPathUtils.h"

/**
 * The graph state's contract (card 0203, review imp-editor-integration-10): what keeps working when E11 gives one
 * archive the on-disk layout. Every case runs on an isolated manager (UN2CGraphStateManager::CreateIsolated) whose
 * state file is in a scratch folder; the host's Saved/NodeToCode and the singleton are never touched.
 *
 * - "the state file": FN2CGraphStateFile survives ToJson -> FromJson -> ToJson with every field set, and a second
 *   manager reads back exactly what the first saved.
 * - "malformed input": a state file that cannot be read is kept (as a .corrupt- copy, or left where it is), an error
 *   is reported, and tag changes afterwards write nothing over it (editor-integration-12).
 * - "ClearAllTags": tag-only graphs all go, in one pass, one save and one reload broadcast; graphs with a translation
 *   or an export keep it (editor-integration-11: the old loop removed entries from the array it walked).
 * - "LoadTranslation": every EN2CCodeLanguage, with the graph folder inside the project (a relative record), outside it
 *   (an absolute record) and under a Custom Translation Output Directory inside and outside the project, written the
 *   way the batch orchestrator writes them (editor-integration-3), and LoadN2CJson beside it. The real writers (batch
 *   orchestrator, LLM module, main window) and the records older builds wrote are NodeToCode.LLM.TranslationLoad's.
 * - "two Blueprints with the same graph GUID": a Blueprint and its Content Browser copy keep separate tags,
 *   translations and exports (editor-integration-2). The Asset Registry rename and delete paths are
 *   NodeToCode.Robustness.GraphStateIdentity's.
 *
 * Older single tests on the same manager: NodeToCode.Robustness.GraphStatePersistence (interrupted saves, legacy
 * migration), .GraphStateVersion (version 2 and newer files), .TagEvents.
 */
namespace N2CGraphStateSpec
{
	const TCHAR* StateFileName = TEXT("BlueprintGraphState.json");

	/** A scratch folder outside the project (so a translation record there is stored as an absolute path) */
	struct FOutsideDir
	{
		FString Path;

		explicit FOutsideDir(const TCHAR* Prefix)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserTempDir(),
				FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			IFileManager::Get().MakeDirectory(*Path, true);
		}

		~FOutsideDir()
		{
			IFileManager::Get().DeleteDirectory(*Path, false, true);
		}
	};

	FString ReadFile(const FString& Path)
	{
		FString Content;
		FFileHelper::LoadFileToString(Content, *Path);
		return Content;
	}

	/** The .corrupt- copies next to a state file, as full paths */
	TArray<FString> CorruptCopies(const FString& StateFile)
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(StateFile + TEXT(".corrupt-*")), true, false);
		TArray<FString> Paths;
		for (const FString& Name : Names)
		{
			Paths.Add(FPaths::Combine(FPaths::GetPath(StateFile), Name));
		}
		return Paths;
	}

	/** The graphs as one JSON document with a fixed version and save time, for comparing two sets of entries */
	FString GraphsJson(const TArray<FN2CGraphState>& Graphs)
	{
		FN2CGraphStateFile File;
		File.LastSaved = FDateTime(2026, 1, 1);
		File.Graphs = Graphs;
		return File.ToJsonString(true);
	}

	FString NormalizedDir(const FString& Path)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeDirectoryName(Full);
		FPaths::CollapseRelativeDirectories(Full);
		return Full;
	}

	/** A state file with every field set, two tags, a translation, an export, a missing entry and text to escape */
	FN2CGraphStateFile MakeFullFile()
	{
		FN2CGraphStateFile File;
		File.LastSaved = FDateTime(2026, 10, 26, 12, 34, 56, 789);

		FN2CGraphState Full;
		Full.GraphGuid = FGuid(0x01234567, 0x89ABCDEF, 0x0F1E2D3C, 0x4B5A6978);
		Full.GraphName = TEXT("Fire \"Weapon\" \\ Ünïcødé 图");
		Full.OwningBlueprint = FSoftObjectPath(TEXT("/Game/Weapons/BP_Rifle.BP_Rifle"));
		FN2CTagEntry First;
		First.Tag = TEXT("Combat");
		First.Category = TEXT("Gameplay");
		First.Description = TEXT("Line one\nline two\ttabbed");
		First.Timestamp = FDateTime(2026, 10, 20, 8, 0, 1, 2);
		FN2CTagEntry Second;
		Second.Tag = TEXT("AI");
		Second.Category = TEXT("Default");
		Second.Timestamp = FDateTime(2026, 10, 21, 9, 30, 0, 500);
		Full.Tags = { First, Second };
		Full.Translation.bExists = true;
		Full.Translation.OutputPath = TEXT("Saved/NodeToCode/Translations/Batch_1/Fire Weapon");
		Full.Translation.Timestamp = FDateTime(2026, 10, 22, 10, 11, 12, 13);
		Full.Translation.Provider = TEXT("Anthropic");
		Full.Translation.Model = TEXT("claude-haiku-5-5");
		Full.Translation.Language = TEXT("Python");
		Full.Translation.Summary.DeclarationPreview = TEXT("def fire_weapon(self):");
		Full.Translation.Summary.ImplementationLines = 42;
		Full.Translation.Summary.bHasNotes = true;
		Full.JsonExport.bExists = true;
		Full.JsonExport.OutputPath = TEXT("D:/Elsewhere/N2C_BP_Rifle.json");
		Full.JsonExport.Timestamp = FDateTime(2026, 10, 23, 23, 59, 59, 999);
		Full.JsonExport.bMinified = true;
		File.Graphs.Add(Full);

		FN2CGraphState Missing;
		Missing.GraphGuid = FGuid(0x11111111, 0x22222222, 0x33333333, 0x44444444);
		Missing.GraphName = TEXT("EventGraph");
		Missing.OwningBlueprint = FSoftObjectPath(TEXT("/Game/Deleted/BP_Gone.BP_Gone"));
		FN2CTagEntry Kept;
		Kept.Tag = TEXT("Keep");
		Kept.Timestamp = FDateTime(2026, 10, 24);
		Missing.Tags.Add(Kept);
		Missing.bMissing = true;
		File.Graphs.Add(Missing);
		return File;
	}

	/** One malformed state file: what it holds, the error it reports, and the finding that keeps it disabled */
	struct FMalformedCase
	{
		const TCHAR* Name;
		FString Content;
		const TCHAR* ExpectedError;
		const TCHAR* KnownDefect = nullptr;
	};

	TArray<FMalformedCase> MalformedCases()
	{
		const FString Entry = TEXT("{\"graphGuid\":\"0123456789ABCDEF0F1E2D3C4B5A6978\",\"graphName\":\"EventGraph\",")
			TEXT("\"owningBlueprint\":\"/Game/BP_Kept.BP_Kept\",\"tags\":[{\"tag\":\"Kept\",\"category\":\"Default\"}]}");
		const TCHAR* ParseError = TEXT("could not read its tag and translation history file");
		// A file that parses as JSON but not as a state file is set aside like one that does not parse, and the error
		// says what is wrong with it (editor-integration-12, card 0499)
		return {
			{ TEXT("a file cut off mid-entry (a save the editor did not finish)"),
				FString::Printf(TEXT("{\"version\":\"3.0\",\"graphs\":[%s,{\"graphGuid\":\"0123"), *Entry), ParseError },
			{ TEXT("text that is not JSON"), TEXT("this is not a graph state file"), ParseError },
			{ TEXT("a hand edit that left a trailing comma"),
				FString::Printf(TEXT("{\"version\":\"3.0\",\"graphs\":[%s],}"), *Entry), ParseError },
			{ TEXT("a JSON array instead of an object"), FString::Printf(TEXT("[%s]"), *Entry), ParseError },
			{ TEXT("a graphs field that is not an array"),
				FString::Printf(TEXT("{\"version\":\"3.0\",\"graphs\":{\"0\":%s}}"), *Entry),
				TEXT("could not read its tag and translation history file .its .graphs. field is not an array.") },
			{ TEXT("a graph entry that is not an object"),
				FString::Printf(TEXT("{\"version\":\"3.0\",\"graphs\":[\"EventGraph\",%s]}"), *Entry),
				TEXT("could not read its tag and translation history file .graph entry 0 is not an object.") },
			{ TEXT("a graph entry whose graphGuid does not parse"),
				TEXT("{\"version\":\"3.0\",\"graphs\":[{\"graphGuid\":\"not-a-guid\",\"graphName\":\"EventGraph\",")
				TEXT("\"owningBlueprint\":\"/Game/BP_Kept.BP_Kept\",\"tags\":[{\"tag\":\"Kept\",\"category\":\"Default\"}]}]}"),
				TEXT("could not read its tag and translation history file .graph entry 0 has no graphGuid that parses as a GUID.") },
			{ TEXT("a graph entry without a graphGuid"),
				FString::Printf(TEXT("{\"version\":\"3.0\",\"graphs\":[%s,{\"graphName\":\"EventGraph\",\"tags\":[]}]}"), *Entry),
				TEXT("could not read its tag and translation history file .graph entry 1 has no graphGuid that parses as a GUID.") },
		};
	}

	/** Where a translation's graph folder is, and so how its record stores the path */
	enum class EFolderKind : uint8
	{
		InsideProject,     // Saved/Automation/...: stored relative to the project
		OutsideProject,    // the user's temp folder: stored as an absolute path
		CustomOutside,     // Custom Translation Output Directory outside the project: absolute
		CustomInside,      // Custom Translation Output Directory inside the project: relative
	};

	const TCHAR* DescribeFolder(EFolderKind Kind)
	{
		switch (Kind)
		{
		case EFolderKind::InsideProject: return TEXT("a folder inside the project (relative record)");
		case EFolderKind::OutsideProject: return TEXT("a folder outside the project (absolute record)");
		case EFolderKind::CustomOutside: return TEXT("a custom output folder outside the project (absolute record)");
		case EFolderKind::CustomInside: return TEXT("a custom output folder inside the project (relative record)");
		}
		return TEXT("?");
	}

	bool IsRelativeKind(EFolderKind Kind)
	{
		return Kind == EFolderKind::InsideProject || Kind == EFolderKind::CustomInside;
	}

	/** What one saved translation holds */
	struct FSavedFiles
	{
		FString Declaration;
		FString Implementation;
		FString Notes;
		FString Json;
	};

	/**
	 * Writes a translation the way UN2CBatchTranslationOrchestrator::SaveItemTranslation does: <Stem><declaration
	 * extension> for a language with a declaration file, <Stem><implementation extension>, <Stem>_Notes.txt and
	 * <Stem>.json in GraphDir. (The orchestrator records into the singleton, so the spec writes the same files and
	 * records them through the same RecordSavedTranslation on its isolated manager.)
	 */
	FSavedFiles WriteLikeTheBatch(const FString& GraphDir, const FString& GraphName, const FN2CTargetLanguage& Language)
	{
		const FString Stem = FN2CPathUtils::GetTranslationFileStem(GraphName);
		FSavedFiles Files;
		Files.Implementation = FString::Printf(TEXT("// %s implementation of %s\nline two\n"), Language.Id, *GraphName);
		Files.Notes = FString::Printf(TEXT("Notes for the %s translation"), Language.DisplayName);
		Files.Json = FString::Printf(TEXT("{\"version\":\"2.0.0\",\"metadata\":{\"Name\":\"%s\"}}"), Language.Id);
		IFileManager::Get().MakeDirectory(*GraphDir, true);
		if (Language.bHasDeclarationFile)
		{
			Files.Declaration = FString::Printf(TEXT("// %s declaration of %s\n"), Language.Id, *GraphName);
			FFileHelper::SaveStringToFile(Files.Declaration, *FPaths::Combine(GraphDir, Stem + Language.DeclarationExtension));
		}
		FFileHelper::SaveStringToFile(Files.Implementation, *FPaths::Combine(GraphDir, Stem + Language.ImplementationExtension));
		FFileHelper::SaveStringToFile(Files.Notes, *FPaths::Combine(GraphDir, Stem + TEXT("_Notes.txt")));
		FFileHelper::SaveStringToFile(Files.Json, *FPaths::Combine(GraphDir, Stem + TEXT(".json")));
		return Files;
	}
}

BEGIN_DEFINE_SPEC(FN2CGraphStateSpec, "NodeToCode.State", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	TStrongObjectPtr<UN2CGraphStateManager> Manager;

	FString StateFile() const { return FPaths::Combine(Scratch->Path, N2CGraphStateSpec::StateFileName); }

	/** A fresh isolated manager on the scratch folder (the previous one is dropped first) */
	UN2CGraphStateManager* OpenManager()
	{
		Manager.Reset();
		Manager.Reset(UN2CGraphStateManager::CreateIsolated(Scratch->Path));
		return Manager.Get();
	}

	/** Runs Body as an xIt, or as an It with N2C_RUN_KNOWN_DEFECTS=1 in the editor's environment */
	void KnownDefectIt(const FString& CaseText, TFunction<void()> Body)
	{
		if (FPlatformMisc::GetEnvironmentVariable(TEXT("N2C_RUN_KNOWN_DEFECTS")) == TEXT("1"))
		{
			It(CaseText, MoveTemp(Body));
		}
		else
		{
			xIt(CaseText, MoveTemp(Body));
		}
	}

END_DEFINE_SPEC(FN2CGraphStateSpec)

void FN2CGraphStateSpec::Define()
{
	using namespace N2CGraphStateSpec;

	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CStateSpec"));
	});

	AfterEach([this]()
	{
		Manager.Reset();
		Scratch.Reset();
	});

	Describe(TEXT("the state file"), [this]()
	{
		It(TEXT("survives ToJson -> FromJson -> ToJson unchanged with every field set"), [this]()
		{
			const FN2CGraphStateFile Original = MakeFullFile();
			for (const bool bPretty : { true, false })
			{
				const FString What = bPretty ? TEXT("pretty") : TEXT("condensed");
				const FString Written = Original.ToJsonString(bPretty);
				FN2CGraphStateFile Read;
				if (!TestTrue(What + TEXT(": the written file parses"), FN2CGraphStateFile::FromJsonString(Written, Read)))
				{
					return;
				}
				TestEqual(What + TEXT(": written again it is the same text"), Read.ToJsonString(bPretty), Written);
				TestEqual(What + TEXT(": version"), Read.Version, FString(TEXT("3.0")));
				TestEqual(What + TEXT(": major version"), Read.GetMajorVersion(), FN2CGraphStateFile::CurrentMajorVersion);
				TestEqual(What + TEXT(": last saved"), Read.LastSaved, Original.LastSaved);
				if (!TestEqual(What + TEXT(": two graphs"), Read.Graphs.Num(), 2))
				{
					return;
				}
				const FN2CGraphState& Full = Read.Graphs[0];
				const FN2CGraphState& Expected = Original.Graphs[0];
				TestEqual(What + TEXT(": graph GUID"), Full.GraphGuid, Expected.GraphGuid);
				TestEqual(What + TEXT(": graph name with quotes, a backslash and non-ASCII text"), Full.GraphName, Expected.GraphName);
				TestTrue(What + TEXT(": owning Blueprint"), Full.OwningBlueprint == Expected.OwningBlueprint);
				TestTrue(What + TEXT(": key"), Full.GetKey() == Expected.GetKey());
				if (TestEqual(What + TEXT(": two tags"), Full.Tags.Num(), 2))
				{
					TestEqual(What + TEXT(": tag"), Full.Tags[0].Tag, Expected.Tags[0].Tag);
					TestEqual(What + TEXT(": category"), Full.Tags[0].Category, Expected.Tags[0].Category);
					TestEqual(What + TEXT(": description with a newline and a tab"), Full.Tags[0].Description, Expected.Tags[0].Description);
					TestEqual(What + TEXT(": tag timestamp (milliseconds)"), Full.Tags[0].Timestamp, Expected.Tags[0].Timestamp);
					TestEqual(What + TEXT(": second tag keeps its order"), Full.Tags[1].Tag, Expected.Tags[1].Tag);
				}
				TestTrue(What + TEXT(": translation exists"), Full.Translation.bExists);
				TestEqual(What + TEXT(": translation path"), Full.Translation.OutputPath, Expected.Translation.OutputPath);
				TestEqual(What + TEXT(": translation timestamp"), Full.Translation.Timestamp, Expected.Translation.Timestamp);
				TestEqual(What + TEXT(": provider"), Full.Translation.Provider, Expected.Translation.Provider);
				TestEqual(What + TEXT(": model"), Full.Translation.Model, Expected.Translation.Model);
				TestEqual(What + TEXT(": language"), Full.Translation.Language, Expected.Translation.Language);
				TestEqual(What + TEXT(": declaration preview"), Full.Translation.Summary.DeclarationPreview, Expected.Translation.Summary.DeclarationPreview);
				TestEqual(What + TEXT(": implementation lines"), Full.Translation.Summary.ImplementationLines, 42);
				TestTrue(What + TEXT(": has notes"), Full.Translation.Summary.bHasNotes);
				TestTrue(What + TEXT(": export exists"), Full.JsonExport.bExists);
				TestEqual(What + TEXT(": export path"), Full.JsonExport.OutputPath, Expected.JsonExport.OutputPath);
				TestEqual(What + TEXT(": export timestamp"), Full.JsonExport.Timestamp, Expected.JsonExport.Timestamp);
				TestTrue(What + TEXT(": minified"), Full.JsonExport.bMinified);
				TestFalse(What + TEXT(": the first graph is not missing"), Full.bMissing);
				TestTrue(What + TEXT(": the second graph is missing"), Read.Graphs[1].bMissing);
				TestEqual(What + TEXT(": a tag without a description reads as empty"), Read.Graphs[1].Tags[0].Description, FString());
				TestFalse(What + TEXT(": the second graph has no translation"), Read.Graphs[1].HasTranslation());
				TestFalse(What + TEXT(": the second graph has no export"), Read.Graphs[1].HasJsonExport());
			}
		});

		It(TEXT("is read back by a second manager exactly as the first saved it"), [this]()
		{
			UN2CGraphStateManager* First = OpenManager();
			const FSoftObjectPath Rifle(TEXT("/Game/Weapons/BP_Rifle.BP_Rifle"));
			const FSoftObjectPath Pistol(TEXT("/Game/Weapons/BP_Pistol.BP_Pistol"));
			const FGuid Fire = FGuid::NewGuid();
			const FGuid Reload = FGuid::NewGuid();
			First->AddTag(Fire, TEXT("Fire"), Rifle, TEXT("Combat"), TEXT("Gameplay"), TEXT("Shoots"));
			First->AddTag(Fire, TEXT("Fire"), Rifle, TEXT("AI"), TEXT(""));
			First->AddTag(Fire, TEXT("Fire"), Pistol, TEXT("Combat"), TEXT("Gameplay"));
			FN2CTranslationSummary Summary;
			Summary.DeclarationPreview = TEXT("void Fire();");
			Summary.ImplementationLines = 7;
			Summary.bHasNotes = true;
			First->RecordSavedTranslation(Reload, TEXT("Reload"), Rifle, Scratch->Path / TEXT("Translations/Reload"),
				TEXT("OpenAI"), TEXT("gpt-test"), EN2CCodeLanguage::Swift, Summary);
			First->SetJsonExportState(Fire, TEXT("Fire"), Pistol, TEXT("Saved/Exports/Fire.json"), true);
			First->HandleBlueprintRemoved(Pistol);
			TestEqual(TEXT("three entries"), First->GetAllGraphStates().Num(), 3);
			TestFalse(TEXT("nothing left unsaved"), First->IsDirty());
			const FString Saved = GraphsJson(First->GetAllGraphStates());

			UN2CGraphStateManager* Second = OpenManager();
			TestFalse(TEXT("saving is on"), Second->IsSavingBlocked());
			TestEqual(TEXT("the second manager holds what the first saved"), GraphsJson(Second->GetAllGraphStates()), Saved);
			const FN2CGraphState* Pistols = Second->FindGraphState(FN2CGraphKey(Pistol, Fire));
			TestTrue(TEXT("the deleted Blueprint's entry is still missing"), Pistols && Pistols->bMissing && Pistols->HasJsonExport());
			const FN2CGraphState* Reloads = Second->FindGraphState(FN2CGraphKey(Rifle, Reload));
			TestTrue(TEXT("the translation record kept its language id"), Reloads && Reloads->Translation.Language == TEXT("Swift"));
			TestTrue(TEXT("and its project-relative path"), Reloads && FPaths::IsRelative(Reloads->Translation.OutputPath));
		});
	});

	Describe(TEXT("malformed input"), [this]()
	{
		for (const FMalformedCase& Case : MalformedCases())
		{
			const FString CaseText = FString::Printf(TEXT("keeps %s, reports an error and writes nothing over it"), Case.Name);
			auto Body = [this, Case]()
			{
				AddExpectedError(Case.ExpectedError, EAutomationExpectedErrorFlags::Contains, 1);
				FFileHelper::SaveStringToFile(Case.Content, *StateFile());

				UN2CGraphStateManager* Loaded = OpenManager();
				TestTrue(TEXT("saving is off"), Loaded->IsSavingBlocked());

				// Two tag changes afterwards: the first save would replace the file and keep it as .bak, the second the .bak
				const FSoftObjectPath Blueprint(TEXT("/Game/BP_New.BP_New"));
				Loaded->AddTag(FGuid::NewGuid(), TEXT("First"), Blueprint, TEXT("New"), TEXT("Default"));
				Loaded->AddTag(FGuid::NewGuid(), TEXT("Second"), Blueprint, TEXT("New"), TEXT("Default"));
				TestEqual(TEXT("nothing was saved"), Loaded->GetSaveCount(), 0);

				// The user's bytes are still on disk: the file where it was, or the copy set aside
				const TArray<FString> Copies = CorruptCopies(StateFile());
				const bool bInPlace = FPaths::FileExists(StateFile()) && ReadFile(StateFile()) == Case.Content;
				const bool bSetAside = Copies.Num() == 1 && ReadFile(Copies[0]) == Case.Content;
				TestTrue(TEXT("the file is kept, unchanged, in place or as one .corrupt- copy"), bInPlace || bSetAside);
				if (bSetAside)
				{
					TestEqual(TEXT("the manager names the copy"), FPaths::ConvertRelativePathToFull(Loaded->GetQuarantinedFilePath()), Copies[0]);
					TestFalse(TEXT("no new state file was written next to it"), FPaths::FileExists(StateFile()));
				}
			};
			if (Case.KnownDefect)
			{
				// Known defect: see FMalformedCase::KnownDefect in MalformedCases()
				KnownDefectIt(CaseText, Body);
			}
			else
			{
				It(CaseText, Body);
			}
		}

#if PLATFORM_WINDOWS
		It(TEXT("keeps a file it cannot open (another process holds it) and writes nothing over it"), [this]()
		{
			AddExpectedError(TEXT("Failed to read graph state from"), EAutomationExpectedErrorFlags::Contains, 1);
			{
				// Seed a good file with one entry
				UN2CGraphStateManager* Seed = OpenManager();
				Seed->AddTag(FGuid::NewGuid(), TEXT("EventGraph"), FSoftObjectPath(TEXT("/Game/BP_Kept.BP_Kept")), TEXT("Kept"), TEXT("Default"));
				Manager.Reset();
			}
			const FString Before = ReadFile(StateFile());

			// A writer without FILEWRITE_AllowRead opens the file with no sharing, so the manager's read fails
			TUniquePtr<FArchive> Holder(IFileManager::Get().CreateFileWriter(*StateFile(), FILEWRITE_Append));
			if (!TestTrue(TEXT("the file is held open"), Holder.IsValid()))
			{
				return;
			}
			UN2CGraphStateManager* Loaded = OpenManager();
			TestTrue(TEXT("saving is off"), Loaded->IsSavingBlocked());
			Loaded->AddTag(FGuid::NewGuid(), TEXT("Other"), FSoftObjectPath(TEXT("/Game/BP_New.BP_New")), TEXT("New"), TEXT("Default"));
			TestEqual(TEXT("nothing was saved"), Loaded->GetSaveCount(), 0);
			Holder->Close();
			Holder.Reset();

			TestEqual(TEXT("the file is unchanged"), ReadFile(StateFile()), Before);
			TestEqual(TEXT("it was not set aside"), CorruptCopies(StateFile()).Num(), 0);
			TestTrue(TEXT("once it can be read, a reload loads it"), Loaded->LoadState());
			TestFalse(TEXT("and turns saving back on"), Loaded->IsSavingBlocked());
			TestEqual(TEXT("with its entry"), Loaded->GetAllGraphStates().Num(), 1);
		});
#endif
	});

	Describe(TEXT("ClearAllTags"), [this]()
	{
		It(TEXT("removes every tag-only graph in one pass, one save and one reload broadcast"), [this]()
		{
			UN2CGraphStateManager* State = OpenManager();
			const FSoftObjectPath A(TEXT("/Game/BP_A.BP_A"));
			const FSoftObjectPath B(TEXT("/Game/BP_B.BP_B"));
			const FGuid Shared = FGuid::NewGuid();
			int32 TagCount = 0;
			// Neighbouring entries removed one after another: the old loop skipped every second one
			for (int32 Graph = 0; Graph < 6; ++Graph)
			{
				const FGuid Guid = Graph < 2 ? Shared : FGuid::NewGuid();
				for (int32 Tag = 0; Tag <= Graph % 3; ++Tag)
				{
					State->AddTag(Guid, FString::Printf(TEXT("Graph%d"), Graph), Graph % 2 ? B : A,
						FString::Printf(TEXT("Tag%d"), Tag), Tag ? TEXT("Second") : TEXT("Default"));
					++TagCount;
				}
			}
			TestEqual(TEXT("six tag-only graphs, two of them with one GUID in two Blueprints"), State->GetAllGraphStates().Num(), 6);

			int32 Reloads = 0;
			int32 PerTagEvents = 0;
			int32 PerGraphEvents = 0;
			State->OnGraphStatesReloaded.AddLambda([&Reloads]() { ++Reloads; });
			State->OnGraphTagRemoved.AddLambda([&PerTagEvents](const FN2CGraphKey&, const FString&) { ++PerTagEvents; });
			State->OnGraphStateChanged.AddLambda([&PerGraphEvents](const FN2CGraphKey&) { ++PerGraphEvents; });
			const int32 SavesBefore = State->GetSaveCount();

			TestEqual(TEXT("ClearAllTags reports every tag"), State->ClearAllTags(), TagCount);
			TestEqual(TEXT("no entries are left"), State->GetAllGraphStates().Num(), 0);
			TestEqual(TEXT("no tag names are left"), State->GetAllTagNames().Num(), 0);
			TestEqual(TEXT("saved once"), State->GetSaveCount() - SavesBefore, 1);
			TestEqual(TEXT("one reload broadcast"), Reloads, 1);
			TestEqual(TEXT("no per-tag broadcasts"), PerTagEvents, 0);
			TestEqual(TEXT("no per-graph broadcasts"), PerGraphEvents, 0);

			FN2CGraphStateFile OnDisk;
			TestTrue(TEXT("the saved file parses"), FN2CGraphStateFile::FromJsonString(ReadFile(StateFile()), OnDisk));
			TestEqual(TEXT("the saved file has no graphs"), OnDisk.Graphs.Num(), 0);

			TestEqual(TEXT("a second Clear All finds nothing"), State->ClearAllTags(), 0);
			TestEqual(TEXT("and saves nothing"), State->GetSaveCount() - SavesBefore, 1);
			TestEqual(TEXT("nor broadcasts"), Reloads, 1);
		});

		It(TEXT("keeps the translation and export of graphs between tag-only ones"), [this]()
		{
			UN2CGraphStateManager* State = OpenManager();
			const FSoftObjectPath Blueprint(TEXT("/Game/BP_Mixed.BP_Mixed"));
			TArray<FGuid> Guids;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				Guids.Add(FGuid::NewGuid());
				const FString Name = FString::Printf(TEXT("Graph%d"), Index);
				State->AddTag(Guids.Last(), Name, Blueprint, TEXT("Tagged"), TEXT("Default"));
				if (Index == 1)
				{
					State->RecordSavedTranslation(Guids.Last(), Name, Blueprint, Scratch->Path / Name, TEXT("Anthropic"),
						TEXT("model"), EN2CCodeLanguage::Cpp, FN2CTranslationSummary());
				}
				else if (Index == 4)
				{
					State->SetJsonExportState(Guids.Last(), Name, Blueprint, TEXT("Saved/Exports/Graph4.json"), false);
				}
			}

			TestEqual(TEXT("six tags removed"), State->ClearAllTags(), 6);
			const TArray<FN2CGraphState>& Left = State->GetAllGraphStates();
			if (TestEqual(TEXT("the translated and the exported graph are left"), Left.Num(), 2))
			{
				TestEqual(TEXT("the translated graph, first"), Left[0].GraphGuid, Guids[1]);
				TestTrue(TEXT("with its translation"), Left[0].HasTranslation());
				TestEqual(TEXT("the exported graph, second"), Left[1].GraphGuid, Guids[4]);
				TestTrue(TEXT("with its export"), Left[1].HasJsonExport());
				TestEqual(TEXT("neither has a tag"), Left[0].Tags.Num() + Left[1].Tags.Num(), 0);
			}
			UN2CGraphStateManager* Reloaded = OpenManager();
			TestEqual(TEXT("the saved file holds the same two"), Reloaded->GetAllGraphStates().Num(), 2);
		});
	});

	Describe(TEXT("RemoveTags (Remove Selected)"), [this]()
	{
		It(TEXT("removes 50 tags with one save and one reload broadcast, and RestoreTags puts them back the same way"), [this]()
		{
			UN2CGraphStateManager* State = OpenManager();
			const FSoftObjectPath Blueprint(TEXT("/Game/BP_Many.BP_Many"));
			TArray<FN2CTagRemovalRequest> Requests;
			TArray<FGuid> Guids;
			// 25 graphs with two tags each; graph 3 also has a translation, so it must stay after its tags go
			for (int32 Graph = 0; Graph < 25; ++Graph)
			{
				Guids.Add(FGuid::NewGuid());
				const FString Name = FString::Printf(TEXT("Graph%d"), Graph);
				State->AddTag(Guids.Last(), Name, Blueprint, TEXT("First"), TEXT("Default"), TEXT("described"));
				State->AddTag(Guids.Last(), Name, Blueprint, TEXT("Second"), TEXT("Other"));
				Requests.Add({FN2CGraphKey(Blueprint, Guids.Last()), TEXT("First"), TEXT("Default")});
				Requests.Add({FN2CGraphKey(Blueprint, Guids.Last()), TEXT("Second"), TEXT("")});
				if (Graph == 3)
				{
					State->RecordSavedTranslation(Guids.Last(), Name, Blueprint, Scratch->Path / Name, TEXT("Anthropic"),
						TEXT("model"), EN2CCodeLanguage::Cpp, FN2CTranslationSummary());
				}
			}
			// One request that matches nothing is skipped
			Requests.Add({FN2CGraphKey(Blueprint, FGuid::NewGuid()), TEXT("First"), TEXT("Default")});
			const FDateTime FirstStamp = State->GetTagsForGraph(FN2CGraphKey(Blueprint, Guids[0]))[0].Timestamp;

			int32 Reloads = 0;
			int32 PerTagEvents = 0;
			int32 PerGraphEvents = 0;
			State->OnGraphStatesReloaded.AddLambda([&Reloads]() { ++Reloads; });
			State->OnGraphTagRemoved.AddLambda([&PerTagEvents](const FN2CGraphKey&, const FString&) { ++PerTagEvents; });
			State->OnGraphTagAdded.AddLambda([&PerTagEvents](const FN2CGraphState&) { ++PerTagEvents; });
			State->OnGraphStateChanged.AddLambda([&PerGraphEvents](const FN2CGraphKey&) { ++PerGraphEvents; });
			int32 SavesBefore = State->GetSaveCount();

			const TArray<FN2CRemovedTag> Removed = State->RemoveTags(Requests);
			TestEqual(TEXT("50 tags removed"), Removed.Num(), 50);
			TestEqual(TEXT("one state-file write"), State->GetSaveCount() - SavesBefore, 1);
			TestEqual(TEXT("one reload broadcast"), Reloads, 1);
			TestEqual(TEXT("no per-tag broadcasts"), PerTagEvents, 0);
			TestEqual(TEXT("no per-graph broadcasts"), PerGraphEvents, 0);
			TestEqual(TEXT("only the translated graph is left"), State->GetAllGraphStates().Num(), 1);
			TestEqual(TEXT("no tag names are left"), State->GetAllTagNames().Num(), 0);

			Reloads = 0;
			SavesBefore = State->GetSaveCount();
			TestEqual(TEXT("RestoreTags puts back all 50"), State->RestoreTags(Removed), 50);
			TestEqual(TEXT("with one state-file write"), State->GetSaveCount() - SavesBefore, 1);
			TestEqual(TEXT("and one reload broadcast"), Reloads, 1);
			TestEqual(TEXT("and no per-tag broadcasts"), PerTagEvents, 0);
			TestEqual(TEXT("every graph is back"), State->GetAllGraphStates().Num(), 25);
			const TArray<FN2CTagEntry> FirstTags = State->GetTagsForGraph(FN2CGraphKey(Blueprint, Guids[0]));
			if (TestEqual(TEXT("graph 0 has both tags again"), FirstTags.Num(), 2))
			{
				const FN2CTagEntry* First = FirstTags.FindByPredicate([](const FN2CTagEntry& Entry) { return Entry.Tag == TEXT("First"); });
				if (TestNotNull(TEXT("its First tag"), First))
				{
					TestEqual(TEXT("keeps its description"), First->Description, FString(TEXT("described")));
					TestEqual(TEXT("keeps its timestamp"), First->Timestamp, FirstStamp);
				}
			}
			TestTrue(TEXT("the translated graph kept its translation"),
				State->HasTranslation(FN2CGraphKey(Blueprint, Guids[3])));

			SavesBefore = State->GetSaveCount();
			TestEqual(TEXT("a second restore finds every tag already there"), State->RestoreTags(Removed), 0);
			TestEqual(TEXT("and saves nothing"), State->GetSaveCount() - SavesBefore, 0);

			UN2CGraphStateManager* Reloaded = OpenManager();
			TestEqual(TEXT("the saved file holds the restored tags"), Reloaded->GetAllTagNames().Num(), 2);
			TestEqual(TEXT("on every graph"), Reloaded->GetAllGraphStates().Num(), 25);
		});
	});

	Describe(TEXT("LoadTranslation"), [this]()
	{
		for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
		{
			for (const EFolderKind Kind : { EFolderKind::InsideProject, EFolderKind::OutsideProject, EFolderKind::CustomOutside, EFolderKind::CustomInside })
			{
				const FN2CTargetLanguage* Row = &Language;
				It(FString::Printf(TEXT("finds a %s translation saved in %s"), Language.Id, DescribeFolder(Kind)), [this, Row, Kind]()
				{
					const FN2CTargetLanguage& Language = *Row;
					// The folder the batch writes under
					TUniquePtr<FOutsideDir> Outside;
					TUniquePtr<N2CTest::FScratchDir> Inside;
					FString Base;
					UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
					const FString SettingBefore = Settings->CustomTranslationOutputDirectory.Path;
					ON_SCOPE_EXIT { Settings->CustomTranslationOutputDirectory.Path = SettingBefore; };
					switch (Kind)
					{
					case EFolderKind::InsideProject:
						Base = Scratch->Path / TEXT("Translations");
						break;
					case EFolderKind::OutsideProject:
						Outside = MakeUnique<FOutsideDir>(TEXT("N2CStateSpecOut"));
						Base = Outside->Path;
						break;
					case EFolderKind::CustomOutside:
					case EFolderKind::CustomInside:
						if (Kind == EFolderKind::CustomOutside)
						{
							Outside = MakeUnique<FOutsideDir>(TEXT("N2CStateSpecCustom"));
							Settings->CustomTranslationOutputDirectory.Path = Outside->Path;
						}
						else
						{
							Inside = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CStateSpecCustom"));
							Settings->CustomTranslationOutputDirectory.Path = Inside->Path;
						}
						Base = FN2CPathUtils::GetTranslationsBasePath();
						TestEqual(TEXT("the translations folder is the custom one"), NormalizedDir(Base),
							NormalizedDir(Settings->CustomTranslationOutputDirectory.Path));
						break;
					}

					const FString GraphName = FString::Printf(TEXT("Graph_%s"), Language.Id);
					const FString GraphDir = FPaths::Combine(Base, TEXT("Batch_Spec"), FN2CPathUtils::GetTranslationFileStem(GraphName));
					const FSavedFiles Saved = WriteLikeTheBatch(GraphDir, GraphName, Language);
					// A header from an earlier C++ translation of the same graph is not this translation's
					if (!Language.bHasDeclarationFile)
					{
						FFileHelper::SaveStringToFile(TEXT("// stale C++ header"), *FPaths::Combine(GraphDir, GraphName + TEXT(".h")));
					}

					UN2CGraphStateManager* State = OpenManager();
					const FN2CGraphKey Key(FSoftObjectPath(TEXT("/Game/BP_Load.BP_Load")), FGuid::NewGuid());
					State->RecordSavedTranslation(Key.GraphGuid, GraphName, Key.Blueprint, GraphDir, TEXT("Anthropic"),
						TEXT("model"), Language.Enum, FN2CTranslationSummary());

					const FN2CGraphState* Entry = State->FindGraphState(Key);
					if (!TestNotNull(TEXT("the record"), Entry))
					{
						return;
					}
					TestEqual(TEXT("the record stores the language id"), Entry->Translation.Language, FString(Language.Id));
					TestEqual(TEXT("the record's path is relative only for a folder inside the project"),
						FPaths::IsRelative(Entry->Translation.OutputPath), IsRelativeKind(Kind));
					TestEqual(TEXT("the record's path resolves to the graph folder"),
						NormalizedDir(FN2CPathUtils::ResolveTranslationRecordPath(Entry->Translation.OutputPath)), NormalizedDir(GraphDir));

					// Through a new manager, as after an editor restart
					State = OpenManager();
					FN2CGraphTranslation Loaded;
					EN2CCodeLanguage LoadedLanguage = EN2CCodeLanguage::Cpp;
					if (!TestTrue(TEXT("LoadTranslation finds it"), State->LoadTranslation(Key, Loaded, LoadedLanguage)))
					{
						return;
					}
					TestEqual(TEXT("in its language"), static_cast<int32>(LoadedLanguage), static_cast<int32>(Language.Enum));
					TestEqual(TEXT("the graph name"), Loaded.GraphName, GraphName);
					TestEqual(TEXT("the implementation"), Loaded.Code.GraphImplementation, Saved.Implementation);
					TestEqual(TEXT("the declaration (empty for a language without one)"), Loaded.Code.GraphDeclaration, Saved.Declaration);
					TestEqual(TEXT("the notes"), Loaded.Code.ImplementationNotes, Saved.Notes);
					FString Json;
					TestTrue(TEXT("LoadN2CJson finds the JSON next to it"), State->LoadN2CJson(Key, Json));
					TestEqual(TEXT("the JSON"), Json, Saved.Json);
				});
			}
		}

		It(TEXT("finds a translation of a graph whose name holds path characters"), [this]()
		{
			const FN2CTargetLanguage& Language = N2C::GetLanguage(EN2CCodeLanguage::JavaScript);
			const FString GraphName = TEXT("Fire/Reload: Weapon?");
			const FString GraphDir = Scratch->Path / TEXT("Translations") / FN2CPathUtils::GetTranslationFileStem(GraphName);
			TestNotEqual(TEXT("the file stem is not the raw name"), FN2CPathUtils::GetTranslationFileStem(GraphName), GraphName);
			const FSavedFiles Saved = WriteLikeTheBatch(GraphDir, GraphName, Language);

			UN2CGraphStateManager* State = OpenManager();
			const FN2CGraphKey Key(FSoftObjectPath(TEXT("/Game/BP_Load.BP_Load")), FGuid::NewGuid());
			State->RecordSavedTranslation(Key.GraphGuid, GraphName, Key.Blueprint, GraphDir, TEXT("Anthropic"), TEXT("model"),
				Language.Enum, FN2CTranslationSummary());
			FN2CGraphTranslation Loaded;
			TestTrue(TEXT("LoadTranslation finds it"), State->LoadTranslation(Key, Loaded));
			TestEqual(TEXT("the implementation"), Loaded.Code.GraphImplementation, Saved.Implementation);
			TestEqual(TEXT("the notes"), Loaded.Code.ImplementationNotes, Saved.Notes);
		});

		It(TEXT("takes the folder's only implementation file when the LLM named the graph differently"), [this]()
		{
			const FString GraphDir = Scratch->Path / TEXT("Single");
			FFileHelper::SaveStringToFile(TEXT("def receive_begin_play(self): pass"), *(GraphDir / TEXT("ReceiveBeginPlay.py")));
			FFileHelper::SaveStringToFile(TEXT("Python notes"), *(GraphDir / TEXT("ReceiveBeginPlay_Notes.txt")));
			UN2CGraphStateManager* State = OpenManager();
			const FN2CGraphKey Key(FSoftObjectPath(TEXT("/Game/BP_Single.BP_Single")), FGuid::NewGuid());
			State->RecordSavedTranslation(Key.GraphGuid, TEXT("EventGraph"), Key.Blueprint, GraphDir, TEXT("Anthropic"),
				TEXT("model"), EN2CCodeLanguage::Python, FN2CTranslationSummary());
			FN2CGraphTranslation Loaded;
			if (TestTrue(TEXT("LoadTranslation finds it"), State->LoadTranslation(Key, Loaded)))
			{
				TestEqual(TEXT("the implementation"), Loaded.Code.GraphImplementation, FString(TEXT("def receive_begin_play(self): pass")));
				TestEqual(TEXT("its notes"), Loaded.Code.ImplementationNotes, FString(TEXT("Python notes")));
			}
		});
	});

	Describe(TEXT("two Blueprints with the same graph GUID"), [this]()
	{
		It(TEXT("keep separate tags, translations and exports, through a save and a reload"), [this]()
		{
			// What duplicating a Blueprint in the Content Browser leaves: the copy's graphs keep the original's GUIDs
			const FGuid Guid = FGuid::NewGuid();
			const FN2CGraphKey Original(FSoftObjectPath(TEXT("/Game/Enemies/BP_Enemy.BP_Enemy")), Guid);
			const FN2CGraphKey Copy(FSoftObjectPath(TEXT("/Game/Enemies/BP_Enemy_Ranged.BP_Enemy_Ranged")), Guid);
			const FN2CTargetLanguage& Cpp = N2C::GetLanguage(EN2CCodeLanguage::Cpp);
			const FN2CTargetLanguage& Python = N2C::GetLanguage(EN2CCodeLanguage::Python);

			UN2CGraphStateManager* State = OpenManager();
			State->AddTag(Guid, TEXT("EventGraph"), Original.Blueprint, TEXT("Melee"), TEXT("Combat"));
			State->AddTag(Guid, TEXT("EventGraph"), Copy.Blueprint, TEXT("Ranged"), TEXT("Combat"));
			const FString OriginalDir = Scratch->Path / TEXT("Translations/BP_Enemy/EventGraph");
			const FString CopyDir = Scratch->Path / TEXT("Translations/BP_Enemy_Ranged/EventGraph");
			const FSavedFiles OriginalFiles = WriteLikeTheBatch(OriginalDir, TEXT("EventGraph"), Cpp);
			const FSavedFiles CopyFiles = WriteLikeTheBatch(CopyDir, TEXT("EventGraph"), Python);
			State->RecordSavedTranslation(Guid, TEXT("EventGraph"), Original.Blueprint, OriginalDir, TEXT("Anthropic"), TEXT("model"),
				Cpp.Enum, FN2CTranslationSummary());
			State->RecordSavedTranslation(Guid, TEXT("EventGraph"), Copy.Blueprint, CopyDir, TEXT("Anthropic"), TEXT("model"),
				Python.Enum, FN2CTranslationSummary());
			State->SetJsonExportState(Copy, [&]()
			{
				FN2CJsonExportState Export;
				Export.bExists = true;
				Export.OutputPath = TEXT("Saved/Exports/BP_Enemy_Ranged.json");
				return Export;
			}());

			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const FString When = Pass == 0 ? TEXT("in memory") : TEXT("after a reload");
				if (Pass == 1)
				{
					State = OpenManager();
				}
				TestEqual(When + TEXT(": two entries"), State->GetAllGraphStates().Num(), 2);
				TestEqual(When + TEXT(": FindKeysForGuid lists both"), State->FindKeysForGuid(Guid).Num(), 2);
				TestTrue(When + TEXT(": the original has only its tag"), State->GraphHasTag(Original, TEXT("Melee"))
					&& !State->GraphHasTag(Original, TEXT("Ranged")) && State->GetTagsForGraph(Original).Num() == 1);
				TestTrue(When + TEXT(": the copy has only its tag"), State->GraphHasTag(Copy, TEXT("Ranged"))
					&& !State->GraphHasTag(Copy, TEXT("Melee")) && State->GetTagsForGraph(Copy).Num() == 1);
				TestFalse(When + TEXT(": only the copy has an export"), State->HasJsonExport(Original));
				TestTrue(When + TEXT(": the copy's export"), State->HasJsonExport(Copy));

				FN2CGraphTranslation Loaded;
				EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
				TestTrue(When + TEXT(": the original's translation loads"), State->LoadTranslation(Original, Loaded, Language));
				TestEqual(When + TEXT(": it is the original's C++"), Loaded.Code.GraphImplementation, OriginalFiles.Implementation);
				TestEqual(When + TEXT(": with its header"), Loaded.Code.GraphDeclaration, OriginalFiles.Declaration);
				TestTrue(When + TEXT(": the copy's translation loads"), State->LoadTranslation(Copy, Loaded, Language));
				TestEqual(When + TEXT(": it is the copy's Python"), Loaded.Code.GraphImplementation, CopyFiles.Implementation);
				TestEqual(When + TEXT(": in Python"), static_cast<int32>(Language), static_cast<int32>(EN2CCodeLanguage::Python));

				// A bare GUID names neither
				TestNull(When + TEXT(": a bare GUID finds no entry"), State->FindGraphState(Guid));
				TestFalse(When + TEXT(": a bare GUID loads no translation"), State->LoadTranslation(Guid, Loaded));
				TestFalse(When + TEXT(": a bare GUID removes no tag"), State->RemoveTag(Guid, TEXT("Melee"), TEXT("")));
			}

			// Changes to one leave the other alone
			TestEqual(TEXT("removing the original's tags removes one"), State->RemoveAllTagsFromGraph(Original), 1);
			TestEqual(TEXT("the copy keeps its tag"), State->GetTagsForGraph(Copy).Num(), 1);
			State->ClearTranslationState(Copy);
			TestTrue(TEXT("the original keeps its translation"), State->HasTranslation(Original));
			TestFalse(TEXT("the copy's is cleared"), State->HasTranslation(Copy));
			TestTrue(TEXT("removing the original's entry"), State->RemoveGraphState(Original));
			const FN2CGraphState* Left = State->FindGraphState(Guid);
			TestTrue(TEXT("a bare GUID finds the one entry left, the copy's"), Left && Left->OwningBlueprint == Copy.Blueprint
				&& Left->HasJsonExport() && Left->Tags.Num() == 1);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
