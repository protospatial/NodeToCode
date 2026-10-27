// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CUserSettings.h"

#include "Misc/ConfigCacheIni.h"
#include "Utils/N2CLogger.h"

namespace N2CUserSettingsPrivate
{
	/** The last Set value of Key in one user layer, or nullptr when the layer does not set it. */
	const FConfigValue* FindLastSetValue(const FConfigCommandStream& Layer, const FString& Section, FName Key)
	{
		const FConfigCommandStreamSection* LayerSection = Layer.Find(Section);
		if (!LayerSection)
		{
			return nullptr;
		}

		const FConfigValue* Last = nullptr;
		for (FConfigSectionMap::TConstKeyIterator It(*LayerSection, Key); It; ++It)
		{
			const FConfigValue::EValueType Type = It.Value().ValueType;
			// A Saved file is read without +/- commands, so its plain Key=Value lines arrive as ArrayAdd; -ini: overrides
			// arrive as Set
			if (Type == FConfigValue::EValueType::Set || Type == FConfigValue::EValueType::Combined
				|| Type == FConfigValue::EValueType::ArrayAdd || Type == FConfigValue::EValueType::ArrayAddUnique)
			{
				Last = &It.Value();
			}
		}
		return Last;
	}

	struct FEffectiveSwitches
	{
		bool bEnableMcpServer = UN2CUserSettings::bDefaultEnableMcpServer;
		bool bAllowMcpPythonExecution = UN2CUserSettings::bDefaultAllowMcpPythonExecution;
		bool bAllowMcpWritesOutsideProject = UN2CUserSettings::bDefaultAllowMcpWritesOutsideProject;
		bool bRequireMcpBearerToken = UN2CUserSettings::bDefaultRequireMcpBearerToken;
	};

	void WarnIgnored(const TCHAR* Key, const TCHAR* DisplayName, bool bIgnoredValue, bool bUsedValue)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(
			TEXT("Ignored %s=%s from a project-shared config layer (for example the project's Config/DefaultEditorPerProjectUserSettings.ini): ")
			TEXT("'%s' is a per-user setting and only your own value (in %s) counts, so it stays %s. ")
			TEXT("Set it in Project Settings > Plugins > Node to Code (User); if the project file sets the same value, remove it there first, ")
			TEXT("because Unreal does not save a value that matches the project file."),
			Key, bIgnoredValue ? TEXT("True") : TEXT("False"), DisplayName,
			*FPaths::ConvertRelativePathToFull(GEditorPerProjectIni), bUsedValue ? TEXT("on") : TEXT("off")));
	}

	/** Resolved once per editor session: every switch needs an editor restart, and each warning is logged once. */
	const FEffectiveSwitches& GetEffectiveSwitches()
	{
		static const FEffectiveSwitches Switches = []()
		{
			FEffectiveSwitches Result;
			UN2CUserSettings* Settings = GetMutableDefault<UN2CUserSettings>();
			const UClass* SettingsClass = UN2CUserSettings::StaticClass();
			const FString Section = SettingsClass->GetPathName();
			const FConfigBranch* Branch = GConfig ? GConfig->FindBranch(SettingsClass->ClassConfigName, SettingsClass->GetConfigName()) : nullptr;
			if (!Branch)
			{
				FN2CLogger::Get().LogWarning(FString::Printf(
					TEXT("Could not find the %s config branch: MCP switches use their defaults (server on, Python execution off, writes outside the project off, bearer token not required)"),
					*SettingsClass->ClassConfigName.ToString()));
			}

			const FN2CPerUserBool Server = UN2CUserSettings::ResolvePerUserBool(Branch, Section,
				GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bEnableMcpServer), UN2CUserSettings::bDefaultEnableMcpServer);
			const FN2CPerUserBool Python = UN2CUserSettings::ResolvePerUserBool(Branch, Section,
				GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bAllowMcpPythonExecution), UN2CUserSettings::bDefaultAllowMcpPythonExecution);
			const FN2CPerUserBool Writes = UN2CUserSettings::ResolvePerUserBool(Branch, Section,
				GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bAllowMcpWritesOutsideProject), UN2CUserSettings::bDefaultAllowMcpWritesOutsideProject);
			const FN2CPerUserBool Token = UN2CUserSettings::ResolvePerUserBool(Branch, Section,
				GET_MEMBER_NAME_CHECKED(UN2CUserSettings, bRequireMcpBearerToken), UN2CUserSettings::bDefaultRequireMcpBearerToken);

			if (Server.bIgnoredSharedValue)
			{
				WarnIgnored(TEXT("bEnableMcpServer"), TEXT("Enable MCP Server"), !Server.bValue, Server.bValue);
			}
			if (Python.bIgnoredSharedValue)
			{
				WarnIgnored(TEXT("bAllowMcpPythonExecution"), TEXT("Allow MCP Python Execution"), !Python.bValue, Python.bValue);
			}
			if (Writes.bIgnoredSharedValue)
			{
				WarnIgnored(TEXT("bAllowMcpWritesOutsideProject"), TEXT("Allow MCP Writes Outside Project"), !Writes.bValue, Writes.bValue);
			}
			if (Token.bIgnoredSharedValue)
			{
				WarnIgnored(TEXT("bRequireMcpBearerToken"), TEXT("Require MCP Bearer Token"), !Token.bValue, Token.bValue);
			}

			Result.bEnableMcpServer = Server.bValue;
			Result.bAllowMcpPythonExecution = Python.bValue;
			Result.bAllowMcpWritesOutsideProject = Writes.bValue;
			Result.bRequireMcpBearerToken = Token.bValue;

			// Show the value in effect in Project Settings, not the ignored project one
			if (Settings)
			{
				Settings->bEnableMcpServer = Result.bEnableMcpServer;
				Settings->bAllowMcpPythonExecution = Result.bAllowMcpPythonExecution;
				Settings->bAllowMcpWritesOutsideProject = Result.bAllowMcpWritesOutsideProject;
				Settings->bRequireMcpBearerToken = Result.bRequireMcpBearerToken;
			}
			return Result;
		}();
		return Switches;
	}
}

