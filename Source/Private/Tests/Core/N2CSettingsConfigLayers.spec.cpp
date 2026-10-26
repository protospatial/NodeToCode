// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "Tests/N2CTestHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

/**
 * Where Custom Toolsets (an array setting) is saved and how each config layer reads it back (card 0479).
 *
 * The editor reads the project's Config/DefaultNodeToCode.ini with the array commands (!Key=ClearArray, +Key=Value),
 * but reads the Saved/Config/<Platform>/NodeToCode.ini layer it writes itself without them (FConfigContext fills the
 * saved layer with FillFileFromDisk(..., bHandleSymbolCommands = false) on UE 5.6, 5.7 and 5.8): there an array is
 * repeated plain Key=Value lines, and a "+Key=Value" line is a key named "+Key" that no property reads.
 */
BEGIN_DEFINE_SPEC(FN2CSettingsConfigLayersSpec, "NodeToCode.Settings.ConfigLayers", N2C_TEST_FLAGS)

	const TArray<FString> Picked = { TEXT("PythonTools"), TEXT("FileSystemTools") };

	FString Section() const { return UN2CSettings::StaticClass()->GetPathName(); }

	/** Settings loaded from File (registered in GConfig as Path for the load only), as LoadConfig loads a branch */
	UN2CSettings* LoadFrom(const FString& Path, const FConfigFile& File)
	{
		GConfig->SetFile(Path, &File);
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *Path);
		GConfig->UnloadFile(Path);
		return Settings;
	}

	/** Text read as the editor reads its Saved/Config layer: no array commands */
	UN2CSettings* LoadAsSavedLayer(const FString& Path, const FString& Text)
	{
		FConfigFile File;
		File.ProcessInputFileContents(Text, Path);
		return LoadFrom(Path, File);
	}

	/** Text read as the editor reads Config/DefaultNodeToCode.ini: with array commands */
	UN2CSettings* LoadAsProjectLayer(const FString& Path, const FString& Text)
	{
		FConfigFile File;
		File.CombineFromBuffer(Text, Path);
		return LoadFrom(Path, File);
	}

	FString SectionText(const FString& Lines) const
	{
		return FString::Printf(TEXT("[%s]\nMcpToolProfile=Custom\n%s\n"), *Section(), *Lines);
	}

END_DEFINE_SPEC(FN2CSettingsConfigLayersSpec)

