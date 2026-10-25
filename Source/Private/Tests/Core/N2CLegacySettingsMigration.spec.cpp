// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CModelCatalog.h"
#include "Tests/N2CTestHelpers.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

/**
 * A project saved by NodeToCode 1.2.x opened in 2.0 (card 0418): the one-time migration saves the remapped and
 * reset model selections so their warnings are logged once, drops 1.2.x prices, reports the per-language themes,
 * removes the HTTP timeouts 1.2.x wrote to DefaultEngine.ini and says once that the CPU-throttle preference is
 * left alone. Since settings version 4 (card 0129) the model selections and prices are saved by model ID and the
 * enum keys removed, once, and not while the model catalog file is missing. Every file is a temp copy under Saved/Automation; the host's own config is never read or written.
 */
BEGIN_DEFINE_SPEC(FN2CLegacySettingsMigrationSpec, "NodeToCode.Settings.LegacyMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	FString Folder;
	FString SettingsIni;
	FString EngineIni;

	/**
	 * [/Script/NodeToCode.N2CSettings] as NodeToCode 1.2.x (origin/main) saved it: key names and value formats of its
	 * UN2CSettings. A Project Settings edit of any map (pricing or themes) saved the whole section, so every pricing
	 * map holds the constructor's prices; the user changed two of them (GPT_4_1, DeepSeek_V3). Gemini_2_5_ProExp is
	 * the v1.2.3 tag's default, which 2.0 does not have. The theme maps are cut down to one entry each.
	 */
	static FString LegacySettingsSection()
	{
		return FString::Printf(TEXT("[%s]\n"), *UN2CSettings::StaticClass()->GetPathName()) + TEXT(
			"Provider=Anthropic\n"
			"AnthropicModel=Claude4_Sonnet\n"
			"OpenAI_Model=GPT_o3\n"
			"Gemini_Model=Gemini_2_5_ProExp\n"
			"DeepSeekModel=DeepSeek_R1\n"
			"OllamaConfig=(OllamaEndpoint=\"http://localhost:11434\",bUseSystemPrompts=False,PrependedModelCommand=\"\",KeepAlive=3600,Temperature=0.000000,NumPredict=8192,TopP=0.500000,TopK=40,MinP=0.050000,RepeatPenalty=1.100000,NumCtx=8192,Mirostat=0,MirostatEta=0.100000,MirostatTau=5.000000,Seed=0)\n"
			"OllamaModel=\"qwen2.5-coder:32b\"\n"
			"LMStudioModel=\"qwen3-32b\"\n"
			"OpenAIModelPricing=((GPT4o_2024_08_06, (InputCost=2.500000,OutputCost=10.000000)),(GPT4o_Mini_2024_07_18, (InputCost=0.150000,OutputCost=0.600000)),(GPT_4_1, (InputCost=1.500000,OutputCost=6.000000)),(GPT_o1, (InputCost=15.000000,OutputCost=60.000000)),(GPT_o3_mini, (InputCost=1.100000,OutputCost=4.400000)),(GPT_o3, (InputCost=15.000000,OutputCost=60.000000)),(GPT_o4_mini, (InputCost=1.100000,OutputCost=4.400000)))\n"
			"AnthropicModelPricing=((Claude4_Opus, (InputCost=15.000000,OutputCost=75.000000)),(Claude4_Sonnet, (InputCost=3.000000,OutputCost=15.000000)),(Claude3_7_Sonnet, (InputCost=3.000000,OutputCost=15.000000)),(Claude3_5_Haiku, (InputCost=0.800000,OutputCost=4.000000)))\n"
			"GeminiModelPricing=((Gemini_2_5_ProExp, (InputCost=0.000000,OutputCost=0.000000)),(Gemini_2_5_Pro, (InputCost=1.250000,OutputCost=10.000000)),(Gemini_Flash_2_0, (InputCost=1.000000,OutputCost=0.400000)),(Gemini_1_5_Pro, (InputCost=1.250000,OutputCost=5.000000)))\n"
			"DeepSeekModelPricing=((DeepSeek_R1, (InputCost=0.550000,OutputCost=2.190000)),(DeepSeek_V3, (InputCost=0.200000,OutputCost=0.400000)))\n"
			"TargetLanguage=Python\n"
			"TranslationDepth=1\n"
			"MinSeverity=Info\n"
			"CPPThemes=(Themes=((\"Unreal Engine\", (NormalText=(B=0,G=255,R=0,A=255),Background=(B=0,G=0,R=0,A=255)))))\n"
			"PythonThemes=(Themes=((\"Midnight Code\", (NormalText=(B=255,G=255,R=255,A=255)))))\n");
	}

	/** DefaultEngine.ini after 1.2.x's ConfigureHttpTimeouts ran, around sections NodeToCode never touched */
	static FString LegacyEngineIni(const TCHAR* ConnectionTimeout = TEXT("300.000000"))
	{
		return FString::Printf(TEXT(
			"[/Script/EngineSettings.GameMapsSettings]\n"
			"GameDefaultMap=/Engine/Maps/Templates/OpenWorld\n"
			"\n"
			"[HTTP]\n"
			"HttpTimeout=180\n"
			"HttpConnectionTimeout=%s\n"
			"HttpActivityTimeout=3600.000000\n"
			"\n"
			"[/Script/Engine.RendererSettings]\n"
			"r.DefaultFeature.AutoExposure=False\n"), ConnectionTimeout);
	}

	void WriteFixture(const FString& Settings, const FString& Engine)
	{
		Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CLegacyMigration_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		SettingsIni = Folder / TEXT("DefaultNodeToCode.ini");
		EngineIni = Folder / TEXT("DefaultEngine.ini");
		TestTrue(TEXT("settings fixture written"), FFileHelper::SaveStringToFile(Settings, *SettingsIni));
		TestTrue(TEXT("engine fixture written"), FFileHelper::SaveStringToFile(Engine, *EngineIni));
	}

	/** A settings object loaded from SettingsIni as the editor loads the project's config (the file stays loaded) */
	UN2CSettings* LoadSettings()
	{
		GConfig->UnloadFile(SettingsIni);
		GConfig->LoadFile(SettingsIni);
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *SettingsIni);
		return Settings;
	}

	FN2CLegacyMigrationTargets Targets(bool bThrottle = false) const
	{
		FN2CLegacyMigrationTargets Result;
		Result.ReadConfig = SettingsIni;
		Result.WriteIni = SettingsIni;
		Result.EngineIni = EngineIni;
		Result.bThrottleCPUWhenNotForeground = bThrottle;
		return Result;
	}

	static FString ReadText(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	static bool HasLine(const FString& Text, const FString& Line)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		return Lines.ContainsByPredicate([&Line](const FString& Each) { return Each.TrimStartAndEnd() == Line; });
	}

	/** The value of Key= in the file text, quotes trimmed; empty when the key is absent */
	static FString IniValue(const FString& Text, const FString& Key)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStart();
			if (Trimmed.StartsWith(Key + TEXT("="), ESearchCase::CaseSensitive))
			{
				return Trimmed.RightChop(Key.Len() + 1).TrimEnd().TrimQuotes();
			}
		}
		return FString();
	}

	static bool HasKey(const FString& Text, const FString& Key)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		return Lines.ContainsByPredicate([&Key](const FString& Each) { return Each.TrimStart().StartsWith(Key + TEXT("=")); });
	}

	void Cleanup()
	{
		GConfig->UnloadFile(SettingsIni);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	}

