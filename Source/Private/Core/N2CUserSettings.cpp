// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CUserSettings.h"

#include "Core/N2CSettings.h"
#include "Core/Style/N2CStyle.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
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
			TEXT("Set it in Editor Preferences > Plugins > Node to Code (User); if the project file sets the same value, remove it there first, ")
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

FN2CUserSettingsUpgradeSources FN2CUserSettingsUpgradeSources::ForProject()
{
	FN2CUserSettingsUpgradeSources Sources;
	// The NodeToCode branch merges Config/DefaultNodeToCode.ini and the Saved/Config layer (which wins)
	Sources.ReadConfigs.Add(UN2CSettings::StaticClass()->GetConfigName());
	Sources.UserConfig = UN2CUserSettings::StaticClass()->GetConfigName();
	Sources.ProjectDir = FPaths::ProjectDir();
	return Sources;
}

TConstArrayView<FName> UN2CUserSettings::GetMovedPropertyNames()
{
	static const FName Names[] = {
		GET_MEMBER_NAME_CHECKED(UN2CUserSettings, Provider),
		GET_MEMBER_NAME_CHECKED(UN2CUserSettings, LMStudioEndpoint),
		GET_MEMBER_NAME_CHECKED(UN2CUserSettings, McpServerPort),
		GET_MEMBER_NAME_CHECKED(UN2CUserSettings, ReferenceSourceFilePaths),
		GET_MEMBER_NAME_CHECKED(UN2CUserSettings, CustomTranslationOutputDirectory),
		GET_MEMBER_NAME_CHECKED(UN2CUserSettings, UIColors),
	};
	return Names;
}

namespace N2CUserSettingsPrivate
{
	/** Text Muted before card 0271 raised it to 148 for contrast; a saved palette still holding it gets the new default */
	const FColor OldTextMuted(107, 107, 107, 255);

	/** A property value imported from config text into its own storage, so a failed import changes nothing */
	struct FScratchValue
	{
		const FProperty* Property;
		void* Data;

		explicit FScratchValue(const FProperty* InProperty)
			: Property(InProperty)
			, Data(FMemory::Malloc(InProperty->GetSize(), InProperty->GetMinAlignment()))
		{
			Property->InitializeValue(Data);
		}

		~FScratchValue()
		{
			Property->DestroyValue(Data);
			FMemory::Free(Data);
		}

		bool Import(const FString& Text, UObject* Owner)
		{
			FOutputDeviceNull Errors;
			// As LoadConfig imports a config value (an unquoted string keeps its ':' and spaces)
			return Property->ImportText_Direct(*Text, Data, Owner, PPF_SerializedAsImportText, &Errors) != nullptr;
		}
	};

	/** The config file on disk behind a GConfig name, for log messages */
	FString DescribeConfig(const FString& ConfigName)
	{
		if (GConfig)
		{
			if (const FConfigBranch* Branch = GConfig->FindBranch(FName(*ConfigName), ConfigName))
			{
				if (!Branch->IniPath.IsEmpty())
				{
					return FPaths::ConvertRelativePathToFull(Branch->IniPath);
				}
			}
		}
		return ConfigName;
	}
}

