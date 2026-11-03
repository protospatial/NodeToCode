// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Tests/N2CTestHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

/**
 * Settings version 5 (card 0188): the provider, LM Studio endpoint, MCP port, UI colours, translation output folder and
 * reference files outside the project moved from the project's [/Script/NodeToCode.N2CSettings] to the per-user
 * UN2CUserSettings. UpgradeFromProjectSettings copies a user's values over once, never edits the project config, and
 * changes nothing when run again. Every config here is a scratch copy under Saved/Automation loaded into GConfig under
 * its own path: the host's config and the user's real EditorPerProjectUserSettings.ini are never read or written.
 * The inputs are the checked-in Fixtures/Settings/Beta20 files (a version 4 project file and its Saved layer).
 */
BEGIN_DEFINE_SPEC(FN2CUserSettingsUpgradeSpec, "NodeToCode.Settings.UserUpgrade", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString ProjectDir;
	FString OutsideDir;
	FString ProjectIni;
	FString SavedIni;
	FString UserIni;

	static FString ReadText(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	/** True when the file text has a Key= line (an array's +Key= lines count) */
	static bool HasKey(const FString& Text, const FString& Key)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		return Lines.ContainsByPredicate([&Key](const FString& Line)
		{
			FString Trimmed = Line.TrimStart();
			while (Trimmed.StartsWith(TEXT("+")) || Trimmed.StartsWith(TEXT("!")) || Trimmed.StartsWith(TEXT(".")))
			{
				Trimmed.RightChopInline(1);
			}
			return Trimmed.StartsWith(Key + TEXT("="), ESearchCase::CaseSensitive);
		});
	}

	/** A fixture with {Project} and {Outside} replaced by this case's folders */
	bool CopyFixture(const TCHAR* Name, const FString& To)
	{
		FString Text = ReadText(N2CTest::FixturesDir(TEXT("Settings/Beta20")) / Name);
		if (!TestFalse(FString::Printf(TEXT("fixture %s found"), Name), Text.IsEmpty()))
		{
			return false;
		}
		Text.ReplaceInline(TEXT("{Project}"), *ProjectDir.LeftChop(ProjectDir.EndsWith(TEXT("/")) ? 1 : 0));
		Text.ReplaceInline(TEXT("{Outside}"), *OutsideDir);
		return TestTrue(FString::Printf(TEXT("fixture %s written"), Name), FFileHelper::SaveStringToFile(Text, *To));
	}

	/** Puts a file into GConfig under its own path, read with the ini array commands as the editor reads config */
	static void LoadIntoGConfig(const FString& Path)
	{
		GConfig->UnloadFile(Path);
		FConfigFile File;
		File.Combine(Path);
		GConfig->SetFile(Path, &File);
	}

	/** A user settings object as the editor loads it from UserIni at startup */
	UN2CUserSettings* LoadUser()
	{
		LoadIntoGConfig(UserIni);
		UN2CUserSettings* User = NewObject<UN2CUserSettings>(GetTransientPackage());
		// A fresh object takes the class default's values; reset the moved fields so only UserIni's values show
		User->SettingsVersion = 0;
		User->Provider = EN2CLLMProvider::Anthropic;
		User->LMStudioEndpoint = TEXT("http://localhost:1234");
		User->McpServerPort = 27000;
		User->ReferenceSourceFilePaths.Reset();
		User->CustomTranslationOutputDirectory.Path.Reset();
		User->UIColors = FN2CUIColors();
		User->LoadConfig(nullptr, *UserIni);
		return User;
	}

	/** The project's two layers, lowest first, and the scratch user config */
	FN2CUserSettingsUpgradeSources Sources(bool bWithSavedLayer = true) const
	{
		FN2CUserSettingsUpgradeSources Result;
		Result.ReadConfigs.Add(ProjectIni);
		if (bWithSavedLayer)
		{
			Result.ReadConfigs.Add(SavedIni);
		}
		Result.UserConfig = UserIni;
		Result.ProjectDir = ProjectDir;
		return Result;
	}

	bool WriteUserIni(const FString& Text)
	{
		return TestTrue(TEXT("user config written"), FFileHelper::SaveStringToFile(Text, *UserIni));
	}

	/** Every value the Beta20 fixtures hold for the user, as the upgraded (or restarted) settings must have them */
	void ExpectBetaValues(const UN2CUserSettings& User, const FString& When)
	{
		auto Label = [&When](const TCHAR* What) { return FString::Printf(TEXT("%s: %s"), *When, What); };
		TestEqual(Label(TEXT("Provider from the Saved layer, which wins")), User.Provider, EN2CLLMProvider::Gemini);
		TestEqual(Label(TEXT("McpServerPort from the Saved layer")), User.McpServerPort, 27200);
		TestEqual(Label(TEXT("LMStudioEndpoint from the project file")), User.LMStudioEndpoint, FString(TEXT("http://192.168.1.21:1234")));
		TestEqual(Label(TEXT("CustomTranslationOutputDirectory")), User.CustomTranslationOutputDirectory.Path, FString(TEXT("D:/N2CBeta/Translations")));
		TestEqual(Label(TEXT("the user's own BgPanel colour")), User.UIColors.BgPanel, FColor(20, 40, 60, 255));
		TestEqual(Label(TEXT("the user's own TextPrimary colour")), User.UIColors.TextPrimary, FColor(230, 230, 230, 255));
		TestEqual(Label(TEXT("the old Text Muted grey becomes the new default")), User.UIColors.TextMuted, FN2CUIColors().TextMuted);
		TestEqual(Label(TEXT("a colour the fixture does not set keeps its default")), User.UIColors.BgInput, FN2CUIColors().BgInput);
		if (TestEqual(Label(TEXT("one reference file of the user's own")), User.ReferenceSourceFilePaths.Num(), 1) && User.ReferenceSourceFilePaths.Num() == 1)
		{
			TestEqual(Label(TEXT("the file outside the project that exists here")),
				UN2CUserSettings::ResolveReferencePath(User.ReferenceSourceFilePaths[0].FilePath, ProjectDir), UN2CUserSettings::ResolveReferencePath(OutsideDir / TEXT("Mine.h")));
		}
		TestEqual(Label(TEXT("settings version")), User.SettingsVersion, UN2CUserSettings::CurrentSettingsVersion);
	}

END_DEFINE_SPEC(FN2CUserSettingsUpgradeSpec)

void FN2CUserSettingsUpgradeSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CUserUpgrade"));
		ProjectDir = *Scratch / TEXT("Project/");
		OutsideDir = *Scratch / TEXT("Outside");
		ProjectIni = *Scratch / TEXT("Project/Config/DefaultNodeToCode.ini");
		SavedIni = *Scratch / TEXT("Project/Saved/Config/WindowsEditor/NodeToCode.ini");
		UserIni = *Scratch / TEXT("Project/Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProjectIni), true);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserIni), true);
		FFileHelper::SaveStringToFile(TEXT("// in the project"), *(ProjectDir / TEXT("Source/Beta/Public/InProject.h")));
		FFileHelper::SaveStringToFile(TEXT("// the user's own header"), *(OutsideDir / TEXT("Mine.h")));
		// The user has never saved a NodeToCode user setting
		FFileHelper::SaveStringToFile(TEXT("[/Script/UnrealEd.EditorPerProjectUserSettings]\nbDisplayDocumentationLink=False\n"), *UserIni);
	});

	AfterEach([this]()
	{
		GConfig->UnloadFile(ProjectIni);
		GConfig->UnloadFile(SavedIni);
		GConfig->UnloadFile(UserIni);
		Scratch.Reset();
	});

	It(TEXT("lists the moved fields, each a config property of the user settings that the project settings no longer have"), [this]()
	{
		for (const FName& Name : UN2CUserSettings::GetMovedPropertyNames())
		{
			const FProperty* UserProperty = UN2CUserSettings::StaticClass()->FindPropertyByName(Name);
			TestTrue(FString::Printf(TEXT("%s is a user config property"), *Name.ToString()), UserProperty && UserProperty->HasAnyPropertyFlags(CPF_Config));
			if (Name != GET_MEMBER_NAME_CHECKED(UN2CUserSettings, ReferenceSourceFilePaths))
			{
				TestNull(FString::Printf(TEXT("%s is not a project setting"), *Name.ToString()), UN2CSettings::StaticClass()->FindPropertyByName(Name));
			}
		}
		TestEqual(TEXT("the user settings stamp the shared settings version"), UN2CUserSettings::CurrentSettingsVersion, 5);
		TestTrue(TEXT("the project version is not behind it"), UN2CSettings::CurrentSettingsVersion >= UN2CUserSettings::CurrentSettingsVersion);
	});

	It(TEXT("copies a version 4 project's per-user values once, the Saved layer winning, without editing the project config"), [this]()
	{
		if (!CopyFixture(TEXT("DefaultNodeToCode.ini"), ProjectIni) || !CopyFixture(TEXT("NodeToCode.ini"), SavedIni)) { return; }
		LoadIntoGConfig(ProjectIni);
		LoadIntoGConfig(SavedIni);
		const FString ProjectBefore = ReadText(ProjectIni);
		const FString SavedBefore = ReadText(SavedIni);

		UN2CUserSettings* User = LoadUser();
		const FN2CUserSettingsUpgradeReport Report = User->UpgradeFromProjectSettings(Sources());
		TestTrue(TEXT("the upgrade ran"), Report.bRan);
		TestTrue(TEXT("the version was written"), Report.bStamped);
		TestFalse(TEXT("nothing failed"), Report.bWriteFailed);
		TestTrue(TEXT("Text Muted was moved off the old grey"), Report.bUpdatedTextMuted);
		ExpectBetaValues(*User, TEXT("after the upgrade"));

		for (const TCHAR* Key : { TEXT("Provider"), TEXT("LMStudioEndpoint"), TEXT("McpServerPort"), TEXT("UIColors"),
			TEXT("CustomTranslationOutputDirectory"), TEXT("ReferenceSourceFilePaths") })
		{
			TestTrue(FString::Printf(TEXT("%s is reported as a key the project config no longer needs"), Key), Report.LegacyKeys.Contains(Key));
		}
		TestEqual(TEXT("the missing outside file is reported, not copied"), Report.SkippedReferenceFiles.Num(), 1);
		TestEqual(TEXT("nothing was unreadable"), Report.Unreadable.Num(), 0);

		// The project config is only read: other developers still need its values for their own upgrade
		TestEqual(TEXT("the project file is unchanged"), ReadText(ProjectIni), ProjectBefore);
		TestEqual(TEXT("the project's Saved layer is unchanged"), ReadText(SavedIni), SavedBefore);

		// Only the moved keys and the version were written to the user's config; no switch the user never set
		const FString UserText = ReadText(UserIni);
		for (const TCHAR* Key : { TEXT("Provider"), TEXT("LMStudioEndpoint"), TEXT("McpServerPort"), TEXT("UIColors"),
			TEXT("CustomTranslationOutputDirectory"), TEXT("ReferenceSourceFilePaths"), TEXT("SettingsVersion") })
		{
			TestTrue(FString::Printf(TEXT("%s is in the user config file"), Key), HasKey(UserText, Key));
		}
		for (const TCHAR* Key : { TEXT("bEnableMcpServer"), TEXT("bAllowMcpPythonExecution"), TEXT("bRequireMcpBearerToken"), TEXT("bShowGetStartedCard") })
		{
			TestFalse(FString::Printf(TEXT("%s is not written"), Key), HasKey(UserText, Key));
		}
		TestTrue(TEXT("the user's other settings in the file stay"), UserText.Contains(TEXT("bDisplayDocumentationLink=False")));
		TestFalse(TEXT("the project's own reference file is not copied"), UserText.Contains(TEXT("InProject.h")));

		// The next start: the same values from the user's file, and the upgrade does not run again
		UN2CUserSettings* Restarted = LoadUser();
		ExpectBetaValues(*Restarted, TEXT("after a restart"));
		const FN2CUserSettingsUpgradeReport Again = Restarted->UpgradeFromProjectSettings(Sources());
		TestFalse(TEXT("the upgrade does not run a second time"), Again.bRan);
		TestEqual(TEXT("the user file is unchanged by the second start"), ReadText(UserIni), UserText);
	});

	It(TEXT("changes nothing when run again on its own output, even with the version reset"), [this]()
	{
		if (!CopyFixture(TEXT("DefaultNodeToCode.ini"), ProjectIni) || !CopyFixture(TEXT("NodeToCode.ini"), SavedIni)) { return; }
		LoadIntoGConfig(ProjectIni);
		LoadIntoGConfig(SavedIni);
		UN2CUserSettings* User = LoadUser();
		User->UpgradeFromProjectSettings(Sources());

		UN2CUserSettings* Again = LoadUser();
		Again->SettingsVersion = 4;
		const FN2CUserSettingsUpgradeReport Report = Again->UpgradeFromProjectSettings(Sources());
		TestTrue(TEXT("it ran"), Report.bRan);
		TestEqual(TEXT("no value copied over the user's own"), Report.Copied.Num(), 0);
		TestEqual(TEXT("no reference file added twice"), Report.CopiedReferenceFiles.Num(), 0);
		TestTrue(TEXT("the user's values are reported as kept"), Report.KeptUserValues.Contains(TEXT("Provider")) && Report.KeptUserValues.Contains(TEXT("McpServerPort")));
		ExpectBetaValues(*Again, TEXT("after the second run"));
		ExpectBetaValues(*LoadUser(), TEXT("reloaded after the second run"));
	});

	It(TEXT("keeps a value the user already set over the project's"), [this]()
	{
		if (!CopyFixture(TEXT("DefaultNodeToCode.ini"), ProjectIni)) { return; }
		LoadIntoGConfig(ProjectIni);
		if (!WriteUserIni(TEXT("[/Script/NodeToCode.N2CUserSettings]\nProvider=LMStudio\nLMStudioEndpoint=\"http://127.0.0.1:4321\"\n"))) { return; }
		UN2CUserSettings* User = LoadUser();
		const FN2CUserSettingsUpgradeReport Report = User->UpgradeFromProjectSettings(Sources(false));
		TestEqual(TEXT("the user's provider stays"), User->Provider, EN2CLLMProvider::LMStudio);
		TestEqual(TEXT("the user's endpoint stays"), User->LMStudioEndpoint, FString(TEXT("http://127.0.0.1:4321")));
		TestTrue(TEXT("both reported as kept"), Report.KeptUserValues.Contains(TEXT("Provider")) && Report.KeptUserValues.Contains(TEXT("LMStudioEndpoint")));
		TestEqual(TEXT("the project file's port is copied"), User->McpServerPort, 27100);
		TestEqual(TEXT("reloaded: the user's provider"), LoadUser()->Provider, EN2CLLMProvider::LMStudio);
	});

	It(TEXT("keeps every saved colour but the old Text Muted grey, and leaves a palette at the defaults uncopied"), [this]()
	{
		const FString Section = FString::Printf(TEXT("[%s]\n"), *UN2CSettings::StaticClass()->GetPathName());
		TestTrue(TEXT("project file written"), FFileHelper::SaveStringToFile(Section
			+ TEXT("UIColors=(TextMuted=(B=107,G=107,R=107,A=255))\n"), *ProjectIni));
		LoadIntoGConfig(ProjectIni);
		UN2CUserSettings* User = LoadUser();
		const FN2CUserSettingsUpgradeReport Report = User->UpgradeFromProjectSettings(Sources(false));
		TestTrue(TEXT("Text Muted reported"), Report.bUpdatedTextMuted);
		TestFalse(TEXT("a palette that is all defaults is not copied"), Report.Copied.Contains(TEXT("UIColors")));
		TestFalse(TEXT("so the user file has no UIColors"), HasKey(ReadText(UserIni), TEXT("UIColors")));
		TestEqual(TEXT("Text Muted is the new default"), User->UIColors.TextMuted, FN2CUIColors().TextMuted);

		// A Text Muted the user chose is theirs
		UN2CUserSettings* Chosen = LoadUser();
		Chosen->SettingsVersion = 0;
		TestTrue(TEXT("project file rewritten"), FFileHelper::SaveStringToFile(Section
			+ TEXT("UIColors=(TextMuted=(B=120,G=120,R=120,A=255))\n"), *ProjectIni));
		LoadIntoGConfig(ProjectIni);
		const FN2CUserSettingsUpgradeReport Own = Chosen->UpgradeFromProjectSettings(Sources(false));
		TestFalse(TEXT("a chosen Text Muted is not changed"), Own.bUpdatedTextMuted);
		TestEqual(TEXT("Text Muted 120 kept"), Chosen->UIColors.TextMuted, FColor(120, 120, 120, 255));
		TestEqual(TEXT("reloaded: Text Muted 120"), LoadUser()->UIColors.TextMuted, FColor(120, 120, 120, 255));
	});

	It(TEXT("stamps the version and logs nothing when the project config holds no per-user value"), [this]()
	{
		const FString Section = FString::Printf(TEXT("[%s]\n"), *UN2CSettings::StaticClass()->GetPathName());
		TestTrue(TEXT("project file written"), FFileHelper::SaveStringToFile(Section + TEXT("TargetLanguage=Python\nSettingsVersion=4\n"), *ProjectIni));
		LoadIntoGConfig(ProjectIni);
		UN2CUserSettings* User = LoadUser();
		const FN2CUserSettingsUpgradeReport Report = User->UpgradeFromProjectSettings(Sources(false));
		TestTrue(TEXT("ran and stamped"), Report.bRan && Report.bStamped);
		TestEqual(TEXT("no legacy keys"), Report.LegacyKeys.Num(), 0);
		TestEqual(TEXT("nothing copied"), Report.Copied.Num(), 0);
		TestEqual(TEXT("defaults stay"), User->Provider, EN2CLLMProvider::Anthropic);
		TestEqual(TEXT("reloaded: version 5"), LoadUser()->SettingsVersion, UN2CUserSettings::CurrentSettingsVersion);
	});

	It(TEXT("stores the project's reference files project-relative and moves files outside the project to the user's list"), [this]()
	{
		TArray<FFilePath> ProjectList;
		TArray<FFilePath> UserList;
		auto Add = [](TArray<FFilePath>& List, const FString& Path) { FFilePath Entry; Entry.FilePath = Path; List.Add(Entry); };
		Add(ProjectList, ProjectDir / TEXT("Source/Beta/Public/InProject.h"));
		Add(ProjectList, OutsideDir / TEXT("Mine.h"));
		Add(ProjectList, TEXT("Source/Beta/Private/Relative.cpp"));
		Add(ProjectList, FString());

		const int32 Moved = UN2CSettings::NormalizeReferenceSourceFilePaths(ProjectList, UserList, ProjectDir);
		TestEqual(TEXT("one entry moved"), Moved, 1);
		if (TestEqual(TEXT("three entries stay in the project list"), ProjectList.Num(), 3) && ProjectList.Num() == 3)
		{
			TestEqual(TEXT("the absolute project path is stored project-relative"), ProjectList[0].FilePath, FString(TEXT("Source/Beta/Public/InProject.h")));
			TestEqual(TEXT("a relative path stays"), ProjectList[1].FilePath, FString(TEXT("Source/Beta/Private/Relative.cpp")));
			TestTrue(TEXT("an empty new row stays"), ProjectList[2].FilePath.IsEmpty());
		}
		if (TestEqual(TEXT("the outside file is the user's"), UserList.Num(), 1) && UserList.Num() == 1)
		{
			TestEqual(TEXT("as an absolute path"), UserList[0].FilePath, UN2CUserSettings::ResolveReferencePath(OutsideDir / TEXT("Mine.h")));
		}

		// Again: nothing changes, and the user's file is not added twice
		Add(ProjectList, OutsideDir / TEXT("Mine.h"));
		const TArray<FFilePath> UserBefore = UserList;
		TestEqual(TEXT("the second outside entry moves too"), UN2CSettings::NormalizeReferenceSourceFilePaths(ProjectList, UserList, ProjectDir), 1);
		TestEqual(TEXT("but is not added twice"), UserList.Num(), UserBefore.Num());
		TestEqual(TEXT("normalized again, the project list keeps its three entries"), UN2CSettings::NormalizeReferenceSourceFilePaths(ProjectList, UserList, ProjectDir), 0);
		TestEqual(TEXT("three project entries"), ProjectList.Num(), 3);

		// What a translation sends: the project's files (relative ones under the project) then the user's, once each
		TArray<FFilePath> UserWithDuplicate = UserList;
		Add(UserWithDuplicate, ProjectDir / TEXT("Source/Beta/Public/InProject.h"));
		Add(UserWithDuplicate, OutsideDir / TEXT("Gone.h"));
		TArray<FFilePath> ProjectAbsolute;
		Add(ProjectAbsolute, ProjectDir / TEXT("Source/Beta/Public/InProject.h"));
		const TArray<FString> Files = UN2CUserSettings::CollectReferenceSourceFiles(ProjectAbsolute, UserWithDuplicate);
		if (TestEqual(TEXT("two files: the project's, then the user's; the duplicate and the missing file left out"), Files.Num(), 2) && Files.Num() == 2)
		{
			TestEqual(TEXT("project file first"), Files[0], UN2CUserSettings::ResolveReferencePath(ProjectDir / TEXT("Source/Beta/Public/InProject.h")));
			TestEqual(TEXT("then the user's"), Files[1], UN2CUserSettings::ResolveReferencePath(OutsideDir / TEXT("Mine.h")));
		}
		TestEqual(TEXT("a relative entry resolves under the given project folder"),
			UN2CUserSettings::ResolveReferencePath(TEXT("Source/X.h"), ProjectDir), UN2CUserSettings::ResolveReferencePath(ProjectDir / TEXT("Source/X.h")));
		TestTrue(TEXT("inside the project"), UN2CUserSettings::IsInsideProject(UN2CUserSettings::ResolveReferencePath(TEXT("Source/X.h"), ProjectDir), ProjectDir));
		TestFalse(TEXT("outside the project"), UN2CUserSettings::IsInsideProject(UN2CUserSettings::ResolveReferencePath(OutsideDir / TEXT("Mine.h")), ProjectDir));
		TestFalse(TEXT("a sibling folder with the project's name as a prefix is outside"),
			UN2CUserSettings::IsInsideProject(UN2CUserSettings::ResolveReferencePath(*Scratch / TEXT("ProjectOther/A.h")), ProjectDir));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
