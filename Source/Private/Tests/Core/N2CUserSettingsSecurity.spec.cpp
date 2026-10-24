// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CUserSettings.h"
#include "Tests/N2CTestHelpers.h"
#include "HAL/PlatformProperties.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"

/**
 * The MCP security switches (Allow MCP Python Execution, Enable MCP Server) come only from the user's own config
 * layer. Each case builds an isolated project in a scratch folder (Config/DefaultEditorPerProjectUserSettings.ini as
 * a committed file would be, Saved/Config/<Platform>/EditorPerProjectUserSettings.ini as the user's file), loads the
 * EditorPerProjectUserSettings hierarchy into a temporary config system the way the editor does, and resolves the
 * switch from the loaded branch. GConfig and the host's own Config and Saved folders are never touched.
 */
BEGIN_DEFINE_SPEC(FN2CUserSettingsSecuritySpec, "NodeToCode.Security.UserSettings", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;

	static FString SectionHeader()
	{
		return FString::Printf(TEXT("[%s]"), *UN2CUserSettings::StaticClass()->GetPathName());
	}

	static FName PythonKey() { return GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bAllowMcpPythonExecution); }
	static FName ServerKey() { return GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bEnableMcpServer); }

	/** Writes "[section]\nKey=Value" lines to Path (an empty Lines writes nothing). */
	static void WriteIni(const FString& Path, const FString& Lines)
	{
		if (!Lines.IsEmpty())
		{
			FFileHelper::SaveStringToFile(SectionHeader() + TEXT("\r\n") + Lines + TEXT("\r\n"), *Path);
		}
	}

	/**
	 * Loads the scratch project's EditorPerProjectUserSettings hierarchy (engine Base layers, the scratch project's
	 * Default file, the scratch Saved file) into a temporary config system and resolves Key from it.
	 */
	FN2CPerUserBool LoadAndResolve(const FString& DefaultLines, const FString& SavedLines, FName Key, bool bCodeDefault,
		bool& bOutLoadedValue)
	{
		const FString ProjectConfigDir = Scratch->Path / TEXT("Config/");
		const FString GeneratedConfigDir = Scratch->Path / TEXT("Saved/Config/");
		const FString SavedFile = FConfigCacheIni::GetDestIniFilename(TEXT("EditorPerProjectUserSettings"),
			*FString(FPlatformProperties::PlatformName()), *GeneratedConfigDir);
		WriteIni(ProjectConfigDir / TEXT("DefaultEditorPerProjectUserSettings.ini"), DefaultLines);
		WriteIni(SavedFile, SavedLines);

		FConfigCacheIni ConfigSystem(EConfigCacheType::Temporary);
		FConfigContext Context = FConfigContext::ReadIntoConfigSystem(&ConfigSystem, FString());
		Context.ProjectConfigDir = ProjectConfigDir;
		Context.GeneratedConfigDir = GeneratedConfigDir;
		Context.bAllowGeneratedIniWhenCooked = true;	// so the context reads the Saved file outside GConfig too
		FString LoadedFilename;
		Context.Load(TEXT("EditorPerProjectUserSettings"), LoadedFilename);
		TestEqual(TEXT("the fixture's Saved file is the branch's file"), FPaths::ConvertRelativePathToFull(LoadedFilename),
			FPaths::ConvertRelativePathToFull(SavedFile));

		const FConfigBranch* Branch = ConfigSystem.FindBranch(TEXT("EditorPerProjectUserSettings"), LoadedFilename);
		TestNotNull(TEXT("the fixture branch loaded"), Branch);

		// What Unreal's own property load would see (all layers merged): proves the fixture really sets the value
		bOutLoadedValue = bCodeDefault;
		ConfigSystem.GetBool(*UN2CUserSettings::StaticClass()->GetPathName(), *Key.ToString(), bOutLoadedValue, LoadedFilename);

		return UN2CUserSettings::ResolvePerUserBool(Branch, UN2CUserSettings::StaticClass()->GetPathName(), Key, bCodeDefault);
	}

END_DEFINE_SPEC(FN2CUserSettingsSecuritySpec)

void FN2CUserSettingsSecuritySpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CUserSettings"));
	});

	AfterEach([this]()
	{
		Scratch.Reset();
	});

	Describe(TEXT("Allow MCP Python Execution"), [this]()
	{
		It(TEXT("stays off when only the project's committed DefaultEditorPerProjectUserSettings.ini turns it on"), [this]()
		{
			bool bLoaded = false;
			const FN2CPerUserBool Result = LoadAndResolve(TEXT("bAllowMcpPythonExecution=True"), FString(), PythonKey(),
				UN2CUserSettings::bDefaultAllowMcpPythonExecution, bLoaded);
			TestTrue(TEXT("Unreal's merged config holds the project's True (the 0090 reproduction)"), bLoaded);
			TestFalse(TEXT("the switch the plugin uses stays off"), Result.bValue);
			TestFalse(TEXT("no user layer set it"), Result.bFromUserLayer);
			TestTrue(TEXT("the ignored project value is reported for the warning"), Result.bIgnoredSharedValue);
		});

		It(TEXT("turns on when the user's own Saved/Config file turns it on"), [this]()
		{
			bool bLoaded = false;
			const FN2CPerUserBool Result = LoadAndResolve(FString(), TEXT("bAllowMcpPythonExecution=True"), PythonKey(),
				UN2CUserSettings::bDefaultAllowMcpPythonExecution, bLoaded);
			TestTrue(TEXT("on"), Result.bValue);
			TestTrue(TEXT("from the user layer"), Result.bFromUserLayer);
			TestFalse(TEXT("nothing ignored"), Result.bIgnoredSharedValue);
		});

		It(TEXT("stays off when the project turns it on and the user's file turns it off"), [this]()
		{
			bool bLoaded = true;
			const FN2CPerUserBool Result = LoadAndResolve(TEXT("bAllowMcpPythonExecution=True"),
				TEXT("bAllowMcpPythonExecution=False"), PythonKey(), UN2CUserSettings::bDefaultAllowMcpPythonExecution, bLoaded);
			TestFalse(TEXT("off"), Result.bValue);
			TestTrue(TEXT("from the user layer"), Result.bFromUserLayer);
			TestFalse(TEXT("no warning: the user's own value won"), Result.bIgnoredSharedValue);
		});

		It(TEXT("is off with no value in any layer, and says nothing"), [this]()
		{
			bool bLoaded = false;
			const FN2CPerUserBool Result = LoadAndResolve(FString(), FString(), PythonKey(),
				UN2CUserSettings::bDefaultAllowMcpPythonExecution, bLoaded);
			TestFalse(TEXT("off"), Result.bValue);
			TestFalse(TEXT("not from the user layer"), Result.bFromUserLayer);
			TestFalse(TEXT("nothing ignored"), Result.bIgnoredSharedValue);
		});
	});

	Describe(TEXT("Enable MCP Server"), [this]()
	{
		It(TEXT("ignores a project-shared value and keeps the per-user default"), [this]()
		{
			bool bLoaded = true;
			const FN2CPerUserBool Result = LoadAndResolve(TEXT("bEnableMcpServer=False"), FString(), ServerKey(),
				UN2CUserSettings::bDefaultEnableMcpServer, bLoaded);
			TestFalse(TEXT("Unreal's merged config holds the project's False"), bLoaded);
			TestTrue(TEXT("the per-user default (on) is used"), Result.bValue);
			TestTrue(TEXT("the ignored project value is reported"), Result.bIgnoredSharedValue);
		});

		It(TEXT("turns off when the user's own Saved/Config file turns it off"), [this]()
		{
			bool bLoaded = true;
			const FN2CPerUserBool Result = LoadAndResolve(FString(), TEXT("bEnableMcpServer=False"), ServerKey(),
				UN2CUserSettings::bDefaultEnableMcpServer, bLoaded);
			TestFalse(TEXT("off"), Result.bValue);
			TestTrue(TEXT("from the user layer"), Result.bFromUserLayer);
		});
	});

	It(TEXT("falls back to the code default without a config branch"), [this]()
	{
		const FN2CPerUserBool Result = UN2CUserSettings::ResolvePerUserBool(nullptr,
			UN2CUserSettings::StaticClass()->GetPathName(), PythonKey(), false);
		TestFalse(TEXT("off"), Result.bValue);
		TestFalse(TEXT("nothing ignored"), Result.bIgnoredSharedValue);
	});

	It(TEXT("uses the per-user value in this editor and shows it in Project Settings"), [this]()
	{
		const UClass* SettingsClass = UN2CUserSettings::StaticClass();
		const FConfigBranch* Branch = GConfig->FindBranch(SettingsClass->ClassConfigName, SettingsClass->GetConfigName());
		const FN2CPerUserBool Python = UN2CUserSettings::ResolvePerUserBool(Branch, SettingsClass->GetPathName(),
			PythonKey(), UN2CUserSettings::bDefaultAllowMcpPythonExecution);
		TestEqual(TEXT("IsMcpPythonExecutionAllowed reads the user layer"), UN2CUserSettings::IsMcpPythonExecutionAllowed(), Python.bValue);
		TestEqual(TEXT("the settings object shows the value in effect"),
			GetDefault<UN2CUserSettings>()->bAllowMcpPythonExecution, UN2CUserSettings::IsMcpPythonExecutionAllowed());
		TestEqual(TEXT("the settings object shows the server switch in effect"),
			GetDefault<UN2CUserSettings>()->bEnableMcpServer, UN2CUserSettings::IsMcpServerEnabled());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