FN2CUserSettingsUpgradeReport UN2CUserSettings::UpgradeFromProjectSettings(const FN2CUserSettingsUpgradeSources& Sources)
{
	using namespace N2CUserSettingsPrivate;

	FN2CUserSettingsUpgradeReport Report;
	if (!GConfig || SettingsVersion >= CurrentSettingsVersion)
	{
		return Report;
	}
	Report.bRan = true;

	const FString ProjectSection = UN2CSettings::StaticClass()->GetPathName();
	const FString UserSection = GetClass()->GetPathName();
	const FString UserConfig = Sources.UserConfig.IsEmpty() ? GetClass()->GetConfigName() : Sources.UserConfig;
	TArray<FName> ToSave;

	for (const FName& Name : GetMovedPropertyNames())
	{
		const FProperty* Property = GetClass()->FindPropertyByName(Name);
		if (!Property)
		{
			continue;
		}
		const FString Key = Name.ToString();

		// Reference files: the project keeps its own files; entries outside the project are this developer's
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TArray<FString> Entries;
			for (const FString& ReadConfig : Sources.ReadConfigs)
			{
				TArray<FString> Values;
				GConfig->GetArray(*ProjectSection, *Key, Values, ReadConfig);
				for (const FString& Value : Values)
				{
					Entries.AddUnique(Value);
				}
			}
			if (Entries.Num() == 0)
			{
				continue;
			}
			Report.LegacyKeys.Add(Key);
			bool bAdded = false;
			for (const FString& Entry : Entries)
			{
				FScratchValue Parsed(ArrayProperty->Inner);
				if (!Parsed.Import(Entry, this))
				{
					Report.Unreadable.Add(FString::Printf(TEXT("%s=%s"), *Key, *Entry));
					continue;
				}
				const FString Absolute = ResolveReferencePath(static_cast<const FFilePath*>(Parsed.Data)->FilePath, Sources.ProjectDir);
				if (IsInsideProject(Absolute, Sources.ProjectDir))
				{
					continue;
				}
				if (!FPaths::FileExists(Absolute))
				{
					// Most likely another developer's file: it stays in the project file, nothing is lost
					Report.SkippedReferenceFiles.Add(Absolute);
					continue;
				}
				const bool bKnown = ReferenceSourceFilePaths.ContainsByPredicate([&Absolute, &Sources](const FFilePath& Each)
					{ return ResolveReferencePath(Each.FilePath, Sources.ProjectDir) == Absolute; });
				if (!bKnown)
				{
					FFilePath Personal;
					Personal.FilePath = Absolute;
					ReferenceSourceFilePaths.Add(Personal);
					Report.CopiedReferenceFiles.Add(Absolute);
					bAdded = true;
				}
			}
			if (bAdded)
			{
				ToSave.Add(Name);
			}
			continue;
		}

		// One value: the last layer that sets it wins, as when the project settings loaded it
		FString Value;
		bool bFound = false;
		for (const FString& ReadConfig : Sources.ReadConfigs)
		{
			FString LayerValue;
			if (GConfig->GetString(*ProjectSection, *Key, LayerValue, ReadConfig))
			{
				Value = LayerValue;
				bFound = true;
			}
		}
		if (!bFound)
		{
			continue;
		}
		Report.LegacyKeys.Add(Key);

		FString UserValue;
		if (GConfig->GetString(*UserSection, *Key, UserValue, UserConfig))
		{
			Report.KeptUserValues.Add(Key);
			continue;
		}

		FScratchValue Imported(Property);
		if (!Imported.Import(Value, this))
		{
			Report.Unreadable.Add(FString::Printf(TEXT("%s=%s"), *Key, *Value));
			continue;
		}
		if (Name == GET_MEMBER_NAME_CHECKED(UN2CUserSettings, UIColors))
		{
			// Project Settings saved the whole palette on any color edit, so a 107 grey is the old default, not a choice
			FN2CUIColors& Colors = *static_cast<FN2CUIColors*>(Imported.Data);
			const FN2CUIColors Defaults;
			if (Colors.TextMuted == OldTextMuted)
			{
				Colors.TextMuted = Defaults.TextMuted;
				Report.bUpdatedTextMuted = true;
			}
			if (Property->Identical(Imported.Data, &Defaults))
			{
				// Nothing of the user's own: leave the palette to follow the defaults
				continue;
			}
			Report.Copied.Add(Key);
		}
		else
		{
			FString Exported;
			Property->ExportTextItem_Direct(Exported, Imported.Data, nullptr, this, PPF_SerializedAsImportText);
			Report.Copied.Add(FString::Printf(TEXT("%s=%s"), *Key, *Exported));
		}
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(this), Imported.Data);
		ToSave.Add(Name);
	}

	const int32 PreviousVersion = SettingsVersion;
	SettingsVersion = CurrentSettingsVersion;
	ToSave.Add(GET_MEMBER_NAME_CHECKED(UN2CUserSettings, SettingsVersion));
	if (SaveToUserConfig(ToSave, UserConfig))
	{
		Report.bStamped = true;
	}
	else
	{
		Report.bWriteFailed = true;
		SettingsVersion = PreviousVersion;
	}

	const FString UserFile = DescribeConfig(UserConfig);
	if (Report.bWriteFailed)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(
			TEXT("Could not save your NodeToCode settings to %s; the copy from the project config is tried again at the next start."),
			*UserFile), TEXT("N2CUserSettings"));
	}
	TArray<FString> Moved = Report.Copied;
	if (Report.CopiedReferenceFiles.Num() > 0)
	{
		Moved.Add(FString::Printf(TEXT("My Reference Source Files += %s"), *FString::Join(Report.CopiedReferenceFiles, TEXT(", "))));
	}
	if (Moved.Num() > 0 || Report.LegacyKeys.Num() > 0)
	{
		const FString Kept = Report.KeptUserValues.Num() > 0
			? FString::Printf(TEXT(" Your own values of %s were already set and are kept."), *FString::Join(Report.KeptUserValues, TEXT(", "))) : FString();
		const FString TextMuted = Report.bUpdatedTextMuted
			? FString(TEXT(" UI Colors: Text Muted moves from the old 107 grey to the higher-contrast 148 default.")) : FString();
		FN2CLogger::Get().Log(FString::Printf(
			TEXT("NodeToCode settings version %d: your own settings now live in %s (Editor Preferences > Plugins > Node to Code (User)). Copied from the project config: %s.%s%s ")
			TEXT("These keys of [%s] in the project's config (Config/DefaultNodeToCode.ini and Saved/Config/<Platform>Editor/NodeToCode.ini) are no longer read: %s. ")
			TEXT("Nothing was removed there: each developer's editor copies them once, so delete them when everyone on the project has upgraded."),
			CurrentSettingsVersion, *UserFile, Moved.Num() > 0 ? *FString::Join(Moved, TEXT(", ")) : TEXT("nothing"), *Kept, *TextMuted,
			*ProjectSection, *FString::Join(Report.LegacyKeys, TEXT(", "))), EN2CLogSeverity::Info, TEXT("N2CUserSettings"));
	}
	if (Report.Unreadable.Num() > 0)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(
			TEXT("These saved NodeToCode values name something this version does not have, so the default applies: %s. They are left in the project's config; choose the setting again in Editor Preferences > Plugins > Node to Code (User)."),
			*FString::Join(Report.Unreadable, TEXT("; "))), TEXT("N2CUserSettings"));
	}
	if (Report.SkippedReferenceFiles.Num() > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(
			TEXT("Reference source files outside the project that do not exist on this machine were not added to your own list (they stay in the project's config): %s"),
			*FString::Join(Report.SkippedReferenceFiles, TEXT(", "))), EN2CLogSeverity::Info, TEXT("N2CUserSettings"));
	}
	return Report;
}