bool UN2CUserSettings::IsMcpServerEnabled()
{
	return N2CUserSettingsPrivate::GetEffectiveSwitches().bEnableMcpServer;
}

bool UN2CUserSettings::IsMcpPythonExecutionAllowed()
{
	return N2CUserSettingsPrivate::GetEffectiveSwitches().bAllowMcpPythonExecution;
}

bool UN2CUserSettings::AreMcpWritesOutsideProjectAllowed()
{
	return N2CUserSettingsPrivate::GetEffectiveSwitches().bAllowMcpWritesOutsideProject;
}

bool UN2CUserSettings::IsMcpBearerTokenRequired()
{
	return N2CUserSettingsPrivate::GetEffectiveSwitches().bRequireMcpBearerToken;
}

FN2CPerUserBool UN2CUserSettings::ResolvePerUserBool(const FConfigBranch* Branch, const FString& Section, FName Key, bool bCodeDefault)
{
	FN2CPerUserBool Result;
	Result.bValue = bCodeDefault;
	if (!Branch)
	{
		return Result;
	}

	// The user's own layers, in the order Unreal applies them: Saved/Config, then -ini: overrides on the command line
	const FConfigValue* UserValue = N2CUserSettingsPrivate::FindLastSetValue(Branch->SavedLayer, Section, Key);
	if (const FConfigValue* CommandLineValue = N2CUserSettingsPrivate::FindLastSetValue(Branch->CommandLineOverrides, Section, Key))
	{
		UserValue = CommandLineValue;
	}
	if (UserValue)
	{
		Result.bValue = FCString::ToBool(*UserValue->GetValue());
		Result.bFromUserLayer = true;
		return Result;
	}

	// Not set by the user: whatever the loaded config holds came from a shared layer (Base, Default, platform, plugin)
	if (const FConfigSection* LoadedSection = Branch->InMemoryFile.FindSection(Section))
	{
		if (const FConfigValue* LoadedValue = LoadedSection->Find(Key))
		{
			Result.bIgnoredSharedValue = FCString::ToBool(*LoadedValue->GetValue()) != bCodeDefault;
		}
	}
	return Result;
}
