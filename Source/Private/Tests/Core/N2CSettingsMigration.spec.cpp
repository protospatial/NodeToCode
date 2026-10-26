// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CModelCatalog.h"
#include "Tests/N2CTestHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace N2CSettingsMigrationSpec
{
	/** What 2.0 does with a key NodeToCode 1.2.x saved in [/Script/NodeToCode.N2CSettings] */
	enum class EFate : uint8
	{
		/** Loaded into the 2.0 property of the same name, unchanged */
		Same,
		/** Loaded into the 2.0 property of the same name, its defaults migrated (Ollama options) */
		Migrated,
		/** Converted to a string model ID (AnthropicModel -> AnthropicModelId) and the old key removed */
		ModelId,
		/** Converted to ModelPricing by model ID (user prices kept, built-in 1.2.x prices dropped), old key removed */
		Pricing,
		/** Per-language theme 2.0 cannot convert: reported once and left in the ini for the user */
		Theme,
	};

	struct FKey
	{
		const TCHAR* Name;
		EFate Fate;
	};

	/**
	 * Every Config property of UN2CSettings in NodeToCode 1.2.x (origin/main, Source/Public/Core/N2CSettings.h).
	 * The API keys were not config: 1.2.x kept them in <Project>/Saved/NodeToCode/User/secrets.json (UN2CUserSecrets).
	 */
	const FKey Legacy12xKeys[] = {
		{ TEXT("Provider"), EFate::Same },
		{ TEXT("AnthropicModel"), EFate::ModelId },
		{ TEXT("OpenAI_Model"), EFate::ModelId },
		{ TEXT("Gemini_Model"), EFate::ModelId },
		{ TEXT("DeepSeekModel"), EFate::ModelId },
		{ TEXT("OllamaConfig"), EFate::Migrated },
		{ TEXT("OllamaModel"), EFate::Same },
		{ TEXT("LMStudioModel"), EFate::Same },
		{ TEXT("LMStudioEndpoint"), EFate::Same },
		{ TEXT("LMStudioPrependedModelCommand"), EFate::Same },
		{ TEXT("OpenAIModelPricing"), EFate::Pricing },
		{ TEXT("AnthropicModelPricing"), EFate::Pricing },
		{ TEXT("GeminiModelPricing"), EFate::Pricing },
		{ TEXT("DeepSeekModelPricing"), EFate::Pricing },
		{ TEXT("TargetLanguage"), EFate::Same },
		{ TEXT("TranslationDepth"), EFate::Same },
		{ TEXT("MinSeverity"), EFate::Same },
		{ TEXT("CPPThemes"), EFate::Theme },
		{ TEXT("PythonThemes"), EFate::Theme },
		{ TEXT("JavaScriptThemes"), EFate::Theme },
		{ TEXT("CSharpThemes"), EFate::Theme },
		{ TEXT("SwiftThemes"), EFate::Theme },
		{ TEXT("PseudocodeThemes"), EFate::Theme },
		{ TEXT("EstimatedReferenceTokens"), EFate::Same },
		{ TEXT("ReferenceSourceFilePaths"), EFate::Same },
		{ TEXT("CustomTranslationOutputDirectory"), EFate::Same },
	};
}

/**
 * A project NodeToCode 1.2.x saved, opened in 2.0: every value of main's ini layout arrives in a 2.0 setting, and every
 * API key of its project secrets.json in the per-user secret store (Windows Credential Manager). The inputs are the
 * checked-in Fixtures/Settings/Legacy12x files, copied to a scratch folder; the host's own config, the real
 * secrets.json and the user's "NodeToCode/*" Credential Manager entries are never read or written (the store under test
 * uses the target prefix "NodeToCodeTest/<guid>/"). NodeToCode.Settings.LegacyMigration covers the single steps (model
 * ID conversion, deferral without a catalog, HTTP timeouts); this spec follows one whole upgrade through a restart.
 */