bool UN2CUserSettings::SaveToUserConfig(TConstArrayView<FName> PropertyNames, const FString& InUserConfig)
{
	if (!GConfig)
	{
		return false;
	}
	const FString UserConfig = InUserConfig.IsEmpty() ? GetClass()->GetConfigName() : InUserConfig;
	const FString Section = GetClass()->GetPathName();
	bool bAllSaved = true;
	int32 Written = 0;
	for (const FName& Name : PropertyNames)
	{
		const FProperty* Property = Name.IsNone() ? nullptr : GetClass()->FindPropertyByName(Name);
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Config) || Property->ArrayDim != 1)
		{
			bAllSaved = false;
			continue;
		}
		// Key by key, as SaveConfig writes them, without writing the properties the user never set
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(this));
			TArray<FString> Values;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				FString Value;
				ArrayProperty->Inner->ExportTextItem_Direct(Value, Helper.GetRawPtr(Index), nullptr, this, PPF_SerializedAsImportText);
				Values.Add(Value);
			}
			GConfig->SetArray(*Section, *Name.ToString(), Values, UserConfig);
		}
		else
		{
			FString Value;
			Property->ExportText_InContainer(0, Value, this, nullptr, this, PPF_SerializedAsImportText);
			GConfig->SetString(*Section, *Name.ToString(), *Value, UserConfig);
		}
		++Written;
	}
	if (Written > 0)
	{
		GConfig->Flush(false, UserConfig);
	}
	return bAllSaved;
}