void FN2CSettingsConfigLayersSpec::Define()
{
	Describe(TEXT("the Saved/Config layer"), [this]()
	{
		It(TEXT("gives back the Custom Toolsets the editor wrote there"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CConfigLayers"));
			const FString Path = Dir / TEXT("NodeToCode.ini");

			// What the editor writes to Saved/Config when it saves the settings outside the project layer
			UN2CSettings* Written = NewObject<UN2CSettings>(GetTransientPackage());
			Written->McpToolProfile = EN2CMcpToolProfile::Custom;
			Written->McpCustomToolsets = Picked;
			FConfigCacheIni Sandbox(EConfigCacheType::Temporary);
			FConfigFile& SavedFile = Sandbox.Add(Path, FConfigFile());
			Written->SaveConfig(CPF_Config, *Path, &Sandbox, false);
			FString Text;
			SavedFile.WriteToString(Text, Path);
			TestTrue(TEXT("saved layer written"), FFileHelper::SaveStringToFile(Text, *Path));

			FString OnDisk;
			FFileHelper::LoadFileToString(OnDisk, *Path);
			const UN2CSettings* Loaded = LoadAsSavedLayer(Path, OnDisk);
			TestEqual(TEXT("Tool Profile"), Loaded->McpToolProfile, EN2CMcpToolProfile::Custom);
			TestEqual(TEXT("Custom Toolsets"), Loaded->McpCustomToolsets, Picked);
		});

		It(TEXT("reads an array as repeated Key=Value lines"), [this]()
		{
			N2CTest::FScratchDir Dir(TEXT("N2CConfigLayers"));
			const UN2CSettings* Loaded = LoadAsSavedLayer(Dir / TEXT("NodeToCode.ini"),
				SectionText(TEXT("McpCustomToolsets=PythonTools\nMcpCustomToolsets=FileSystemTools")));
			TestEqual(TEXT("Custom Toolsets"), Loaded->McpCustomToolsets, Picked);
		});

		It(TEXT("does not read +Key array commands, which only the project layer understands"), [this]()
		{
			// What Tests/mcp_probe/live_epic_overlap_0174.py first wrote there: it loaded as an empty list (card 0479)
			const FString Lines = TEXT("!McpCustomToolsets=ClearArray\n+McpCustomToolsets=PythonTools\n+McpCustomToolsets=FileSystemTools");
			N2CTest::FScratchDir Dir(TEXT("N2CConfigLayers"));
			const UN2CSettings* Saved = LoadAsSavedLayer(Dir / TEXT("NodeToCode.ini"), SectionText(Lines));
			TestEqual(TEXT("Saved layer: Custom Toolsets"), Saved->McpCustomToolsets.Num(), 0);
			const UN2CSettings* Project = LoadAsProjectLayer(Dir / TEXT("DefaultNodeToCode.ini"), SectionText(Lines));
			TestEqual(TEXT("project layer: Custom Toolsets"), Project->McpCustomToolsets, Picked);
		});
	});

	Describe(TEXT("Project Settings"), [this]()
	{
		It(TEXT("saves picked Custom Toolsets to the project layer, which gives them back"), [this]()
		{
			// As SSettingsEditor::NotifyPostChange: the class is DefaultConfig, so a scalar edit goes to
			// UpdateSinglePropertyInConfigFile and an array edit (add, then pick a value) to Section->Save(), which is
			// TryUpdateDefaultConfigFile. UN2CSettings::PostEditChangeProperty runs first and writes the same file.
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			const FString DefaultIni = FPaths::ConvertRelativePathToFull(Settings->GetDefaultConfigFilename());
			TArray<uint8> Before;
			const bool bExisted = FFileHelper::LoadFileToArray(Before, *DefaultIni);
			const EN2CMcpToolProfile ProfileBefore = Settings->McpToolProfile;
			const TArray<FString> ToolsetsBefore = Settings->McpCustomToolsets;
			// PreEditChange stamps the current layout version on the settings; later specs make settings from them
			const int32 VersionBefore = Settings->SettingsVersion;

			FProperty* ProfileProperty = UN2CSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CSettings, McpToolProfile));
			FArrayProperty* ArrayProperty = CastField<FArrayProperty>(UN2CSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CSettings, McpCustomToolsets)));
			if (!TestNotNull(TEXT("Tool Profile property"), ProfileProperty) || !TestNotNull(TEXT("Custom Toolsets property"), ArrayProperty))
			{
				return;
			}

			Settings->PreEditChange(ProfileProperty);
			Settings->McpToolProfile = EN2CMcpToolProfile::Custom;
			FPropertyChangedEvent ProfileEvent(ProfileProperty, EPropertyChangeType::ValueSet);
			Settings->PostEditChangeProperty(ProfileEvent);
			Settings->UpdateSinglePropertyInConfigFile(ProfileProperty, Settings->GetDefaultConfigFilename());

			Settings->McpCustomToolsets.Reset();
			for (const FString& Name : Picked)
			{
				Settings->PreEditChange(ArrayProperty);
				Settings->McpCustomToolsets.Add(FString());
				FPropertyChangedEvent AddEvent(ArrayProperty, EPropertyChangeType::ArrayAdd);
				Settings->PostEditChangeProperty(AddEvent);
				Settings->TryUpdateDefaultConfigFile();

				Settings->PreEditChange(ArrayProperty);
				Settings->McpCustomToolsets.Last() = Name;
				FPropertyChangedEvent SetEvent(ArrayProperty->Inner, EPropertyChangeType::ValueSet);
				SetEvent.SetActiveMemberProperty(ArrayProperty);
				Settings->PostEditChangeProperty(SetEvent);
				Settings->TryUpdateDefaultConfigFile();
			}

			FString Text;
			FFileHelper::LoadFileToString(Text, *DefaultIni);
			N2CTest::FScratchDir Dir(TEXT("N2CConfigLayers"));
			const UN2CSettings* Loaded = LoadAsProjectLayer(Dir / TEXT("DefaultNodeToCode.ini"), Text);
			TestEqual(TEXT("Tool Profile"), Loaded->McpToolProfile, EN2CMcpToolProfile::Custom);
			TestEqual(TEXT("Custom Toolsets"), Loaded->McpCustomToolsets, Picked);
			if (Loaded->McpCustomToolsets != Picked)
			{
				AddInfo(FString::Printf(TEXT("%s holds:\n%s"), *DefaultIni, *Text));
			}

			// Put the project file, GConfig's branch and the live settings back
			if (bExisted)
			{
				FFileHelper::SaveArrayToFile(Before, *DefaultIni);
			}
			else
			{
				IFileManager::Get().Delete(*DefaultIni, false, true);
			}
			FConfigContext Context = FConfigContext::ForceReloadIntoGConfig();
			Context.bWriteDestIni = false;
			Context.Load(*UN2CSettings::StaticClass()->ClassConfigName.ToString());
			Settings->McpToolProfile = ProfileBefore;
			Settings->McpCustomToolsets = ToolsetsBefore;
			Settings->SettingsVersion = VersionBefore;
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