BEGIN_DEFINE_SPEC(FN2CSettingsMigrationSpec, "NodeToCode.Settings.Migration", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString SettingsIni;
	FString EngineIni;
	FString UserSecrets;
	FString ProjectSecrets;
	FString VaultPrefix;

	static FString Fixture(const TCHAR* Name)
	{
		return N2CTest::FixturesDir(TEXT("Settings/Legacy12x")) / Name;
	}

	bool CopyFixture(const TCHAR* Name, const FString& To)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(To), true);
		return TestEqual(FString::Printf(TEXT("fixture %s copied"), Name),
			IFileManager::Get().Copy(*To, *Fixture(Name)), static_cast<uint32>(COPY_OK));
	}

	static FString ReadText(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	/** The keys of the settings section in an ini text, with the +/!/- array prefixes taken off, in file order */
	static TArray<FString> SectionKeys(const FString& Text)
	{
		TArray<FString> Keys;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		bool bInSection = false;
		for (const FString& Each : Lines)
		{
			const FString Line = Each.TrimStartAndEnd();
			if (Line.StartsWith(TEXT("[")))
			{
				bInSection = Line == FString::Printf(TEXT("[%s]"), *UN2CSettings::StaticClass()->GetPathName());
				continue;
			}
			int32 Equals = INDEX_NONE;
			if (!bInSection || Line.IsEmpty() || Line.StartsWith(TEXT(";")) || !Line.FindChar(TEXT('='), Equals))
			{
				continue;
			}
			FString Key = Line.Left(Equals);
			while (Key.Len() > 0 && (Key[0] == TEXT('+') || Key[0] == TEXT('!') || Key[0] == TEXT('-') || Key[0] == TEXT('.')))
			{
				Key.RightChopInline(1);
			}
			Keys.AddUnique(Key);
		}
		return Keys;
	}

	/**
	 * A settings object loaded from SettingsIni as the editor loads the project's config at startup. The file is read
	 * with the ini array commands applied (!Key=ClearArray, +Key=...), as the editor's config hierarchy reads
	 * DefaultNodeToCode.ini; a plain GConfig->LoadFile would keep "+ReferenceSourceFilePaths" as a key of its own.
	 */
	UN2CSettings* LoadSettings()
	{
		GConfig->UnloadFile(SettingsIni);
		FConfigFile File;
		File.Combine(SettingsIni);
		GConfig->SetFile(SettingsIni, &File);
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *SettingsIni);
		return Settings;
	}

	FN2CLegacyMigrationTargets Targets() const
	{
		FN2CLegacyMigrationTargets Result;
		Result.ReadConfig = SettingsIni;
		Result.WriteIni = SettingsIni;
		Result.EngineIni = EngineIni;
		// 1.2.x turned Use Less CPU when in Background off on every start
		Result.bThrottleCPUWhenNotForeground = false;
		return Result;
	}

	/** The store the editor uses on this platform, over this case's own entries or files */
	TUniquePtr<IN2CSecretBackend> MakeTarget() const
	{
		if (FN2CCredentialManagerBackend::IsSupported())
		{
			return MakeUnique<FN2CCredentialManagerBackend>(VaultPrefix);
		}
		return MakeUnique<FN2CSecretFileBackend>(*Scratch / TEXT("Target/NodeToCode/secrets.json"));
	}

	void RemoveVaultEntries() const
	{
		if (!FN2CCredentialManagerBackend::IsSupported())
		{
			return;
		}
		FN2CCredentialManagerBackend Vault(VaultPrefix);
		TMap<FString, FString> Entries;
		if (Vault.LoadAll(Entries))
		{
			for (const TPair<FString, FString>& Entry : Entries)
			{
				TMap<FString, FString> Unused;
				Vault.StoreKey(Entry.Key, FString(), Unused);
			}
		}
	}

	/** Checks every value the fixture holds as 2.0 reads it (after the upgrade, and after a restart) */
	void ExpectFixtureValues(const UN2CSettings& Settings, const FString& When)
	{
		auto Label = [&When](const TCHAR* What) { return FString::Printf(TEXT("%s: %s"), *When, What); };

		// Same: loaded unchanged
		TestEqual(Label(TEXT("Provider")), Settings.Provider, EN2CLLMProvider::DeepSeek);
		TestEqual(Label(TEXT("OllamaModel")), Settings.OllamaModel, FString(TEXT("llama3.3:70b")));
		TestEqual(Label(TEXT("LMStudioModel")), Settings.LMStudioModel, FString(TEXT("gemma-3-27b-it")));
		TestEqual(Label(TEXT("LMStudioEndpoint")), Settings.LMStudioEndpoint, FString(TEXT("http://192.168.1.21:1234")));
		TestEqual(Label(TEXT("LMStudioPrependedModelCommand")), Settings.LMStudioPrependedModelCommand, FString(TEXT("/think")));
		TestEqual(Label(TEXT("TargetLanguage")), Settings.TargetLanguage, EN2CCodeLanguage::CSharp);
		TestEqual(Label(TEXT("TranslationDepth")), Settings.TranslationDepth, 3);
		TestEqual(Label(TEXT("MinSeverity")), Settings.MinSeverity, EN2CLogSeverity::Warning);
		TestEqual(Label(TEXT("EstimatedReferenceTokens")), Settings.EstimatedReferenceTokens, 1834);
		TestEqual(Label(TEXT("two reference files")), Settings.ReferenceSourceFilePaths.Num(), 2);
		if (Settings.ReferenceSourceFilePaths.Num() == 2)
		{
			TestEqual(Label(TEXT("first reference file")), Settings.ReferenceSourceFilePaths[0].FilePath, FString(TEXT("C:/Projects/MyGame/Source/MyGame/Public/MyActor.h")));
			TestEqual(Label(TEXT("second reference file")), Settings.ReferenceSourceFilePaths[1].FilePath, FString(TEXT("C:/Projects/MyGame/Source/MyGame/Private/MyActor.cpp")));
		}
		TestEqual(Label(TEXT("CustomTranslationOutputDirectory")), Settings.CustomTranslationOutputDirectory.Path, FString(TEXT("C:/Projects/MyGame/Translations")));

		// Migrated: the user's Ollama values are kept and still sent; values at 1.2.x's always-sent default are not
		const FN2COllamaConfig& Ollama = Settings.OllamaConfig;
		TestEqual(Label(TEXT("Ollama endpoint")), Ollama.OllamaEndpoint, FString(TEXT("http://192.168.1.20:11434")));
		TestTrue(Label(TEXT("Ollama system prompts on")), Ollama.bUseSystemPrompts);
		TestEqual(Label(TEXT("Ollama prepended command")), Ollama.PrependedModelCommand, FString(TEXT("/no_think")));
		TestEqual(Label(TEXT("Ollama keep alive")), Ollama.KeepAlive, 600);
		TestEqual(Label(TEXT("Ollama num_predict")), Ollama.NumPredict, 4096);
		TestEqual(Label(TEXT("Ollama num_ctx")), Ollama.NumCtx, 32768);
		TestTrue(Label(TEXT("Ollama temperature 0.3, sent")), Ollama.bOverride_Temperature && FMath::IsNearlyEqual(Ollama.Temperature, 0.3f));
		TestTrue(Label(TEXT("Ollama top_k 20, sent")), Ollama.bOverride_TopK && Ollama.TopK == 20);
		TestTrue(Label(TEXT("Ollama seed 42, sent")), Ollama.bOverride_Seed && Ollama.Seed == 42);
		TestFalse(Label(TEXT("Ollama top_p at 1.2.x's default is not sent")), Ollama.bOverride_TopP);
		TestFalse(Label(TEXT("Ollama min_p at 1.2.x's default is not sent")), Ollama.bOverride_MinP);
		TestFalse(Label(TEXT("Ollama repeat_penalty at 1.2.x's default is not sent")), Ollama.bOverride_RepeatPenalty);
		TestFalse(Label(TEXT("Ollama mirostat at 1.2.x's default is not sent")), Ollama.bOverride_Mirostat || Ollama.bOverride_MirostatEta || Ollama.bOverride_MirostatTau);

		// ModelId: the four selections by catalog ID
		TestEqual(Label(TEXT("Anthropic: retired Claude 4 Sonnet moves to the default")), Settings.AnthropicModelId, FString(N2CDefaultModelIds::Anthropic));
		TestEqual(Label(TEXT("OpenAI: GPT-4.1 by ID")), Settings.OpenAIModelId, FString(TEXT("gpt-4.1")));
		TestEqual(Label(TEXT("Gemini: 2.5 Pro Exp (not in 2.0) moves to the default")), Settings.GeminiModelId, FString(N2CDefaultModelIds::Gemini));
		TestEqual(Label(TEXT("DeepSeek: R1 is deepseek-flash")), Settings.DeepSeekModelId, FString(TEXT("deepseek-flash")));
		TestEqual(Label(TEXT("DeepSeek: R1 thinks at high effort")), Settings.DeepSeekReasoningEffort, FString(TEXT("high")));
		TestTrue(Label(TEXT("no custom model IDs")), Settings.AnthropicCustomModelId.IsEmpty() && Settings.OpenAICustomModelId.IsEmpty()
			&& Settings.GeminiCustomModelId.IsEmpty() && Settings.DeepSeekCustomModelId.IsEmpty());

		// Pricing: the user's two prices by model ID; 1.2.x's built-in prices give way to the catalog's
		const FN2CModelPricing* Gpt41 = Settings.ModelPricing.Find(TEXT("gpt-4.1"));
		TestTrue(Label(TEXT("the user's GPT-4.1 price")), Gpt41 && FMath::IsNearlyEqual(Gpt41->InputCost, 1.5f) && FMath::IsNearlyEqual(Gpt41->OutputCost, 6.0f));
		const FN2CModelPricing* Flash = Settings.ModelPricing.Find(TEXT("deepseek-flash"));
		TestTrue(Label(TEXT("the user's DeepSeek V3 price on deepseek-flash")), Flash && FMath::IsNearlyEqual(Flash->InputCost, 0.2f) && FMath::IsNearlyEqual(Flash->OutputCost, 0.4f));
		TestEqual(Label(TEXT("only the user's prices are overrides")), Settings.ModelPricing.Num(), 2);
	}