FString UN2CUserSettings::ResolveReferencePath(const FString& Path, const FString& ProjectDir)
{
	const FString Trimmed = Path.TrimStartAndEnd().TrimQuotes();
	FString Result;
	if (FPaths::IsRelative(Trimmed))
	{
		const FString Root = FPaths::ConvertRelativePathToFull(ProjectDir.IsEmpty() ? FPaths::ProjectDir() : ProjectDir);
		Result = FPaths::ConvertRelativePathToFull(Root / Trimmed);
	}
	else
	{
		Result = FPaths::ConvertRelativePathToFull(Trimmed);
	}
	FPaths::NormalizeFilename(Result);
	FPaths::CollapseRelativeDirectories(Result);
	return Result;
}

bool UN2CUserSettings::IsInsideProject(const FString& AbsolutePath, const FString& ProjectDir)
{
	FString Root = FPaths::ConvertRelativePathToFull(ProjectDir.IsEmpty() ? FPaths::ProjectDir() : ProjectDir);
	FPaths::NormalizeDirectoryName(Root);
	FPaths::CollapseRelativeDirectories(Root);
	return FPaths::IsUnderDirectory(AbsolutePath, Root);
}

TArray<FString> UN2CUserSettings::GetReferenceSourceFiles()
{
	static const TArray<FFilePath> None;
	const UN2CSettings* ProjectSettings = GetDefault<UN2CSettings>();
	const UN2CUserSettings* UserSettings = GetDefault<UN2CUserSettings>();
	return CollectReferenceSourceFiles(ProjectSettings ? ProjectSettings->ReferenceSourceFilePaths : None,
		UserSettings ? UserSettings->ReferenceSourceFilePaths : None);
}

TArray<FString> UN2CUserSettings::CollectReferenceSourceFiles(const TArray<FFilePath>& ProjectList, const TArray<FFilePath>& UserList)
{
	TArray<FString> Files;
	auto AddExisting = [&Files](const TArray<FFilePath>& List)
	{
		for (const FFilePath& Entry : List)
		{
			if (Entry.FilePath.TrimStartAndEnd().IsEmpty())
			{
				continue;
			}
			const FString Absolute = ResolveReferencePath(Entry.FilePath);
			if (FPaths::FileExists(Absolute))
			{
				Files.AddUnique(Absolute);
			}
		}
	};
	AddExisting(ProjectList);
	AddExisting(UserList);
	return Files;
}

#if WITH_EDITOR
FText UN2CUserSettings::GetSectionDescription() const
{
	return NSLOCTEXT("NodeToCode", "UserSettingsSection",
		"Your own Node to Code settings: provider, LM Studio endpoint, MCP server port and switches, window colors, your reference files and translation output folder. They are saved in your user settings for this project, not in the project's Config folder, so they are never committed. The project's shared settings are in Project Settings > Plugins > Node to Code.");
}

void UN2CUserSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A drag (colour picker, slider) sends Interactive changes while moving; act once, when the value is committed
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();

	// Editing these settings stamps the layout version, which is saved with them
	SettingsVersion = FMath::Max(SettingsVersion, CurrentSettingsVersion);

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CUserSettings, Provider))
	{
		GetMutableDefault<UN2CSettings>()->NotifyModelSettingsChanged();
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CUserSettings, McpServerPort))
	{
		// Restart the MCP server on the new port; if no port in the range is free, its notification says so
		FN2CMcpHttpServerManager::Get().HandlePortSettingsChanged();
	}
	if (MemberName == GET_MEMBER_NAME_CHECKED(UN2CUserSettings, ReferenceSourceFilePaths))
	{
		UN2CSettings* ProjectSettings = GetMutableDefault<UN2CSettings>();
		ProjectSettings->EstimatedReferenceTokens = ProjectSettings->GetReferenceFilesTokenEstimate();
		ProjectSettings->NotifyModelSettingsChanged();
	}
	if (MemberName == GET_MEMBER_NAME_CHECKED(UN2CUserSettings, UIColors))
	{
		N2CStyle::UpdateButtonStyles();
		N2CStyle::UpdateBorderBrushes();
		N2CStyle::UpdateComboBoxStyle();
		N2CStyle::UpdateWidgetStyles();
	}
}
#endif
