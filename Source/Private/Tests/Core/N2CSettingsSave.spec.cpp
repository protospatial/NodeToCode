// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CContextWindowVisualizer.h"
#include "Tests/N2CTestHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/**
 * How the settings are saved and loaded (cards 0186, 0188).
 *
 * Since settings version 5 the provider, colours and other per-user fields are UN2CUserSettings properties: the picker
 * saves the provider to the user's own config (SaveToUserConfig) and never touches Config/DefaultNodeToCode.ini.
 *
 * The provider and model picker in the NodeToCode window and Project Settings save through one path,
 * UN2CSettings::SaveToProjectConfig: it writes Config/DefaultNodeToCode.ini and removes the same keys from the
 * Saved/Config/<Platform>Editor/NodeToCode.ini layer, which loads after it. Before, the picker called SaveConfig(),
 * which wrote that Saved layer, so its choice overrode every later Project Settings edit at the next start.
 *
 * The cases that save use the host project's real config files (the engine's layering is what is under test), keep
 * a copy of both files and of the live settings, and put everything back at the end.
 */
BEGIN_DEFINE_SPEC(FN2CSettingsSaveSpec, "NodeToCode.Settings.Save", N2C_TEST_FLAGS)

	/** One config file's bytes, or that it did not exist */
	struct FFileCopy
	{
		FString Path;
		bool bExisted = false;
		TArray<uint8> Bytes;

		explicit FFileCopy(const FString& InPath)
			: Path(FPaths::ConvertRelativePathToFull(InPath))
		{
			bExisted = FFileHelper::LoadFileToArray(Bytes, *Path);
		}

		void Restore() const
		{
			if (bExisted)
			{
				FFileHelper::SaveArrayToFile(Bytes, *Path);
			}
			else
			{
				IFileManager::Get().Delete(*Path, false, true);
			}
		}
	};

	FString Section() const { return UN2CSettings::StaticClass()->GetPathName(); }
	FString ConfigName() const { return UN2CSettings::StaticClass()->ClassConfigName.ToString(); }

	/** As an editor start: the NodeToCode branch read again from the files (Default, then the Saved layer) */
	void ReloadConfigFromFiles() const
	{
		FConfigContext Context = FConfigContext::ForceReloadIntoGConfig();
		Context.bWriteDestIni = false;
		Context.Load(*ConfigName());
	}

	/** The "Key=" lines of a config file on disk */
	bool FileHasKey(const FString& Path, const FString& Key) const
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::ConvertRelativePathToFull(Path)))
		{
			return false;
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		const FString Prefix = Key + TEXT("=");
		return Lines.ContainsByPredicate([&Prefix](const FString& Line) { return Line.TrimStart().StartsWith(Prefix, ESearchCase::CaseSensitive); });
	}

	/** The user's Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini on disk (UN2CUserSettings) */
	static FString UserIniPath()
	{
		const FString UserConfig = UN2CUserSettings::StaticClass()->GetConfigName();
		const FConfigBranch* Branch = GConfig->FindBranch(FName(*UserConfig), UserConfig);
		return Branch ? FPaths::ConvertRelativePathToFull(Branch->IniPath) : FString();
	}

	/** As an editor start: the per-user config read again from its files */
	static void ReloadUserConfigFromFiles()
	{
		FConfigContext Context = FConfigContext::ForceReloadIntoGConfig();
		Context.bWriteDestIni = false;
		Context.Load(*UN2CUserSettings::StaticClass()->GetConfigName());
	}

	/** Settings loaded from the text of the project file, read with array commands as the editor reads it */
	UN2CSettings* LoadProjectText(const FString& ScratchPath, const FString& Text)
	{
		FConfigFile File;
		File.CombineFromBuffer(Text, ScratchPath);
		GConfig->SetFile(ScratchPath, &File);
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *ScratchPath);
		GConfig->UnloadFile(ScratchPath);
		return Settings;
	}

END_DEFINE_SPEC(FN2CSettingsSaveSpec)

