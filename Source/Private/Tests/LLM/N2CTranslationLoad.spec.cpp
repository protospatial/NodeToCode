// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/Widgets/SN2CMainWindow.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CPathUtils.h"

// The graph state singleton, swapped for an isolated manager so the host project's BlueprintGraphState.json is untouched
N2C_TEST_PRIVATE_MEMBER(FN2CLoadStateInstance, UN2CGraphStateManager**, &UN2CGraphStateManager::Instance)

// The batch writer and the folder it writes into
N2C_TEST_PRIVATE_MEMBER(FN2CLoadSaveItemTranslation,
	void (UN2CBatchTranslationOrchestrator::*)(const FN2CBatchTranslationItem&),
	&UN2CBatchTranslationOrchestrator::SaveItemTranslation)
N2C_TEST_PRIVATE_MEMBER(FN2CLoadBatchOutputPath, FString UN2CBatchTranslationOrchestrator::*,
	&UN2CBatchTranslationOrchestrator::BatchOutputPath)

// The LLM module's latest-save fields, put back after the test
N2C_TEST_PRIVATE_MEMBER(FN2CLoadLatestPath, FString UN2CLLMModule::*, &UN2CLLMModule::LatestTranslationPath)
N2C_TEST_PRIVATE_MEMBER(FN2CLoadLatestLanguage, EN2CCodeLanguage UN2CLLMModule::*, &UN2CLLMModule::LatestTranslationLanguage)

/**
 * A saved translation loads in the translation viewer for every target language (card 0438). The viewer reads
 * through UN2CGraphStateManager::LoadTranslation (also UN2CTagBlueprintLibrary::LoadTranslation), which picks the
 * implementation file from the language the writer recorded. Before 0438 the batch recorded EN2CCodeLanguage::Python,
 * the main window recorded CPP for every language, and the loader expected Python and Pseudocode as .txt, so only
 * C++ loaded. Checks: each writer (the batch orchestrator, the LLM module's save plus the main window's record) saves
 * each of the six languages and the reader gets the code back; the record keeps the canonical id across a reload of
 * the state file; folders recorded by older builds (enum-qualified id, CPP, Pseudocode as .txt) still load.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationLoadSpec, "NodeToCode.LLM.TranslationLoad", N2C_TEST_FLAGS)

	struct FLanguageCase
	{
		EN2CCodeLanguage Language;
		const TCHAR* Id;
		const TCHAR* Extension;
		const TCHAR* Code;
	};

	static TArray<FLanguageCase> Languages()
	{
		return {
			{EN2CCodeLanguage::Cpp, TEXT("Cpp"), TEXT(".cpp"), TEXT("void AActor::Run()\n{\n    UE_LOG(LogTemp, Log, TEXT(\"cpp\"));\n}\n")},
			{EN2CCodeLanguage::Python, TEXT("Python"), TEXT(".py"), TEXT("def run(self):\n    print(\"python\")\n")},
			{EN2CCodeLanguage::JavaScript, TEXT("JavaScript"), TEXT(".js"), TEXT("function run() {\n  console.log(\"javascript\");\n}\n")},
			{EN2CCodeLanguage::CSharp, TEXT("CSharp"), TEXT(".cs"), TEXT("public void Run()\n{\n    Console.WriteLine(\"csharp\");\n}\n")},
			{EN2CCodeLanguage::Swift, TEXT("Swift"), TEXT(".swift"), TEXT("func run() {\n    print(\"swift\")\n}\n")},
			{EN2CCodeLanguage::Pseudocode, TEXT("Pseudocode"), TEXT(".md"), TEXT("1. Print \"pseudocode\"\n")},
		};
	}

	/** An isolated graph state manager as UN2CGraphStateManager::Get() and the target language, for the life of the scope */
	struct FScopedState
	{
		TStrongObjectPtr<UN2CGraphStateManager> Manager;
		UN2CGraphStateManager* SavedInstance = nullptr;
		EN2CCodeLanguage SavedLanguage;
		FString SavedOutput;
		FString SavedLatestPath;
		EN2CCodeLanguage SavedLatestLanguage;

		FScopedState(const FString& StateDir, const FString& OutputDir)
		{
			UN2CGraphStateManager*& Instance = *N2CTest::GetPrivateMember(N2CTest::FN2CLoadStateInstance());
			SavedInstance = Instance;
			Manager.Reset(UN2CGraphStateManager::CreateIsolated(StateDir));
			Instance = Manager.Get();

			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			SavedLanguage = Settings->TargetLanguage;
			SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
			Settings->CustomTranslationOutputDirectory.Path = OutputDir;

			UN2CLLMModule* Module = UN2CLLMModule::Get();
			SavedLatestPath = Module->*N2CTest::GetPrivateMember(N2CTest::FN2CLoadLatestPath());
			SavedLatestLanguage = Module->*N2CTest::GetPrivateMember(N2CTest::FN2CLoadLatestLanguage());
		}

		~FScopedState()
		{
			*N2CTest::GetPrivateMember(N2CTest::FN2CLoadStateInstance()) = SavedInstance;
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->TargetLanguage = SavedLanguage;
			Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
			UN2CLLMModule* Module = UN2CLLMModule::Get();
			Module->*N2CTest::GetPrivateMember(N2CTest::FN2CLoadLatestPath()) = SavedLatestPath;
			Module->*N2CTest::GetPrivateMember(N2CTest::FN2CLoadLatestLanguage()) = SavedLatestLanguage;
		}
	};

	static FN2CGraphTranslation MakeGraph(const FString& GraphName, const FLanguageCase& Case)
	{
		FN2CGraphTranslation Graph;
		Graph.GraphName = GraphName;
		Graph.GraphType = TEXT("Function");
		Graph.Code.GraphImplementation = Case.Code;
		Graph.Code.ImplementationNotes = FString::Printf(TEXT("notes for %s"), Case.Id);
		if (Case.Language == EN2CCodeLanguage::Cpp)
		{
			Graph.Code.GraphDeclaration = TEXT("void Run();");
		}
		return Graph;
	}

	static FN2CTagInfo MakeTag(const FString& GraphName)
	{
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("N2CLoadTest");
		Tag.GraphGuid = FGuid::NewGuid().ToString();
		Tag.GraphName = GraphName;
		Tag.BlueprintPath = TEXT("/Game/N2CTest/BP_N2CLoad.BP_N2CLoad");
		return Tag;
	}

	/** Loads Tag's translation the way the viewer does and checks it holds Case's code */
	void ExpectLoads(const FString& What, const FN2CTagInfo& Tag, const FLanguageCase& Case)
	{
		FN2CGraphTranslation Loaded;
		if (TestTrue(FString::Printf(TEXT("%s: %s loads"), *What, Case.Id), UN2CTagBlueprintLibrary::LoadTranslation(Tag.GraphGuid, Loaded)))
		{
			TestEqual(FString::Printf(TEXT("%s: %s implementation"), *What, Case.Id), Loaded.Code.GraphImplementation, FString(Case.Code));
			TestEqual(FString::Printf(TEXT("%s: %s notes"), *What, Case.Id), Loaded.Code.ImplementationNotes,
				FString::Printf(TEXT("notes for %s"), Case.Id));
			TestEqual(FString::Printf(TEXT("%s: %s declaration only for C++"), *What, Case.Id), Loaded.Code.GraphDeclaration.IsEmpty(),
				Case.Language != EN2CCodeLanguage::Cpp);
		}
	}

	/** The language id recorded for Tag */
	static FString RecordedLanguage(const FN2CTagInfo& Tag)
	{
		FGuid Guid;
		FGuid::Parse(Tag.GraphGuid, Guid);
		const FN2CGraphState* State = UN2CGraphStateManager::Get().FindGraphState(Guid);
		return State ? State->Translation.Language : FString();
	}

