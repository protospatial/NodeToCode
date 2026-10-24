// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "LLM/N2CLLMModels.h"
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
 * left alone. Every file is a temp copy under Saved/Automation; the host's own config is never read or written.
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
	});

	It(TEXT("migrates a 1.2.x config once and is silent on the next start"), [this]()
	{
		WriteFixture(LegacySettingsSection(), LegacyEngineIni());

		// The engine's own messages while loading Gemini_2_5_ProExp (Gemini_Model and the pricing map keyed by it), on
		// the first load only: the second load below must not repeat them
		AddExpectedMessage(TEXT("invalid value of 'Gemini_2_5_ProExp'"), EAutomationExpectedMessageFlags::Contains, 2);
		AddExpectedMessage(TEXT("import failed for"), EAutomationExpectedMessageFlags::Contains, 2);
		UN2CSettings* Settings = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Settings)) { return; }

		// What loading alone gives: the retired name loads as saved, the name 2.0 lacks keeps the 2.0 value
		TestEqual(TEXT("Claude4_Sonnet loads as saved"), Settings->AnthropicModel, EN2CAnthropicModel::Claude4_Sonnet);
		TestEqual(TEXT("Gemini_2_5_ProExp cannot load"), Settings->Gemini_Model, N2CDefaultModels::Gemini);
		TestEqual(TEXT("a map keyed by a removed model loses every entry on load"), Settings->GeminiModelPricing.Num(), 0);

		// First start: each notice exactly once
		AddExpectedMessage(TEXT("no longer served by its provider"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("values NodeToCode 2.0 does not have"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("Code editor themes saved by NodeToCode 1.2.x"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("Use Less CPU when in Background"), EAutomationExpectedMessageFlags::Contains, 1);
		const FN2CLegacyMigrationReport First = Settings->MigrateLegacy12Settings(Targets());

		TestTrue(TEXT("one-time steps ran"), First.bRanLegacySteps);
		TestTrue(TEXT("settings version stamped"), First.bStampedVersion);
		TestFalse(TEXT("config written"), First.bWriteFailed);
		TestEqual(TEXT("version in memory"), Settings->SettingsVersion, UN2CSettings::CurrentSettingsVersion);

		// Model selections: the retired one moved to the default, the unknown one reported, the kept ones untouched
		TestEqual(TEXT("Anthropic on the default"), Settings->AnthropicModel, N2CDefaultModels::Anthropic);
		TestEqual(TEXT("o3 kept"), Settings->OpenAI_Model, EN2COpenAIModel::GPT_o3);
		TestEqual(TEXT("DeepSeek R1 kept"), Settings->DeepSeekModel, EN2CDeepSeekModel::DeepSeek_R1);
		TestEqual(TEXT("two values could not load"), First.ResetEnumValues.Num(), 2);
		TestTrue(TEXT("the unreadable pricing map is named"), First.ResetEnumValues.ContainsByPredicate([](const FString& Each) { return Each.StartsWith(TEXT("GeminiModelPricing ")); }));
		TestTrue(TEXT("reset names the old and new value"), First.ResetEnumValues.Num() > 0
			&& First.ResetEnumValues[0] == FString::Printf(TEXT("Gemini_Model=Gemini_2_5_ProExp -> %s"),
				*StaticEnum<EN2CGeminiModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::Gemini))));

		// Pricing: retired models and 1.2.x built-in prices dropped, the user's prices for current models kept
		const FN2COpenAIPricing* Gpt41 = Settings->OpenAIModelPricing.Find(EN2COpenAIModel::GPT_4_1);
		TestTrue(TEXT("user price for GPT-4.1 kept"), Gpt41 && FMath::IsNearlyEqual(Gpt41->InputCost, 1.5f) && FMath::IsNearlyEqual(Gpt41->OutputCost, 6.0f));
		TestEqual(TEXT("only the user's OpenAI price is left"), Settings->OpenAIModelPricing.Num(), 1);
		TestFalse(TEXT("o3's 1.2.x price (15/60) dropped"), Settings->OpenAIModelPricing.Contains(EN2COpenAIModel::GPT_o3));
		TestEqual(TEXT("retired Anthropic prices dropped"), Settings->AnthropicModelPricing.Num(), 0);
		TestEqual(TEXT("retired Gemini prices dropped"), Settings->GeminiModelPricing.Num(), 0);
		TestTrue(TEXT("DeepSeek R1 1.2.x price dropped"), !Settings->DeepSeekModelPricing.Contains(EN2CDeepSeekModel::DeepSeek_R1));
		TestTrue(TEXT("user price for DeepSeek V3 kept"), Settings->DeepSeekModelPricing.Contains(EN2CDeepSeekModel::DeepSeek_V3));
		Settings->Provider = EN2CLLMProvider::OpenAI;
		Settings->OpenAI_Model = EN2COpenAIModel::GPT_o3;
		TestFalse(TEXT("o3 now costs the registry price, not 1.2.x's 15"), FMath::IsNearlyEqual(Settings->GetCurrentInputCost(), 15.0f));
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

		// DefaultNodeToCode.ini: changed keys rewritten, the rest of the user's lines untouched
		const FString Saved = ReadText(SettingsIni);
		const FString AnthropicName = StaticEnum<EN2CAnthropicModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::Anthropic));
		const FString GeminiName = StaticEnum<EN2CGeminiModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::Gemini));
		TestTrue(TEXT("Anthropic selection saved"), HasLine(Saved, TEXT("AnthropicModel=") + AnthropicName));
		TestTrue(TEXT("Gemini selection saved"), HasLine(Saved, TEXT("Gemini_Model=") + GeminiName));
		TestTrue(TEXT("version saved"), HasLine(Saved, FString::Printf(TEXT("SettingsVersion=%d"), UN2CSettings::CurrentSettingsVersion)));
		TestTrue(TEXT("kept OpenAI selection untouched"), HasLine(Saved, TEXT("OpenAI_Model=GPT_o3")));
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
		const FN2CLegacyMigrationReport Second = Reloaded->MigrateLegacy12Settings(Targets());
		TestFalse(TEXT("one-time steps skipped"), Second.bRanLegacySteps);
		TestEqual(TEXT("nothing written"), Second.PersistedProperties.Num(), 0);
		TestEqual(TEXT("nothing reset"), Second.ResetEnumValues.Num(), 0);
		TestEqual(TEXT("Anthropic loads as the default"), Reloaded->AnthropicModel, N2CDefaultModels::Anthropic);
		TestEqual(TEXT("Gemini loads as the default"), Reloaded->Gemini_Model, N2CDefaultModels::Gemini);
		TestEqual(TEXT("user price still there"), Reloaded->OpenAIModelPricing.Num(), 1);
		TestEqual(TEXT("Ollama context stays at the server default"), Reloaded->OllamaConfig.NumCtx, 0);
		TestEqual(TEXT("settings file unchanged"), ReadText(SettingsIni), SettingsBefore);
		TestEqual(TEXT("engine file unchanged"), ReadText(EngineIni), EngineBefore);
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
		const FString Settings = FString::Printf(TEXT("[%s]\nProvider=Anthropic\nAnthropicModel=%s\n"),
			*UN2CSettings::StaticClass()->GetPathName(),
			*StaticEnum<EN2CAnthropicModel>()->GetNameStringByValue(static_cast<int64>(N2CDefaultModels::Anthropic)));
		const FString Engine = TEXT("[/Script/EngineSettings.GameMapsSettings]\nGameDefaultMap=/Engine/Maps/Templates/OpenWorld\n");
		WriteFixture(Settings, Engine);
		UN2CSettings* Loaded = LoadSettings();
		if (!TestNotNull(TEXT("settings loaded"), Loaded)) { return; }

		// Throttling off with nothing from 1.2.x in the project: not ours to mention
		const FN2CLegacyMigrationReport Report = Loaded->MigrateLegacy12Settings(Targets(false));
		TestTrue(TEXT("checks ran"), Report.bRanLegacySteps);
		TestFalse(TEXT("no version stamped"), Report.bStampedVersion);
		TestFalse(TEXT("no throttle notice"), Report.bReportedThrottle);
		TestEqual(TEXT("nothing written"), Report.PersistedProperties.Num(), 0);
		TestEqual(TEXT("settings file unchanged"), ReadText(SettingsIni), Settings);
		TestEqual(TEXT("engine file unchanged"), ReadText(EngineIni), Engine);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