END_DEFINE_SPEC(FN2CSettingsMigrationSpec)

void FN2CSettingsMigrationSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CSettingsMigration"));
		SettingsIni = *Scratch / TEXT("Project/Config/DefaultNodeToCode.ini");
		EngineIni = *Scratch / TEXT("Project/Config/DefaultEngine.ini");
		UserSecrets = *Scratch / TEXT("User/NodeToCode/secrets.json");
		ProjectSecrets = *Scratch / TEXT("Project/Saved/NodeToCode/User/secrets.json");
		VaultPrefix = FString::Printf(TEXT("NodeToCodeTest/%s/"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	});

	AfterEach([this]()
	{
		GConfig->UnloadFile(SettingsIni);
		RemoveVaultEntries();
		Scratch.Reset();
		FN2CModelCatalog::Get().ReloadBundled();
	});

	It(TEXT("has a fixture that holds every key NodeToCode 1.2.x saved"), [this]()
	{
		const FString Text = ReadText(Fixture(TEXT("DefaultNodeToCode.ini")));
		if (!TestFalse(TEXT("fixture found"), Text.IsEmpty())) { return; }
		TArray<FString> Expected;
		for (const N2CSettingsMigrationSpec::FKey& Key : N2CSettingsMigrationSpec::Legacy12xKeys)
		{
			Expected.Add(Key.Name);
		}
		const TArray<FString> Found = SectionKeys(Text);
		for (const FString& Key : Expected)
		{
			TestTrue(FString::Printf(TEXT("the fixture sets %s"), *Key), Found.Contains(Key));
		}
		for (const FString& Key : Found)
		{
			TestTrue(FString::Printf(TEXT("%s is a 1.2.x key this spec gives a fate"), *Key), Expected.Contains(Key));
		}

		// Each key kept under its own name is still a config property of 2.0's settings
		for (const N2CSettingsMigrationSpec::FKey& Key : N2CSettingsMigrationSpec::Legacy12xKeys)
		{
			if (Key.Fate == N2CSettingsMigrationSpec::EFate::Same || Key.Fate == N2CSettingsMigrationSpec::EFate::Migrated)
			{
				const FProperty* Property = UN2CSettings::StaticClass()->FindPropertyByName(Key.Name);
				TestTrue(FString::Printf(TEXT("%s is a 2.0 config property"), Key.Name), Property && Property->HasAnyPropertyFlags(CPF_Config));
			}
		}

		const FString Secrets = ReadText(Fixture(TEXT("secrets.json")));
		TestFalse(TEXT("the secrets fixture holds no real OpenAI or Anthropic key"), Secrets.Contains(TEXT("sk-")));
		TestFalse(TEXT("the secrets fixture holds no real Google key"), Secrets.Contains(TEXT("AIza")));
	});

	It(TEXT("brings every 1.2.x setting into the 2.0 project settings, and keeps them after a restart"), [this]()
	{
		if (!CopyFixture(TEXT("DefaultNodeToCode.ini"), SettingsIni) || !CopyFixture(TEXT("DefaultEngine.ini"), EngineIni)) { return; }
		UN2CSettings* Settings = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

		// The editor's first start: each notice once
		AddExpectedMessage(TEXT("no longer served by its provider"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("values NodeToCode 2.0 does not have"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("Code editor themes saved by NodeToCode 1.2.x"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("Use Less CPU when in Background"), EAutomationExpectedMessageFlags::Contains, 1);
		const FN2CLegacyMigrationReport Report = Settings->MigrateLegacy12Settings(Targets());
		TestTrue(TEXT("the 1.2.x steps ran"), Report.bRanLegacySteps);
		TestTrue(TEXT("the model ID conversion ran"), Report.ModelIds.bRan && !Report.ModelIds.bDeferred);
		TestFalse(TEXT("the config was written"), Report.bWriteFailed);
		TestEqual(TEXT("settings version"), Settings->SettingsVersion, UN2CSettings::CurrentSettingsVersion);
		ExpectFixtureValues(*Settings, TEXT("after the upgrade"));

		// What happened to each 1.2.x key, by its fate
		const FString Saved = ReadText(SettingsIni);
		const TArray<FString> SavedKeys = SectionKeys(Saved);
		for (const N2CSettingsMigrationSpec::FKey& Key : N2CSettingsMigrationSpec::Legacy12xKeys)
		{
			switch (Key.Fate)
			{
				case N2CSettingsMigrationSpec::EFate::Same:
				case N2CSettingsMigrationSpec::EFate::Migrated:
				case N2CSettingsMigrationSpec::EFate::Theme:
					TestTrue(FString::Printf(TEXT("%s is still in DefaultNodeToCode.ini"), Key.Name), SavedKeys.Contains(Key.Name));
					break;
				case N2CSettingsMigrationSpec::EFate::ModelId:
				case N2CSettingsMigrationSpec::EFate::Pricing:
					TestFalse(FString::Printf(TEXT("%s is removed from DefaultNodeToCode.ini"), Key.Name), SavedKeys.Contains(Key.Name));
					TestTrue(FString::Printf(TEXT("%s is reported as removed"), Key.Name), Report.RemovedLegacyKeys.Contains(Key.Name));
					break;
			}
			if (Key.Fate == N2CSettingsMigrationSpec::EFate::Theme)
			{
				TestTrue(FString::Printf(TEXT("%s is reported for the user"), Key.Name), Report.LegacyThemeKeys.Contains(Key.Name));
			}
		}
		for (const TCHAR* NewKey : { TEXT("AnthropicModelId"), TEXT("OpenAIModelId"), TEXT("GeminiModelId"), TEXT("DeepSeekModelId"),
			TEXT("DeepSeekReasoningEffort"), TEXT("ModelPricing"), TEXT("SettingsVersion") })
		{
			TestTrue(FString::Printf(TEXT("%s is written"), NewKey), SavedKeys.Contains(NewKey));
		}
		TestEqual(TEXT("the 1.2.x HTTP timeouts are removed from DefaultEngine.ini"), Report.RemovedEngineKeys.Num(), 2);
		TestTrue(TEXT("the rest of DefaultEngine.ini stays"), ReadText(EngineIni).Contains(TEXT("HttpTimeout=180")));

		// The next start reads the same values from the written file, logs nothing and writes nothing
		UN2CSettings* Restarted = LoadSettings();
		if (!TestNotNull(TEXT("settings reloaded"), Restarted)) { return; }
		const FN2CLegacyMigrationReport Again = Restarted->MigrateLegacy12Settings(Targets());
		TestFalse(TEXT("the 1.2.x steps do not run again"), Again.bRanLegacySteps);
		TestEqual(TEXT("nothing is written again"), Again.PersistedProperties.Num(), 0);
		TestEqual(TEXT("the settings file is unchanged"), ReadText(SettingsIni), Saved);
		ExpectFixtureValues(*Restarted, TEXT("after a restart"));
	});

	It(TEXT("moves every API key of a 1.2.x project secrets.json into the per-user store"), [this]()
	{
		// 1.2.x's UN2CUserSecrets kept the keys in <Project>/Saved/NodeToCode/User/secrets.json; 2.0 looks there
		const FString Legacy = FPaths::ConvertRelativePathToFull(FN2CSecretStore::GetLegacyProjectSecretsFilePath());
		TestEqual(TEXT("2.0 looks where 1.2.x saved"), Legacy,
			FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("User"), TEXT("secrets.json"))));

		if (!CopyFixture(TEXT("secrets.json"), ProjectSecrets)) { return; }
		TUniquePtr<IN2CSecretBackend> Target = MakeTarget();
		const FN2CSecretMigrationResult Result = FN2CSecretStore::MigrateFileToBackend(UserSecrets, ProjectSecrets, *Target);
		TestTrue(TEXT("found the project file"), Result.bFoundFile);
		TestFalse(TEXT("did not fail"), Result.bFailed);
		if (FN2CCredentialManagerBackend::IsSupported())
		{
			TestTrue(TEXT("completed"), Result.bCompleted);
			TestEqual(TEXT("moved the three keys"), Result.Moved, TArray<FString>{ TEXT("Anthropic"), TEXT("DeepSeek"), TEXT("OpenAI") });
			TestFalse(TEXT("the per-user file it went through is deleted"), FPaths::FileExists(UserSecrets));
		}
		TestEqual(TEXT("no conflicts"), Result.ConflictIds.Num(), 0);
		TestFalse(TEXT("the project file is deleted"), FPaths::FileExists(ProjectSecrets));

		// A store over the target, as the restarted editor has, sends each key
		FN2CSecretStore Store(MakeTarget(), [](const FString&) { return FString(); });
		TestEqual(TEXT("OpenAI key"), Store.GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-fixture-openai-key-12x")));
		TestEqual(TEXT("Anthropic key"), Store.GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("n2c-fixture-anthropic-key-12x")));
		TestEqual(TEXT("DeepSeek key"), Store.GetApiKey(EN2CLLMProvider::DeepSeek), FString(TEXT("n2c-fixture-deepseek-key-12x")));
		TestEqual(TEXT("the empty Gemini key stays unset"), Store.GetApiKeySource(EN2CLLMProvider::Gemini), EN2CApiKeySource::None);
		TestEqual(TEXT("each from the store"), Store.GetApiKeySource(EN2CLLMProvider::OpenAI), EN2CApiKeySource::Store);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