END_DEFINE_SPEC(FN2CTranslationLoadSpec)

void FN2CTranslationLoadSpec::Define()
{
	Describe(TEXT("Language ids"), [this]()
	{
		It(TEXT("map every language to one id and extension, and read the older forms"), [this]()
		{
			for (const FLanguageCase& Case : Languages())
			{
				TestEqual(FString::Printf(TEXT("%s id"), Case.Id), FN2CPathUtils::GetLanguageId(Case.Language), FString(Case.Id));
				TestEqual(FString::Printf(TEXT("%s extension"), Case.Id), FN2CPathUtils::GetFileExtensionForLanguage(Case.Language),
					FString(Case.Extension));
				EN2CCodeLanguage Parsed = EN2CCodeLanguage::Cpp;
				TestTrue(FString::Printf(TEXT("%s parses"), Case.Id), FN2CPathUtils::ParseLanguageId(Case.Id, Parsed) && Parsed == Case.Language);
				const FString Qualified = UEnum::GetValueAsString(Case.Language);
				TestTrue(FString::Printf(TEXT("%s parses"), *Qualified), FN2CPathUtils::ParseLanguageId(Qualified, Parsed) && Parsed == Case.Language);
				TestTrue(FString::Printf(TEXT("%s parses in lower case"), Case.Id),
					FN2CPathUtils::ParseLanguageId(FString(Case.Id).ToLower(), Parsed) && Parsed == Case.Language);
			}
			EN2CCodeLanguage Parsed = EN2CCodeLanguage::Python;
			TestTrue(TEXT("CPP (the main window's old id) is C++"), FN2CPathUtils::ParseLanguageId(TEXT("CPP"), Parsed) && Parsed == EN2CCodeLanguage::Cpp);
			TestTrue(TEXT("C# is CSharp"), FN2CPathUtils::ParseLanguageId(TEXT("C#"), Parsed) && Parsed == EN2CCodeLanguage::CSharp);
			TestFalse(TEXT("an empty id is unknown"), FN2CPathUtils::ParseLanguageId(TEXT(""), Parsed));
			TestFalse(TEXT("cobol is unknown"), FN2CPathUtils::ParseLanguageId(TEXT("cobol"), Parsed));
			TestEqual(TEXT("Pseudocode also loads as .txt"), FN2CPathUtils::GetTranslationFileExtensions(EN2CCodeLanguage::Pseudocode),
				TArray<FString>{TEXT(".md"), TEXT(".txt")});
		});
	});

	Describe(TEXT("Batch translation"), [this]()
	{
		It(TEXT("saves each language where the viewer loads it, and the record survives a reload"), [this]()
		{
			N2CTest::FScratchDir StateDir(TEXT("N2CLoadState"));
			N2CTest::FScratchDir Output(TEXT("N2CLoadBatch"));
			FScopedState Scoped(StateDir.Path, Output.Path);

			TStrongObjectPtr<UN2CBatchTranslationOrchestrator> Orchestrator(NewObject<UN2CBatchTranslationOrchestrator>());
			Orchestrator.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CLoadBatchOutputPath()) = Output / TEXT("BatchTranslation_Test");
			IFileManager::Get().MakeDirectory(*(Output / TEXT("BatchTranslation_Test")), true);

			TArray<FN2CTagInfo> Tags;
			for (const FLanguageCase& Case : Languages())
			{
				GetMutableDefault<UN2CSettings>()->TargetLanguage = Case.Language;
				FN2CBatchTranslationItem Item;
				Item.TagInfo = MakeTag(FString::Printf(TEXT("Batch%s"), Case.Id));
				Item.TranslationResponse.Graphs.Add(MakeGraph(Item.TagInfo.GraphName, Case));
				Item.CachedJsonPayload = TEXT("{}");
				(Orchestrator.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CLoadSaveItemTranslation()))(Item);
				Tags.Add(Item.TagInfo);

				TestTrue(FString::Printf(TEXT("batch %s file written as %s"), Case.Id, Case.Extension), FPaths::FileExists(
					Output / TEXT("BatchTranslation_Test") / Item.TagInfo.GraphName / (Item.TagInfo.GraphName + Case.Extension)));
				TestEqual(FString::Printf(TEXT("batch %s recorded as its id"), Case.Id), RecordedLanguage(Item.TagInfo), FString(Case.Id));
				ExpectLoads(TEXT("batch"), Item.TagInfo, Case);
			}

			// The state file round-trips the id and the path
			*N2CTest::GetPrivateMember(N2CTest::FN2CLoadStateInstance()) = nullptr;
			Scoped.Manager->SaveState();
			Scoped.Manager.Reset(UN2CGraphStateManager::CreateIsolated(StateDir.Path));
			*N2CTest::GetPrivateMember(N2CTest::FN2CLoadStateInstance()) = Scoped.Manager.Get();
			for (int32 Index = 0; Index < Tags.Num(); ++Index)
			{
				TestEqual(FString::Printf(TEXT("reloaded %s id"), Languages()[Index].Id), RecordedLanguage(Tags[Index]), FString(Languages()[Index].Id));
				ExpectLoads(TEXT("batch after reload"), Tags[Index], Languages()[Index]);
			}
		});
	});

	Describe(TEXT("Single translation"), [this]()
	{
		It(TEXT("saves each language where the viewer loads it, also when the response names the graph differently"), [this]()
		{
			N2CTest::FScratchDir StateDir(TEXT("N2CLoadState"));
			// Outside the project folder too: a custom output directory can be anywhere, so the record is absolute
			for (const bool bOutsideProject : {false, true})
			{
				const FString OutputPath = bOutsideProject
					? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserTempDir(),
						FString::Printf(TEXT("N2CLoadSingle_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))))
					: FString();
				N2CTest::FScratchDir InProject(TEXT("N2CLoadSingle"));
				const FString Output = bOutsideProject ? OutputPath : InProject.Path;
				IFileManager::Get().MakeDirectory(*Output, true);
				{
					FScopedState Scoped(StateDir.Path, Output);
					UN2CLLMModule* Module = UN2CLLMModule::Get();
					// A valid capture to save beside the translation (SaveTranslationToDisk writes it as N2C_BP_*.json)
					FString FixtureJson;
					FN2CBlueprint Fixture;
					if (!TestTrue(TEXT("the capture fixture loads"), FFileHelper::LoadFileToString(FixtureJson,
						*(N2CTest::FixturesDir(TEXT("Capture/V1")) / TEXT("FanOutData.json")))
						&& FN2CSerializer::FromJson(FixtureJson, Fixture)))
					{
						return;
					}
					for (const FLanguageCase& Case : Languages())
					{
						for (const bool bRenamed : {false, true})
						{
							GetMutableDefault<UN2CSettings>()->TargetLanguage = Case.Language;
							const FN2CTagInfo Tag = MakeTag(FString::Printf(TEXT("Single%s"), Case.Id));
							FN2CTranslationResponse Response;
							Response.Graphs.Add(MakeGraph(bRenamed ? Tag.GraphName + TEXT("_FromLLM") : Tag.GraphName, Case));
							FN2CBlueprint Blueprint = Fixture;
							Blueprint.Metadata.Name = FString::Printf(TEXT("BP_N2CLoad%s%s"), Case.Id, bRenamed ? TEXT("R") : TEXT(""));

							const FString What = FString::Printf(TEXT("single%s%s"), bRenamed ? TEXT(" (renamed)") : TEXT(""),
								bOutsideProject ? TEXT(" outside the project") : TEXT(""));
							if (!TestTrue(What + TEXT(": saved"), Module->SaveTranslationToDisk(Response, Blueprint))
								|| !TestTrue(What + TEXT(": recorded"), SN2CMainWindow::RecordSingleTranslation(Tag, Response)))
							{
								continue;
							}
							TestEqual(What + TEXT(": recorded as its id"), RecordedLanguage(Tag), FString(Case.Id));
							ExpectLoads(What, Tag, Case);
						}
					}
				}
				if (bOutsideProject)
				{
					IFileManager::Get().DeleteDirectory(*OutputPath, false, true);
				}
			}
		});
	});

	Describe(TEXT("Folders saved by older builds"), [this]()
	{
		It(TEXT("still load: enum-qualified ids, CPP, and Pseudocode saved as .txt"), [this]()
		{
			N2CTest::FScratchDir StateDir(TEXT("N2CLoadState"));
			N2CTest::FScratchDir Output(TEXT("N2CLoadLegacy"));
			FScopedState Scoped(StateDir.Path, Output.Path);

			struct FLegacy
			{
				FLanguageCase Case;
				FString StoredId;
				FString FileExtension;
			};
			TArray<FLegacy> Legacy;
			for (const FLanguageCase& Case : Languages())
			{
				// What batch translations before 0438 recorded
				Legacy.Add({Case, UEnum::GetValueAsString(Case.Language), Case.Extension});
			}
			const FLanguageCase& Cpp = Languages()[0];
			const FLanguageCase& Pseudo = Languages()[5];
			Legacy.Add({Cpp, TEXT("CPP"), TEXT(".cpp")});                // the main window's old id
			Legacy.Add({Pseudo, TEXT("Pseudocode"), TEXT(".txt")});      // Pseudocode at the old loader's extension
			Legacy.Add({Pseudo, TEXT("EN2CCodeLanguage::Pseudocode"), TEXT(".txt")});

			int32 Index = 0;
			for (const FLegacy& Entry : Legacy)
			{
				const FN2CTagInfo Tag = MakeTag(FString::Printf(TEXT("Legacy%d"), Index++));
				const FString GraphDir = Output / Tag.GraphName;
				FFileHelper::SaveStringToFile(FString(Entry.Case.Code), *(GraphDir / (Tag.GraphName + Entry.FileExtension)));
				FFileHelper::SaveStringToFile(FString::Printf(TEXT("notes for %s"), Entry.Case.Id), *(GraphDir / (Tag.GraphName + TEXT("_Notes.txt"))));
				if (Entry.Case.Language == EN2CCodeLanguage::Cpp)
				{
					FFileHelper::SaveStringToFile(FString(TEXT("void Run();")), *(GraphDir / (Tag.GraphName + TEXT(".h"))));
				}
				FGuid Guid;
				FGuid::Parse(Tag.GraphGuid, Guid);
				// The raw record an older build wrote: project-relative path and its language string
				FString RelativeDir = GraphDir;
				FPaths::MakePathRelativeTo(RelativeDir, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
				UN2CGraphStateManager::Get().SetTranslationState(Guid, Tag.GraphName, FSoftObjectPath(Tag.BlueprintPath),
					RelativeDir, TEXT("EN2CLLMProvider::Anthropic"), TEXT("claude-haiku-5-5"), Entry.StoredId, FN2CTranslationSummary());
				ExpectLoads(FString::Printf(TEXT("legacy '%s' %s"), *Entry.StoredId, *Entry.FileExtension), Tag, Entry.Case);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
