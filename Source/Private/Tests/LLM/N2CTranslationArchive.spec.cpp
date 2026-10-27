// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "LLM/N2CTranslationArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "Utils/N2CPathUtils.h"

/**
 * The translation archive (card 0252) is the one writer of translation folders. Checks, with a fixture response of
 * a root graph, a nested graph, a struct and an enum, in C++ and in Python, written to a scratch base folder: every
 * entry gets its own folder and files (a struct no longer overwrites the root graph's files), the declaration is
 * written for Python too, manifest.json names the run and every entry with its role and files, two runs started in
 * the same second get two folders, and a batch run keeps each item in its own folder. Also: the base path is
 * absolute (a relative custom folder is under the project), and no temporary file is left behind.
 */
BEGIN_DEFINE_SPEC(FN2CTranslationArchiveSpec, "NodeToCode.Translation.Archive", N2C_TEST_FLAGS)

	static FN2CGraphTranslation Entry(const TCHAR* Name, const TCHAR* Type, const FString& Declaration, const FString& Implementation)
	{
		FN2CGraphTranslation Graph;
		Graph.GraphName = Name;
		Graph.GraphType = Type;
		Graph.Code.GraphDeclaration = Declaration;
		Graph.Code.GraphImplementation = Implementation;
		Graph.Code.ImplementationNotes = FString::Printf(TEXT("notes for %s"), Name);
		return Graph;
	}

	/** EventGraph (root), a nested function, a struct and an enum, as the prompts ask each language to return them */
	static FN2CTranslationResponse MakeResponse(EN2CCodeLanguage Language)
	{
		FN2CTranslationResponse Response;
		Response.Usage.InputTokens = 120;
		Response.Usage.OutputTokens = 80;
		if (Language == EN2CCodeLanguage::Cpp)
		{
			Response.Graphs.Add(Entry(TEXT("EventGraph"), TEXT("EventGraph"), TEXT("void BeginPlay();"), TEXT("void ABP::BeginPlay() {}")));
			Response.Graphs.Add(Entry(TEXT("Helper"), TEXT("Function"), TEXT("int32 Helper();"), TEXT("int32 ABP::Helper() { return 1; }")));
			Response.Graphs.Add(Entry(TEXT("S_Item"), TEXT("Struct"), TEXT("USTRUCT() struct FS_Item { int32 Count; };"), TEXT("")));
			Response.Graphs.Add(Entry(TEXT("E_Level"), TEXT("Enum"), TEXT("UENUM() enum class EE_Level : uint8 { Low, High };"), TEXT("")));
		}
		else
		{
			// A response that puts the definitions in graphDeclaration, as the C#, Swift and JavaScript prompts ask
			Response.Graphs.Add(Entry(TEXT("EventGraph"), TEXT("EventGraph"), TEXT(""), TEXT("def begin_play(self):\n    pass\n")));
			Response.Graphs.Add(Entry(TEXT("Helper"), TEXT("Function"), TEXT("# helper(self) -> int"), TEXT("def helper(self):\n    return 1\n")));
			Response.Graphs.Add(Entry(TEXT("S_Item"), TEXT("Struct"), TEXT("class S_Item:\n    count: int\n"), TEXT("")));
			Response.Graphs.Add(Entry(TEXT("E_Level"), TEXT("Enum"), TEXT("class E_Level(Enum):\n    LOW = 0\n"), TEXT("")));
		}
		return Response;
	}

	static FString Load(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	static TSharedPtr<FJsonObject> LoadManifest(const FString& RunFolder)
	{
		return N2CTest::ParseJsonObject(Load(RunFolder / TEXT("manifest.json")));
	}

	/** Every file under Folder, recursively */
	static TArray<FString> FilesUnder(const FString& Folder)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *Folder, TEXT("*"), true, false);
		return Found;
	}

	static FN2CTranslationRunInfo RunInfo(EN2CTranslationRunKind Kind, EN2CCodeLanguage Language, const TCHAR* Name)
	{
		FN2CTranslationRunInfo Info;
		Info.Kind = Kind;
		Info.Name = Name;
		Info.Provider = TEXT("Anthropic");
		Info.Model = TEXT("claude-haiku-5-5");
		Info.Language = Language;
		return Info;
	}

END_DEFINE_SPEC(FN2CTranslationArchiveSpec)