void FN2CSettingsSaveSpec::Define()
{
	Describe(TEXT("the provider picker in the NodeToCode window"), [this]()
	{
		It(TEXT("saves the provider to the user's own config and the model to the project file (card 0188)"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
			const FString ProjectIni = Settings->GetDefaultConfigFilename();
			const FString SavedIni = Settings->GetClass()->GetConfigName();
			const FString UserConfig = UN2CUserSettings::StaticClass()->GetConfigName();
			const FString UserIni = UserIniPath();
			if (!TestFalse(TEXT("the user config file is known"), UserIni.IsEmpty()))
			{
				return;
			}
			GConfig->Flush(false, *ConfigName());
			GConfig->Flush(false, UserConfig);
			const FFileCopy ProjectCopy(ProjectIni);
			const FFileCopy SavedCopy(SavedIni);
			const FFileCopy UserCopy(UserIni);
			const EN2CLLMProvider ProviderBefore = UserSettings->Provider;
			const FString ModelBefore = Settings->AnthropicModelId;
			// Saving stamps the current layout version on the settings; later specs make settings from them
			const int32 VersionBefore = Settings->SettingsVersion;
			ON_SCOPE_EXIT
			{
				ProjectCopy.Restore();
				SavedCopy.Restore();
				UserCopy.Restore();
				ReloadConfigFromFiles();
				ReloadUserConfigFromFiles();
				UserSettings->Provider = ProviderBefore;
				Settings->AnthropicModelId = ModelBefore;
				Settings->SettingsVersion = VersionBefore;
			};

			// The picker: the provider alone, as OnProviderSelectionChanged saves it
			UserSettings->Provider = EN2CLLMProvider::Ollama;
			SN2CContextWindowVisualizer::SaveSettings({ GET_MEMBER_NAME_CHECKED(UN2CUserSettings, Provider) });
			GConfig->Flush(false, *ConfigName());
			TArray<uint8> ProjectAfter;
			const bool bProjectExists = FFileHelper::LoadFileToArray(ProjectAfter, *ProjectCopy.Path);
			TestTrue(TEXT("provider: the project file is untouched"), bProjectExists == ProjectCopy.bExisted && ProjectAfter == ProjectCopy.Bytes);
			TestFalse(TEXT("provider: not in the project's Saved layer"), FileHasKey(SavedIni, TEXT("Provider")));
			TestTrue(TEXT("provider: written to the user's own config file"), FileHasKey(UserIni, TEXT("Provider")));
			FString Loaded;
			GConfig->GetString(*UN2CUserSettings::StaticClass()->GetPathName(), TEXT("Provider"), Loaded, UserConfig);
			TestEqual(TEXT("provider: the user config holds it"), Loaded, FString(TEXT("Ollama")));

			// A model choice still goes to the project file, the provider still to the user's
			Settings->AnthropicModelId = TEXT("n2c-test-model-0188");
			UserSettings->Provider = EN2CLLMProvider::Anthropic;
			SN2CContextWindowVisualizer::SaveSettings({ GET_MEMBER_NAME_CHECKED(UN2CUserSettings, Provider), GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModelId) });
			TestTrue(TEXT("model: written to the project file"), FileHasKey(ProjectIni, TEXT("AnthropicModelId")));
			TestFalse(TEXT("model: the provider is not written to the project file"), FileHasKey(ProjectIni, TEXT("Provider")));
			GConfig->GetString(*UN2CUserSettings::StaticClass()->GetPathName(), TEXT("Provider"), Loaded, UserConfig);
			TestEqual(TEXT("model: the provider is saved per user"), Loaded, FString(TEXT("Anthropic")));

			// The next start loads the user's provider from their own config file
			ReloadUserConfigFromFiles();
			UN2CUserSettings* Restarted = NewObject<UN2CUserSettings>(GetTransientPackage());
			Restarted->LoadConfig();
			TestEqual(TEXT("provider loaded at the next start"), Restarted->Provider, EN2CLLMProvider::Anthropic);
		});
	});

	Describe(TEXT("Editor Preferences"), [this]()
	{
		It(TEXT("restyles nothing while a UI colour is dragged, restyles once it is released, and never writes the project file"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
			const FString ProjectIni = Settings->GetDefaultConfigFilename();
			const FFileCopy ProjectCopy(ProjectIni);
			const FN2CUIColors ColorsBefore = UserSettings->UIColors;
			const int32 UserVersionBefore = UserSettings->SettingsVersion;
			ON_SCOPE_EXIT
			{
				ProjectCopy.Restore();
				UserSettings->UIColors = ColorsBefore;
				UserSettings->SettingsVersion = UserVersionBefore;
				N2CStyle::UpdateBorderBrushes();
			};

			FStructProperty* ColorsProperty = CastField<FStructProperty>(UN2CUserSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CUserSettings, UIColors)));
			FProperty* LeafProperty = FN2CUIColors::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FN2CUIColors, BorderSubtle));
			if (!TestNotNull(TEXT("UIColors property"), ColorsProperty) || !TestNotNull(TEXT("BorderSubtle property"), LeafProperty))
			{
				return;
			}
			TestNull(TEXT("UI Colors is no longer a project setting"), UN2CSettings::StaticClass()->FindPropertyByName(TEXT("UIColors")));
			const FColor Picked(12, 34, 56);
			const FLinearColor PickedLinear = FN2CUIColors::ToLinear(Picked);
			if (!TestFalse(TEXT("the picked colour differs from the current one"), UserSettings->UIColors.BorderSubtle == Picked))
			{
				return;
			}

			// The project file's time is set back, so any write shows
			const FDateTime Old(2001, 1, 1);
			IFileManager::Get().SetTimeStamp(*ProjectCopy.Path, Old);
			const FDateTime Before = IFileManager::Get().GetTimeStamp(*ProjectCopy.Path);

			// A drag: the colour picker sends an Interactive change for the colour inside UI Colors on each move
			UserSettings->PreEditChange(LeafProperty);
			UserSettings->UIColors.BorderSubtle = Picked;
			FPropertyChangedEvent Drag(LeafProperty, EPropertyChangeType::Interactive);
			Drag.SetActiveMemberProperty(ColorsProperty);
			UserSettings->PostEditChangeProperty(Drag);
			TestFalse(TEXT("drag: panel border style not rebuilt"), N2CStyle::GetPanelBorderBrush().OutlineSettings.Color.GetSpecifiedColor().Equals(PickedLinear));

			// Released: one ValueSet change
			FPropertyChangedEvent Release(LeafProperty, EPropertyChangeType::ValueSet);
			Release.SetActiveMemberProperty(ColorsProperty);
			UserSettings->PostEditChangeProperty(Release);
			TestTrue(TEXT("release: panel border style rebuilt"), N2CStyle::GetPanelBorderBrush().OutlineSettings.Color.GetSpecifiedColor().Equals(PickedLinear));
			TestEqual(TEXT("the project file is not written"), IFileManager::Get().GetTimeStamp(*ProjectCopy.Path), Before);
			TArray<uint8> After;
			const bool bExists = FFileHelper::LoadFileToArray(After, *ProjectCopy.Path);
			TestTrue(TEXT("the project file is unchanged on disk"), bExists == ProjectCopy.bExisted && After == ProjectCopy.Bytes);
		});
	});

	Describe(TEXT("loading the config"), [this]()
	{
		It(TEXT("drops a missing reference file and estimates the others' tokens once the config is loaded"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CSettingsSave"));
			const FString Present = Dir / TEXT("Present.h");
			const FString Missing = Dir / TEXT("Missing.h");
			TestTrue(TEXT("reference file written"), FFileHelper::SaveStringToFile(FString::ChrN(40, TEXT('x')), *Present));

			// As a project file saved it; the token estimate saved there is stale
			const FString Path = Dir / TEXT("DefaultNodeToCode.ini");
			const FString Text = FString::Printf(TEXT("[%s]\n+ReferenceSourceFilePaths=(FilePath=\"%s\")\n+ReferenceSourceFilePaths=(FilePath=\"%s\")\nEstimatedReferenceTokens=999\n"),
				*Section(), *Present, *Missing);
			FConfigFile File;
			File.CombineFromBuffer(Text, Path);
			GConfig->SetFile(Path, &File);
			UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
			// The config reload that applies the NodeToCode branch to the settings at editor start (PostReloadConfig)
			Settings->LoadConfig(nullptr, *Path, UE::LCPF_ReloadingConfigData);
			GConfig->UnloadFile(Path);

			if (TestEqual(TEXT("reference files kept"), Settings->ReferenceSourceFilePaths.Num(), 1) && Settings->ReferenceSourceFilePaths.Num() == 1)
			{
				TestEqual(TEXT("the file that exists"), Settings->ReferenceSourceFilePaths[0].FilePath, Present);
			}
			TestEqual(TEXT("estimated tokens (40 characters / 4)"), Settings->EstimatedReferenceTokens, 10);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
