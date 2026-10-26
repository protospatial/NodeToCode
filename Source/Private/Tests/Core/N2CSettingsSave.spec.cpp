// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CContextWindowVisualizer.h"
#include "Tests/N2CTestHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/**
 * How the settings are saved and loaded (card 0186).
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
		It(TEXT("saves to the project file, so a later Project Settings choice is the one loaded at the next start"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			const FString ProjectIni = Settings->GetDefaultConfigFilename();
			const FString SavedIni = Settings->GetClass()->GetConfigName();
			const FFileCopy ProjectCopy(ProjectIni);
			const FFileCopy SavedCopy(SavedIni);
			const EN2CLLMProvider ProviderBefore = Settings->Provider;
			// Saving stamps the current layout version on the settings; later specs make settings from them
			const int32 VersionBefore = Settings->SettingsVersion;
			const FName ProviderName = GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider);
			FProperty* ProviderProperty = UN2CSettings::StaticClass()->FindPropertyByName(ProviderName);
			if (!TestNotNull(TEXT("Provider property"), ProviderProperty))
			{
				return;
			}

			// The picker: Provider set, then saved as OnProviderSelectionChanged saves it
			Settings->Provider = EN2CLLMProvider::Ollama;
			SN2CContextWindowVisualizer::SaveSettings({ ProviderName });
			GConfig->Flush(false, *ConfigName());
			TestTrue(TEXT("picker: Provider written to the project file"), FileHasKey(ProjectIni, TEXT("Provider")));
			TestFalse(TEXT("picker: Provider not in the Saved layer"), FileHasKey(SavedIni, TEXT("Provider")));

			// Project Settings, as the details panel and SSettingsEditor::NotifyPostChange run it for a committed edit
			Settings->PreEditChange(ProviderProperty);
			Settings->Provider = EN2CLLMProvider::Gemini;
			FPropertyChangedEvent Event(ProviderProperty, EPropertyChangeType::ValueSet);
			Settings->PostEditChangeProperty(Event);
			Settings->UpdateSinglePropertyInConfigFile(ProviderProperty, ProjectIni);

			// Editor exit writes GConfig's changes to the Saved layer; the next start reads the files again
			GConfig->Flush(false, *ConfigName());
			ReloadConfigFromFiles();
			FString Loaded;
			GConfig->GetString(*Section(), TEXT("Provider"), Loaded, SavedIni);
			TestEqual(TEXT("Provider at the next start"), Loaded, FString(TEXT("Gemini")));
			TestFalse(TEXT("Provider not in the Saved layer"), FileHasKey(SavedIni, TEXT("Provider")));
			UN2CSettings* Restarted = NewObject<UN2CSettings>(GetTransientPackage());
			Restarted->LoadConfig();
			TestEqual(TEXT("settings loaded at the next start"), Restarted->Provider, EN2CLLMProvider::Gemini);

			ProjectCopy.Restore();
			SavedCopy.Restore();
			ReloadConfigFromFiles();
			Settings->Provider = ProviderBefore;
			Settings->SettingsVersion = VersionBefore;
		});

		It(TEXT("removes a provider an older picker left in the Saved layer"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			const FString ProjectIni = Settings->GetDefaultConfigFilename();
			const FString SavedIni = Settings->GetClass()->GetConfigName();
			const FFileCopy ProjectCopy(ProjectIni);
			const FFileCopy SavedCopy(SavedIni);
			const EN2CLLMProvider ProviderBefore = Settings->Provider;
			const int32 VersionBefore = Settings->SettingsVersion;

			// What SaveConfig() left there before card 0186
			GConfig->Flush(false, *ConfigName());
			FString SavedText;
			FFileHelper::LoadFileToString(SavedText, *SavedCopy.Path);
			SavedText += FString::Printf(TEXT("\r\n[%s]\r\nProvider=Ollama\r\n"), *Section());
			TestTrue(TEXT("stale Saved layer written"), FFileHelper::SaveStringToFile(SavedText, *SavedCopy.Path));
			ReloadConfigFromFiles();

			Settings->Provider = EN2CLLMProvider::DeepSeek;
			SN2CContextWindowVisualizer::SaveSettings({ GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider) });
			GConfig->Flush(false, *ConfigName());
			ReloadConfigFromFiles();
			FString Loaded;
			GConfig->GetString(*Section(), TEXT("Provider"), Loaded, SavedIni);
			TestEqual(TEXT("Provider at the next start"), Loaded, FString(TEXT("DeepSeek")));
			TestFalse(TEXT("old Provider removed from the Saved layer"), FileHasKey(SavedIni, TEXT("Provider")));

			ProjectCopy.Restore();
			SavedCopy.Restore();
			ReloadConfigFromFiles();
			Settings->Provider = ProviderBefore;
			Settings->SettingsVersion = VersionBefore;
		});
	});

	Describe(TEXT("Project Settings"), [this]()
	{
		It(TEXT("saves and restyles nothing while a colour is dragged, and saves UI Colors once it is released"), [this]()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			const FString ProjectIni = Settings->GetDefaultConfigFilename();
			const FFileCopy ProjectCopy(ProjectIni);
			const FFileCopy SavedCopy(Settings->GetClass()->GetConfigName());
			const FN2CUIColors ColorsBefore = Settings->UIColors;
			const int32 VersionBefore = Settings->SettingsVersion;

			FStructProperty* ColorsProperty = CastField<FStructProperty>(UN2CSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CSettings, UIColors)));
			FProperty* LeafProperty = FN2CUIColors::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FN2CUIColors, BorderSubtle));
			if (!TestNotNull(TEXT("UIColors property"), ColorsProperty) || !TestNotNull(TEXT("BorderSubtle property"), LeafProperty))
			{
				return;
			}
			const FColor Picked(12, 34, 56);
			const FLinearColor PickedLinear = FN2CUIColors::ToLinear(Picked);
			if (!TestFalse(TEXT("the picked colour differs from the current one"), Settings->UIColors.BorderSubtle == Picked))
			{
				return;
			}

			// The project file's time is set back, so any write shows
			const FDateTime Old(2001, 1, 1);
			IFileManager::Get().SetTimeStamp(*ProjectCopy.Path, Old);
			const FDateTime Before = IFileManager::Get().GetTimeStamp(*ProjectCopy.Path);

			// A drag: the colour picker sends an Interactive change for the colour inside UI Colors on each move
			Settings->PreEditChange(LeafProperty);
			Settings->UIColors.BorderSubtle = Picked;
			FPropertyChangedEvent Drag(LeafProperty, EPropertyChangeType::Interactive);
			Drag.SetActiveMemberProperty(ColorsProperty);
			Settings->PostEditChangeProperty(Drag);
			TestEqual(TEXT("drag: project file not written"), IFileManager::Get().GetTimeStamp(*ProjectCopy.Path), Before);
			TArray<uint8> AfterDrag;
			const bool bExistsAfterDrag = FFileHelper::LoadFileToArray(AfterDrag, *ProjectCopy.Path);
			TestTrue(TEXT("drag: project file unchanged on disk"), bExistsAfterDrag == ProjectCopy.bExisted && AfterDrag == ProjectCopy.Bytes);
			TestFalse(TEXT("drag: panel border style not rebuilt"), N2CStyle::GetPanelBorderBrush().OutlineSettings.Color.GetSpecifiedColor().Equals(PickedLinear));

			// Released: one ValueSet change
			FPropertyChangedEvent Release(LeafProperty, EPropertyChangeType::ValueSet);
			Release.SetActiveMemberProperty(ColorsProperty);
			Settings->PostEditChangeProperty(Release);
			TestTrue(TEXT("release: panel border style rebuilt"), N2CStyle::GetPanelBorderBrush().OutlineSettings.Color.GetSpecifiedColor().Equals(PickedLinear));
			FString Text;
			FFileHelper::LoadFileToString(Text, *ProjectCopy.Path);
			N2CTest::FScratchDir Dir(TEXT("N2CSettingsSave"));
			const UN2CSettings* Loaded = LoadProjectText(Dir / TEXT("DefaultNodeToCode.ini"), Text);
			TestEqual(TEXT("release: UI Colors in the project file has the picked colour"), Loaded->UIColors.BorderSubtle, Picked);
			if (Loaded->UIColors.BorderSubtle != Picked)
			{
				AddInfo(FString::Printf(TEXT("%s holds:\n%s"), *ProjectCopy.Path, *Text));
			}

			ProjectCopy.Restore();
			SavedCopy.Restore();
			ReloadConfigFromFiles();
			Settings->UIColors = ColorsBefore;
			Settings->SettingsVersion = VersionBefore;
			N2CStyle::UpdateBorderBrushes();
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