void FN2CTranslationArchiveSpec::Define()
{
	It(TEXT("writes every entry of a response to its own files, with Python declarations, and a manifest"), [this]()
	{
		N2CTest::FScratchDir Base(TEXT("N2CArchive"));
		struct FCase
		{
			EN2CCodeLanguage Language;
			const TCHAR* Root;
			const TCHAR* Helper;
			const TCHAR* HelperDeclaration;
			const TCHAR* Struct;
			const TCHAR* Enum;
		};
		const FCase Cases[] = {
			{EN2CCodeLanguage::Cpp, TEXT("EventGraph/EventGraph.cpp"), TEXT("Helper/Helper.cpp"), TEXT("Helper/Helper.h"),
				TEXT("S_Item/S_Item.h"), TEXT("E_Level/E_Level.h")},
			{EN2CCodeLanguage::Python, TEXT("EventGraph/EventGraph.py"), TEXT("Helper/Helper.py"), TEXT("Helper/Helper_Declaration.py"),
				TEXT("S_Item/S_Item.py"), TEXT("E_Level/E_Level.py")},
		};
		for (const FCase& Case : Cases)
		{
			const FString Id = N2C::GetLanguage(Case.Language).Id;
			const TSharedPtr<FN2CTranslationArchive> Run = FN2CTranslationArchive::BeginRun(
				RunInfo(EN2CTranslationRunKind::Single, Case.Language, TEXT("BP_N2CArchive")), Base.Path);
			if (!TestTrue(Id + TEXT(": the run starts"), Run.IsValid()))
			{
				continue;
			}
			TestTrue(Id + TEXT(": the run folder is under the base"), FPaths::IsUnderDirectory(Run->GetFolder(), Base.Path));
			TestTrue(Id + TEXT(": the run folder is named after the Blueprint"),
				FPaths::GetCleanFilename(Run->GetFolder()).StartsWith(TEXT("BP_N2CArchive_")));

			const FN2CTranslationResponse Response = MakeResponse(Case.Language);
			FN2CArchiveSource Source;
			Source.InputJson = TEXT("{\"version\":\"2.0.0\"}");
			Source.GraphName = TEXT("EventGraph");
			FN2CArchivedResponse Written;
			if (!TestTrue(Id + TEXT(": the response is written"), Run->WriteResponse(Response, Source, FString(), Written)))
			{
				continue;
			}

			const FString Folder = Run->GetFolder();
			TestEqual(Id + TEXT(": four entries"), Written.Entries.Num(), 4);
			TestTrue(Id + TEXT(": the root entry is EventGraph"), Written.GetRoot() && Written.GetRoot()->GraphName == TEXT("EventGraph"));
			TestEqual(Id + TEXT(": root implementation"), Load(Folder / Case.Root), Response.Graphs[0].Code.GraphImplementation);
			TestEqual(Id + TEXT(": nested implementation"), Load(Folder / Case.Helper), Response.Graphs[1].Code.GraphImplementation);
			TestEqual(Id + TEXT(": nested declaration"), Load(Folder / Case.HelperDeclaration), Response.Graphs[1].Code.GraphDeclaration);
			TestEqual(Id + TEXT(": struct definition"), Load(Folder / Case.Struct), Response.Graphs[2].Code.GraphDeclaration);
			TestEqual(Id + TEXT(": enum definition"), Load(Folder / Case.Enum), Response.Graphs[3].Code.GraphDeclaration);
			TestEqual(Id + TEXT(": root notes"), Load(Folder / TEXT("EventGraph/EventGraph_Notes.txt")), FString(TEXT("notes for EventGraph")));
			TestTrue(Id + TEXT(": the N2C JSON sent is beside the translation"),
				FPaths::FileExists(Folder / FString::Printf(TEXT("N2C_BP_%s.json"), *FPaths::GetCleanFilename(Folder))));
			TestFalse(Id + TEXT(": no temporary file is left"), FilesUnder(Folder).ContainsByPredicate(
				[](const FString& File) { return File.EndsWith(TEXT(".tmp")); }));

			const TSharedPtr<FJsonObject> Manifest = LoadManifest(Folder);
			if (!TestTrue(Id + TEXT(": manifest.json parses"), Manifest.IsValid()))
			{
				continue;
			}
			TestEqual(Id + TEXT(": schemaVersion"), static_cast<int32>(Manifest->GetNumberField(TEXT("schemaVersion"))),
				FN2CTranslationArchive::ManifestSchemaVersion);
			TestEqual(Id + TEXT(": runId"), Manifest->GetStringField(TEXT("runId")), Run->GetRunId());
			TestEqual(Id + TEXT(": kind"), Manifest->GetStringField(TEXT("kind")), FString(TEXT("single")));
			TestEqual(Id + TEXT(": provider"), Manifest->GetStringField(TEXT("provider")), FString(TEXT("Anthropic")));
			TestEqual(Id + TEXT(": model"), Manifest->GetStringField(TEXT("model")), FString(TEXT("claude-haiku-5-5")));
			TestEqual(Id + TEXT(": language"), Manifest->GetStringField(TEXT("language")), Id);
			TestFalse(Id + TEXT(": timestamp"), Manifest->GetStringField(TEXT("timestamp")).IsEmpty());
			TestTrue(Id + TEXT(": stopReason and requestIds"), Manifest->HasField(TEXT("stopReason")) && Manifest->HasField(TEXT("requestIds")));
			const TSharedPtr<FJsonObject>* Usage = nullptr;
			if (TestTrue(Id + TEXT(": usage"), Manifest->TryGetObjectField(TEXT("usage"), Usage)))
			{
				TestEqual(Id + TEXT(": input tokens"), static_cast<int32>((*Usage)->GetNumberField(TEXT("input_tokens"))), 120);
				TestEqual(Id + TEXT(": output tokens"), static_cast<int32>((*Usage)->GetNumberField(TEXT("output_tokens"))), 80);
			}
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (TestTrue(Id + TEXT(": entries"), Manifest->TryGetArrayField(TEXT("entries"), Entries)) && TestEqual(Id + TEXT(": four entries listed"), Entries->Num(), 4))
			{
				const TCHAR* Roles[] = {TEXT("root"), TEXT("nested"), TEXT("struct"), TEXT("enum")};
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const TSharedPtr<FJsonObject> Listed = (*Entries)[Index]->AsObject();
					TestEqual(FString::Printf(TEXT("%s: entry %d graphName"), *Id, Index), Listed->GetStringField(TEXT("graphName")),
						Response.Graphs[Index].GraphName);
					TestEqual(FString::Printf(TEXT("%s: entry %d role"), *Id, Index), Listed->GetStringField(TEXT("role")), FString(Roles[Index]));
					const TSharedPtr<FJsonObject>* Files = nullptr;
					if (TestTrue(FString::Printf(TEXT("%s: entry %d files"), *Id, Index), Listed->TryGetObjectField(TEXT("files"), Files)))
					{
						const FString Implementation = (*Files)->GetStringField(TEXT("implementation"));
						const FString Declaration = (*Files)->GetStringField(TEXT("declaration"));
						TestTrue(FString::Printf(TEXT("%s: entry %d's listed files exist"), *Id, Index),
							(Implementation.IsEmpty() || FPaths::FileExists(Folder / Implementation))
							&& (Declaration.IsEmpty() || FPaths::FileExists(Folder / Declaration)));
					}
				}
				TestEqual(Id + TEXT(": the struct's declaration is listed"),
					(*Entries)[2]->AsObject()->GetObjectField(TEXT("files"))->GetStringField(TEXT("declaration")), FString(Case.Struct));
			}
		}
	});

	It(TEXT("gives two runs started in the same second their own folders"), [this]()
	{
		N2CTest::FScratchDir Base(TEXT("N2CArchiveTwice"));
		const FN2CTranslationRunInfo Info = RunInfo(EN2CTranslationRunKind::Mcp, EN2CCodeLanguage::Cpp, TEXT("BP_N2CArchiveTwice"));
		const TSharedPtr<FN2CTranslationArchive> First = FN2CTranslationArchive::BeginRun(Info, Base.Path);
		const TSharedPtr<FN2CTranslationArchive> Second = FN2CTranslationArchive::BeginRun(Info, Base.Path);
		if (!TestTrue(TEXT("both runs start"), First.IsValid() && Second.IsValid()))
		{
			return;
		}
		TestNotEqual(TEXT("two folders"), First->GetFolder(), Second->GetFolder());
		TestNotEqual(TEXT("two run ids"), First->GetRunId(), Second->GetRunId());

		const FN2CTranslationResponse Response = MakeResponse(EN2CCodeLanguage::Cpp);
		FN2CArchivedResponse A;
		FN2CArchivedResponse B;
		First->WriteResponse(Response, FN2CArchiveSource(), FString(), A);
		Second->WriteResponse(Response, FN2CArchiveSource(), FString(), B);
		TestTrue(TEXT("each run has its own root files"), FPaths::FileExists(First->GetFolder() / TEXT("EventGraph/EventGraph.cpp"))
			&& FPaths::FileExists(Second->GetFolder() / TEXT("EventGraph/EventGraph.cpp")));
		const TSharedPtr<FJsonObject> Manifest = LoadManifest(Second->GetFolder());
		TestTrue(TEXT("an MCP run says so"), Manifest.IsValid() && Manifest->GetStringField(TEXT("kind")) == TEXT("mcp"));
	});

	It(TEXT("keeps each batch item and each repeated entry name in its own folder"), [this]()
	{
		N2CTest::FScratchDir Base(TEXT("N2CArchiveBatch"));
		const TSharedPtr<FN2CTranslationArchive> Run = FN2CTranslationArchive::BeginRun(
			RunInfo(EN2CTranslationRunKind::Batch, EN2CCodeLanguage::Python, TEXT("BatchTranslation")), Base.Path);
		if (!TestTrue(TEXT("the run starts"), Run.IsValid()))
		{
			return;
		}

		// Two items; the second's response names two entries alike
		FN2CTranslationResponse Second = MakeResponse(EN2CCodeLanguage::Python);
		Second.Graphs[1].GraphName = TEXT("EventGraph");
		FN2CArchiveSource SourceA;
		SourceA.InputJson = TEXT("{\"item\":\"a\"}");
		SourceA.GraphName = TEXT("EventGraph");
		SourceA.BlueprintPath = TEXT("/Game/N2CTest/BP_A.BP_A");
		FN2CArchiveSource SourceB = SourceA;
		SourceB.InputJson = TEXT("{\"item\":\"b\"}");
		SourceB.BlueprintPath = TEXT("/Game/N2CTest/BP_B.BP_B");
		FN2CArchivedResponse A;
		FN2CArchivedResponse B;
		TestTrue(TEXT("item A is written"), Run->WriteResponse(MakeResponse(EN2CCodeLanguage::Python), SourceA, TEXT("EventGraph"), A));
		TestTrue(TEXT("item B is written"), Run->WriteResponse(Second, SourceB, TEXT("BP_B__EventGraph"), B));

		const FString Folder = Run->GetFolder();
		TestEqual(TEXT("item A's folder"), A.Folder, Folder / TEXT("EventGraph"));
		TestEqual(TEXT("item B's folder"), B.Folder, Folder / TEXT("BP_B__EventGraph"));
		TestEqual(TEXT("item A's JSON is in its root entry's folder"), Load(A.Folder / TEXT("EventGraph/EventGraph.json")), SourceA.InputJson);
		TestEqual(TEXT("item B's JSON is in its root entry's folder"), Load(B.Folder / TEXT("EventGraph/EventGraph.json")), SourceB.InputJson);
		TestEqual(TEXT("B's first EventGraph"), Load(B.Folder / TEXT("EventGraph/EventGraph.py")), Second.Graphs[0].Code.GraphImplementation);
		TestEqual(TEXT("B's second EventGraph gets _2"), Load(B.Folder / TEXT("EventGraph_2/EventGraph.py")), Second.Graphs[1].Code.GraphImplementation);

		const TSharedPtr<FJsonObject> Manifest = LoadManifest(Folder);
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (TestTrue(TEXT("the manifest lists the items"), Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("items"), Items)))
		{
			TestEqual(TEXT("two items"), Items->Num(), 2);
			TestEqual(TEXT("eight entries"), Manifest->GetArrayField(TEXT("entries")).Num(), 8);
			TestEqual(TEXT("the run's usage adds the items'"), static_cast<int32>(Manifest->GetObjectField(TEXT("usage"))->GetNumberField(TEXT("input_tokens"))), 240);
			if (Items->Num() == 2)
			{
				TestEqual(TEXT("item B's folder in the manifest"), (*Items)[1]->AsObject()->GetStringField(TEXT("folder")), FString(TEXT("BP_B__EventGraph")));
			}
		}
	});

	It(TEXT("resolves the output directory to an absolute path"), [this]()
	{
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		const FString Saved = Settings->CustomTranslationOutputDirectory.Path;
		ON_SCOPE_EXIT { Settings->CustomTranslationOutputDirectory.Path = Saved; };

		Settings->CustomTranslationOutputDirectory.Path = FString();
		const FString Default = FN2CPathUtils::GetTranslationsBasePath();
		TestFalse(TEXT("the default is absolute"), FPaths::IsRelative(Default));
		TestEqual(TEXT("the default is Saved/NodeToCode/Translations"), Default, FN2CPathUtils::GetDefaultTranslationsBasePath());
		TestTrue(TEXT("the default is under the project's Saved folder"),
			FPaths::IsUnderDirectory(Default, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir())));

		Settings->CustomTranslationOutputDirectory.Path = TEXT("N2CRelativeTranslations");
		const FString Relative = FN2CPathUtils::GetTranslationsBasePath();
		TestFalse(TEXT("a relative custom folder resolves to an absolute path"), FPaths::IsRelative(Relative));
		TestEqual(TEXT("a relative custom folder is under the project"), Relative,
			FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("N2CRelativeTranslations")));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