END_DEFINE_SPEC(FN2CLegacySettingsMigrationSpec)

void FN2CLegacySettingsMigrationSpec::Define()
{
	AfterEach([this]()
	{
		Cleanup();
		// A case may have replaced the catalog rows; later cases and tests get the shipped file again
		FN2CModelCatalog::Get().ReloadBundled();
	});

	It(TEXT("migrates a 1.2.x config once and is silent on the next start"), [this]()
	{
		WriteFixture(LegacySettingsSection(), LegacyEngineIni());
		UN2CSettings* Settings = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

		// The model enum keys are not properties any more, so loading alone keeps the 2.0 defaults (and the engine
		// has nothing to fail on: no import error for Gemini_2_5_ProExp)
		TestEqual(TEXT("ModelPricing empty before the upgrade"), Settings->ModelPricing.Num(), 0);

		// First start: each notice exactly once
		AddExpectedMessage(TEXT("no longer served by its provider"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("values NodeToCode 2.0 does not have"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("Code editor themes saved by NodeToCode 1.2.x"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("Use Less CPU when in Background"), EAutomationExpectedMessageFlags::Contains, 1);
		const FN2CLegacyMigrationReport First = Settings->MigrateLegacy12Settings(Targets());

		TestTrue(TEXT("one-time steps ran"), First.bRanLegacySteps);
		TestTrue(TEXT("model ID conversion ran"), First.ModelIds.bRan);
		TestFalse(TEXT("not deferred"), First.ModelIds.bDeferred);
		TestTrue(TEXT("settings version stamped"), First.bStampedVersion);
		TestFalse(TEXT("config written"), First.bWriteFailed);
		TestEqual(TEXT("version in memory"), Settings->SettingsVersion, UN2CSettings::CurrentSettingsVersion);

		// Model selections: the retired one and the removed one on the default, the kept ones by their catalog ID
		TestEqual(TEXT("Anthropic on the default"), Settings->AnthropicModelId, FString(N2CDefaultModelIds::Anthropic));
		TestEqual(TEXT("o3 kept"), Settings->OpenAIModelId, FString(TEXT("o3")));
		TestEqual(TEXT("Gemini on the default"), Settings->GeminiModelId, FString(N2CDefaultModelIds::Gemini));
		TestEqual(TEXT("DeepSeek R1 is deepseek-flash"), Settings->DeepSeekModelId, FString(TEXT("deepseek-flash")));
		TestEqual(TEXT("DeepSeek R1 thinks at high effort"), Settings->DeepSeekReasoningEffort, FString(TEXT("high")));
		TestTrue(TEXT("no custom IDs"), Settings->AnthropicCustomModelId.IsEmpty() && Settings->OpenAICustomModelId.IsEmpty()
			&& Settings->GeminiCustomModelId.IsEmpty() && Settings->DeepSeekCustomModelId.IsEmpty());
		TestEqual(TEXT("two values could not load"), First.ResetEnumValues.Num(), 2);
		TestTrue(TEXT("reset names the old and new value"), First.ResetEnumValues.Contains(
			FString::Printf(TEXT("Gemini_Model=Gemini_2_5_ProExp -> %s"), N2CDefaultModelIds::Gemini)));
		TestTrue(TEXT("the unreadable pricing map is named"), First.ResetEnumValues.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("GeminiModelPricing ")); }));

		// Pricing: retired models and 1.2.x built-in prices dropped, the user's prices for current models kept by ID
		const FN2CModelPricing* Gpt41 = Settings->ModelPricing.Find(TEXT("gpt-4.1"));
		TestTrue(TEXT("user price for GPT-4.1 kept"), Gpt41 && FMath::IsNearlyEqual(Gpt41->InputCost, 1.5f) && FMath::IsNearlyEqual(Gpt41->OutputCost, 6.0f));
		const FN2CModelPricing* Flash = Settings->ModelPricing.Find(TEXT("deepseek-flash"));
		TestTrue(TEXT("user price for DeepSeek V3 kept as deepseek-flash"), Flash && FMath::IsNearlyEqual(Flash->InputCost, 0.2f) && FMath::IsNearlyEqual(Flash->OutputCost, 0.4f));
		TestEqual(TEXT("only the two user prices are left"), Settings->ModelPricing.Num(), 2);
		TestFalse(TEXT("o3's 1.2.x price (15/60) dropped"), Settings->ModelPricing.Contains(TEXT("o3")));
		Settings->Provider = EN2CLLMProvider::OpenAI;
		TestFalse(TEXT("o3 now costs the catalog price, not 1.2.x's 15"), FMath::IsNearlyEqual(Settings->GetCurrentInputCost(), 15.0f));
		TestTrue(TEXT("o3 costs something"), Settings->GetCurrentInputCost() > 0.0f);
		Settings->Provider = EN2CLLMProvider::Anthropic;

		// Themes reported, CPU throttle reported because the project shows 1.2.x traces
		TestEqual(TEXT("both theme keys reported"), First.LegacyThemeKeys, TArray<FString>({ TEXT("CPPThemes"), TEXT("PythonThemes") }));
		TestTrue(TEXT("throttle notice logged"), First.bReportedThrottle);

		// DefaultEngine.ini: exactly the two 1.2.x keys gone, everything else as it was
		TestEqual(TEXT("two engine keys removed"), First.RemovedEngineKeys.Num(), 2);
		const FString Engine = ReadText(EngineIni);
		TestFalse(TEXT("HttpConnectionTimeout removed"), HasKey(Engine, TEXT("HttpConnectionTimeout")));
		TestFalse(TEXT("HttpActivityTimeout removed"), HasKey(Engine, TEXT("HttpActivityTimeout")));
		TestTrue(TEXT("other HTTP key kept"), HasLine(Engine, TEXT("HttpTimeout=180")));
		TestTrue(TEXT("section before kept"), HasLine(Engine, TEXT("GameDefaultMap=/Engine/Maps/Templates/OpenWorld")));
		TestTrue(TEXT("section after kept"), HasLine(Engine, TEXT("r.DefaultFeature.AutoExposure=False")));

		// DefaultNodeToCode.ini: model IDs written, enum keys removed, the rest of the user's lines untouched
		const FString Saved = ReadText(SettingsIni);
		TestEqual(TEXT("Anthropic ID saved"), IniValue(Saved, TEXT("AnthropicModelId")), FString(N2CDefaultModelIds::Anthropic));
		TestEqual(TEXT("OpenAI ID saved"), IniValue(Saved, TEXT("OpenAIModelId")), FString(TEXT("o3")));
		TestEqual(TEXT("Gemini ID saved"), IniValue(Saved, TEXT("GeminiModelId")), FString(N2CDefaultModelIds::Gemini));
		TestEqual(TEXT("DeepSeek ID saved"), IniValue(Saved, TEXT("DeepSeekModelId")), FString(TEXT("deepseek-flash")));
		TestEqual(TEXT("DeepSeek thinking saved"), IniValue(Saved, TEXT("DeepSeekReasoningEffort")), FString(TEXT("high")));
		TestTrue(TEXT("prices saved by ID"), IniValue(Saved, TEXT("ModelPricing")).Contains(TEXT("gpt-4.1")) && IniValue(Saved, TEXT("ModelPricing")).Contains(TEXT("deepseek-flash")));
		TestTrue(TEXT("version saved"), HasLine(Saved, FString::Printf(TEXT("SettingsVersion=%d"), UN2CSettings::CurrentSettingsVersion)));
		for (const TCHAR* Old : { TEXT("AnthropicModel"), TEXT("OpenAI_Model"), TEXT("Gemini_Model"), TEXT("DeepSeekModel"),
			TEXT("OpenAIModelPricing"), TEXT("AnthropicModelPricing"), TEXT("GeminiModelPricing"), TEXT("DeepSeekModelPricing") })
		{
			TestFalse(FString::Printf(TEXT("%s removed"), Old), HasKey(Saved, Old));
			TestTrue(FString::Printf(TEXT("%s reported as removed"), Old), First.RemovedLegacyKeys.Contains(Old));
		}
		TestTrue(TEXT("other settings untouched"), HasLine(Saved, TEXT("TargetLanguage=Python")) && HasLine(Saved, TEXT("OllamaModel=\"qwen2.5-coder:32b\"")));
		TestTrue(TEXT("theme keys left for the user"), HasKey(Saved, TEXT("CPPThemes")) && HasKey(Saved, TEXT("PythonThemes")));
		TestFalse(TEXT("no value names the removed model"), Saved.Contains(TEXT("Gemini_2_5_ProExp")));
		TestTrue(TEXT("Ollama migration saved"), Saved.Contains(TEXT("NumCtx=0")) && !Saved.Contains(TEXT("NumCtx=8192")));

		// Next start: loads the saved values, logs nothing, writes nothing
		const FString SettingsBefore = ReadText(SettingsIni);
		const FString EngineBefore = ReadText(EngineIni);
		UN2CSettings* Reloaded = LoadSettings();
		if (!TestNotNull(TEXT("settings reloaded"), Reloaded)) { return; }
		TestEqual(TEXT("no retired selection left"), Reloaded->MigrateRetiredModelSelections(), 0);
		TestFalse(TEXT("no model ID conversion on the second load"), Reloaded->MigrateModelSelectionsToIds(SettingsIni).bRan);
		const FN2CLegacyMigrationReport Second = Reloaded->MigrateLegacy12Settings(Targets());
		TestFalse(TEXT("one-time steps skipped"), Second.bRanLegacySteps);
		TestFalse(TEXT("conversion skipped"), Second.ModelIds.bRan);
		TestEqual(TEXT("nothing written"), Second.PersistedProperties.Num(), 0);
		TestEqual(TEXT("nothing reset"), Second.ResetEnumValues.Num(), 0);
		TestEqual(TEXT("Anthropic loads as the default"), Reloaded->AnthropicModelId, FString(N2CDefaultModelIds::Anthropic));
		TestEqual(TEXT("o3 loads"), Reloaded->OpenAIModelId, FString(TEXT("o3")));
		TestEqual(TEXT("DeepSeek thinking loads"), Reloaded->DeepSeekReasoningEffort, FString(TEXT("high")));
		TestEqual(TEXT("user prices still there"), Reloaded->ModelPricing.Num(), 2);
		TestEqual(TEXT("Ollama context stays at the server default"), Reloaded->OllamaConfig.NumCtx, 0);
		TestEqual(TEXT("settings file unchanged"), ReadText(SettingsIni), SettingsBefore);
		TestEqual(TEXT("engine file unchanged"), ReadText(EngineIni), EngineBefore);
	});

	It(TEXT("converts saved enum selections and a price override to model IDs once (card 0129)"), [this]()
	{
		// A config saved by a 2.0 build before the model catalog took over selection (no 1.2.x traces)
		WriteFixture(FString::Printf(TEXT("[%s]\n"), *UN2CSettings::StaticClass()->GetPathName()) + TEXT(
			"Provider=Anthropic\n"
			"AnthropicModel=Claude4_5_Sonnet\n"
			"OpenAI_Model=GPT_5\n"
			"Gemini_Model=Gemini_2_5_Flash\n"
			"DeepSeekModel=DeepSeek_R1\n"
			"AnthropicModelPricing=((Claude4_5_Sonnet, (InputCost=2.500000,OutputCost=12.500000)))\n"
			"TargetLanguage=Cpp\n"), TEXT("[/Script/EngineSettings.GameMapsSettings]\n"));
		UN2CSettings* Settings = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

		AddExpectedMessage(TEXT("no longer served by its provider"), EAutomationExpectedMessageFlags::Contains, 1);
		const FN2CLegacyMigrationReport First = Settings->MigrateLegacy12Settings(Targets(true));
		TestTrue(TEXT("converted"), First.ModelIds.bRan);
		TestTrue(TEXT("version 4 stamped"), First.bStampedVersion);

		TestEqual(TEXT("AnthropicModelId"), Settings->AnthropicModelId, FString(TEXT("claude-sonnet-4-5-20250929")));
		TestEqual(TEXT("OpenAIModelId"), Settings->OpenAIModelId, FString(TEXT("gpt-5")));
		TestEqual(TEXT("GeminiModelId (Gemini 2.5 Flash Preview is retired: the default)"), Settings->GeminiModelId, FString(N2CDefaultModelIds::Gemini));
		TestEqual(TEXT("DeepSeekModelId"), Settings->DeepSeekModelId, FString(TEXT("deepseek-flash")));
		TestEqual(TEXT("DeepSeekReasoningEffort"), Settings->DeepSeekReasoningEffort, FString(TEXT("high")));
		const FN2CModelPricing* Sonnet = Settings->ModelPricing.Find(TEXT("claude-sonnet-4-5-20250929"));
		TestTrue(TEXT("price override kept under the model ID"), Sonnet && FMath::IsNearlyEqual(Sonnet->InputCost, 2.5f) && FMath::IsNearlyEqual(Sonnet->OutputCost, 12.5f));
		TestEqual(TEXT("the override is the current cost"), Settings->GetCurrentInputCost(), 2.5f);

		const FString Saved = ReadText(SettingsIni);
		TestEqual(TEXT("ini AnthropicModelId"), IniValue(Saved, TEXT("AnthropicModelId")), FString(TEXT("claude-sonnet-4-5-20250929")));
		TestEqual(TEXT("ini OpenAIModelId"), IniValue(Saved, TEXT("OpenAIModelId")), FString(TEXT("gpt-5")));
		TestTrue(TEXT("ini ModelPricing keyed by ID"), IniValue(Saved, TEXT("ModelPricing")).Contains(TEXT("claude-sonnet-4-5-20250929")));
		TestFalse(TEXT("ini AnthropicModel removed"), HasKey(Saved, TEXT("AnthropicModel")));
		TestFalse(TEXT("ini AnthropicModelPricing removed"), HasKey(Saved, TEXT("AnthropicModelPricing")));
		TestTrue(TEXT("other keys kept"), HasLine(Saved, TEXT("TargetLanguage=Cpp")));

		// Second load: the upgrade does not run again and the values come from the new keys
		const FString Before = ReadText(SettingsIni);
		UN2CSettings* Reloaded = LoadSettings();
		if (!TestNotNull(TEXT("settings reloaded"), Reloaded)) { return; }
		const FN2CLegacyMigrationReport Second = Reloaded->MigrateLegacy12Settings(Targets(true));
		TestFalse(TEXT("no second conversion"), Second.ModelIds.bRan);
		TestEqual(TEXT("nothing written"), Second.PersistedProperties.Num(), 0);
		TestEqual(TEXT("file unchanged"), ReadText(SettingsIni), Before);
		TestEqual(TEXT("AnthropicModelId loads"), Reloaded->AnthropicModelId, FString(TEXT("claude-sonnet-4-5-20250929")));
		TestEqual(TEXT("price loads"), Reloaded->ModelPricing.Num(), 1);

		// Gemini costs its catalog price (GetCurrentInputCost used to return 0 for every Gemini model)
		Reloaded->Provider = EN2CLLMProvider::Gemini;
		const FN2CModelMetadata* Gemini = FN2CModelCatalog::Get().Find(TEXT("gemini"), N2CDefaultModelIds::Gemini);
		if (TestNotNull(TEXT("Gemini default row"), Gemini))
		{
			TestTrue(TEXT("Gemini input cost is non-zero"), Reloaded->GetCurrentInputCost() > 0.0f);
			TestEqual(TEXT("Gemini input cost is the catalog price"), Reloaded->GetCurrentInputCost(), Gemini->InputCostPer1M);
			TestEqual(TEXT("Gemini output cost is the catalog price"), Reloaded->GetCurrentOutputCost(), Gemini->OutputCostPer1M);
		}
	});

	It(TEXT("keeps a saved model the catalog does not list as a custom model ID with a warning"), [this]()
	{
		// The shipped catalog without its gpt-5 row: GPT_5 is still an enumerator, but no row names it
		FString Json;
		TestTrue(TEXT("bundled catalog read"), FFileHelper::LoadFileToString(Json, *FN2CModelCatalog::GetBundledPath()));
		TSharedPtr<FJsonObject> Root;
		TestTrue(TEXT("bundled catalog parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
		if (!Root.IsValid()) { return; }
		TArray<TSharedPtr<FJsonValue>> Models = Root->GetArrayField(TEXT("models"));
		Models.RemoveAll([](const TSharedPtr<FJsonValue>& Value) { return Value->AsObject()->GetStringField(TEXT("id")) == TEXT("gpt-5"); });
		Root->SetArrayField(TEXT("models"), Models);
		FString Trimmed;
		FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&Trimmed));
		if (!TestTrue(TEXT("trimmed catalog loads"), FN2CModelCatalog::Get().LoadFromString(Trimmed, TEXT("test without gpt-5")))) { return; }

		WriteFixture(FString::Printf(TEXT("[%s]\nSettingsVersion=3\nProvider=OpenAI\nOpenAI_Model=GPT_5\n"), *UN2CSettings::StaticClass()->GetPathName()),
			TEXT("[/Script/EngineSettings.GameMapsSettings]\n"));
		UN2CSettings* Settings = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

		AddExpectedMessage(TEXT("is not in the model catalog"), EAutomationExpectedMessageFlags::Contains, 1);
		const FN2CLegacyMigrationReport Report = Settings->MigrateLegacy12Settings(Targets(true));
		TestEqual(TEXT("one custom ID"), Report.ModelIds.Custom.Num(), 1);
		TestEqual(TEXT("kept as the Custom Model ID"), Settings->OpenAICustomModelId, FString(TEXT("gpt-5")));
		TestEqual(TEXT("not reset: requests send gpt-5"), Settings->GetActiveModelForProvider(EN2CLLMProvider::OpenAI), FString(TEXT("gpt-5")));
		TestEqual(TEXT("Model left at the default"), Settings->OpenAIModelId, FString(N2CDefaultModelIds::OpenAI));
		const FString Saved = ReadText(SettingsIni);
		TestEqual(TEXT("custom ID saved"), IniValue(Saved, TEXT("OpenAICustomModelId")), FString(TEXT("gpt-5")));
		TestFalse(TEXT("old key removed"), HasKey(Saved, TEXT("OpenAI_Model")));
		TestEqual(TEXT("a custom model has no catalog price"), Settings->GetCurrentInputCost(), 0.0f);
	});

	It(TEXT("keeps saved selections and prices unconverted on disk while the catalog file is missing"), [this]()
	{
		N2CTest::FScratchDir Dir(TEXT("N2CLegacyMigrationCatalog"));
		AddExpectedError(TEXT("could not be read"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("missing catalog does not load"), FN2CModelCatalog::Get().LoadFromFile(Dir / TEXT("missing.json")));
		TestTrue(TEXT("fallback"), FN2CModelCatalog::Get().IsFallback());

		const FString Settings = FString::Printf(TEXT("[%s]\nSettingsVersion=3\nProvider=OpenAI\nOpenAI_Model=GPT_6_Astra\nDeepSeekModel=DeepSeek_V3\n")
			TEXT("OpenAIModelPricing=((GPT_6_Astra, (InputCost=11.000000,OutputCost=55.000000)))\n"), *UN2CSettings::StaticClass()->GetPathName());
		WriteFixture(Settings, TEXT("[/Script/EngineSettings.GameMapsSettings]\n"));
		UN2CSettings* Loaded = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Loaded)) { return; }

		const FN2CLegacyMigrationReport Report = Loaded->MigrateLegacy12Settings(Targets(true));
		TestTrue(TEXT("deferred"), Report.ModelIds.bDeferred);
		TestFalse(TEXT("no version stamped"), Report.bStampedVersion);
		TestEqual(TEXT("nothing written"), Report.PersistedProperties.Num(), 0);
		TestEqual(TEXT("settings file unchanged"), ReadText(SettingsIni), Settings);
		TestEqual(TEXT("version stays below 4"), Loaded->SettingsVersion, 3);

		// In memory the selection and price still apply, by the enumerators' model IDs
		TestEqual(TEXT("GPT-6 Astra still selected"), Loaded->GetActiveModelForProvider(EN2CLLMProvider::OpenAI), FString(TEXT("gpt-6-astra")));
		TestEqual(TEXT("DeepSeek V3 still thinking off"), Loaded->GetActiveReasoningEffortForProvider(EN2CLLMProvider::DeepSeek), FString(TEXT("none")));
		TestEqual(TEXT("price kept"), Loaded->GetCurrentInputCost(), 11.0f);

		// With the catalog back, the next start converts and saves
		FN2CModelCatalog::Get().ReloadBundled();
		UN2CSettings* Next = LoadSettings();
		if (!TestNotNull(TEXT("settings reloaded"), Next)) { return; }
		const FN2CLegacyMigrationReport After = Next->MigrateLegacy12Settings(Targets(true));
		TestTrue(TEXT("converted once the catalog loads"), After.ModelIds.bRan && !After.ModelIds.bDeferred && After.bStampedVersion);
		const FString Saved = ReadText(SettingsIni);
		TestEqual(TEXT("OpenAIModelId saved"), IniValue(Saved, TEXT("OpenAIModelId")), FString(TEXT("gpt-6-astra")));
		TestEqual(TEXT("DeepSeek thinking saved"), IniValue(Saved, TEXT("DeepSeekReasoningEffort")), FString(TEXT("none")));
		TestTrue(TEXT("price saved by ID"), IniValue(Saved, TEXT("ModelPricing")).Contains(TEXT("gpt-6-astra")));
	});

	It(TEXT("keeps HTTP timeouts and a CPU-throttle setting it cannot attribute to 1.2.x"), [this]()
	{
		// A user value for the connection timeout; only the activity timeout still holds 1.2.x's value
		WriteFixture(FString::Printf(TEXT("[%s]\nProvider=Anthropic\n"), *UN2CSettings::StaticClass()->GetPathName()), LegacyEngineIni(TEXT("120")));
		UN2CSettings* Settings = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

		AddExpectedMessage(TEXT("Use Less CPU when in Background"), EAutomationExpectedMessageFlags::Contains, 1);
		const FN2CLegacyMigrationReport Report = Settings->MigrateLegacy12Settings(Targets());
		const FString Engine = ReadText(EngineIni);
		TestTrue(TEXT("user's connection timeout kept"), HasLine(Engine, TEXT("HttpConnectionTimeout=120")));
		TestFalse(TEXT("1.2.x activity timeout removed"), HasKey(Engine, TEXT("HttpActivityTimeout")));
		TestEqual(TEXT("one key removed"), Report.RemovedEngineKeys, TArray<FString>({ TEXT("[HTTP] HttpActivityTimeout=3600.000000") }));
		TestTrue(TEXT("version stamped"), HasLine(ReadText(SettingsIni), FString::Printf(TEXT("SettingsVersion=%d"), UN2CSettings::CurrentSettingsVersion)));
		TestTrue(TEXT("throttle off is reported, not changed"), Report.bReportedThrottle);
	});

	It(TEXT("leaves a config with no 1.2.x traces unwritten"), [this]()
	{
		const FString Settings = FString::Printf(TEXT("[%s]\nProvider=Anthropic\nAnthropicModelId=%s\n"),
			*UN2CSettings::StaticClass()->GetPathName(), N2CDefaultModelIds::Anthropic);
		const FString Engine = TEXT("[/Script/EngineSettings.GameMapsSettings]\nGameDefaultMap=/Engine/Maps/Templates/OpenWorld\n");
		WriteFixture(Settings, Engine);
		UN2CSettings* Loaded = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Loaded)) { return; }

		// Throttling off with nothing from 1.2.x in the project: not ours to mention
		const FN2CLegacyMigrationReport Report = Loaded->MigrateLegacy12Settings(Targets(false));
		TestTrue(TEXT("checks ran"), Report.bRanLegacySteps);
		TestFalse(TEXT("nothing to convert"), Report.ModelIds.bRan);
		TestFalse(TEXT("no version stamped"), Report.bStampedVersion);
		TestFalse(TEXT("no throttle notice"), Report.bReportedThrottle);
		TestEqual(TEXT("nothing written"), Report.PersistedProperties.Num(), 0);
		TestEqual(TEXT("settings file unchanged"), ReadText(SettingsIni), Settings);
		TestEqual(TEXT("engine file unchanged"), ReadText(EngineIni), Engine);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
