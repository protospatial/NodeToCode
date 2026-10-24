// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettings.h"
#include "Core/N2CSecretStore.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Models/N2CStyle.h"
#include "Async/AsyncWork.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "Utils/N2CLogger.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "UnrealEdMisc.h"
#endif

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformApplicationMisc.h"
#endif

#if PLATFORM_MAC
#include "Mac/MacPlatformApplicationMisc.h"
#endif

#define LOCTEXT_NAMESPACE "NodeToCode"

UN2CSettings::UN2CSettings()
{
    FN2CLogger::Get().Log(TEXT("N2CSettings constructor called"), EN2CLogSeverity::Info);

    // Note: Pricing defaults are now centralized in FN2CLLMModelRegistry.
    // Users can still override pricing in config files; the getters fall back to registry.

    // Validate reference source paths on startup
    ValidateReferenceSourcePaths();
    
    // The key fields only mirror the process-wide secret store for the details panel; edits go back through it
    // (the stored keys only: a key from an environment variable is used for requests but never shown or saved)
    const FN2CSecretStore& Secrets = FN2CSecretStore::Get();
    OpenAI_API_Key_UI = Secrets.GetStoredApiKey(EN2CLLMProvider::OpenAI);
    Anthropic_API_Key_UI = Secrets.GetStoredApiKey(EN2CLLMProvider::Anthropic);
    Gemini_API_Key_UI = Secrets.GetStoredApiKey(EN2CLLMProvider::Gemini);
    DeepSeek_API_Key_UI = Secrets.GetStoredApiKey(EN2CLLMProvider::DeepSeek);
    RefreshApiKeySources();
    
    // Initialize token estimate
    EstimatedReferenceTokens = GetReferenceFilesTokenEstimate();

    // Set tooltip for ReferenceSourceFilePaths
    FProperty* ReferenceFilesProperty = GetClass()->FindPropertyByName(TEXT("ReferenceSourceFilePaths"));
    if (ReferenceFilesProperty)
    {
        ReferenceFilesProperty->SetMetaData(TEXT("ToolTip"), TEXT("Source files to include as context in LLM prompts"));
    }
}

void UN2CSettings::PostInitProperties()
{
    // Config is loaded by now (LoadConfig runs before PostInitProperties), so saved selections can be checked
    Super::PostInitProperties();
    MigrateRetiredModelSelections();
    MigrateSavedSettings();
    ReportRemovedSignInSelections();
}

void UN2CSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
    // The NodeToCode config branch is applied to the class default object by a config reload once the plugin's
    // config is mounted, which is after PostInitProperties; check the saved selections again
    Super::PostReloadConfig(PropertyThatWasLoaded);
    MigrateRetiredModelSelections();
    MigrateSavedSettings();
    ReportRemovedSignInSelections();
}

namespace
{
    /** Moves Selected to Default when the registry marks it retired or does not know it; records the change */
    template <typename TEnum>
    void MigrateIfRetired(TEnum& Selected, TEnum Default, const TCHAR* PropertyName, TArray<FString>& OutChanges)
    {
        const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
        const FN2CModelMetadata* Meta = Registry.GetModelMetadata(Selected);
        if (Meta && !Meta->bIsRetired)
        {
            return;
        }

        const UEnum* Enum = StaticEnum<TEnum>();
        const FString OldName = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Selected)) : FString();
        const FString OldId = Meta ? Meta->ModelId : FString(TEXT("unknown"));
        const FString NewName = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Default)) : FString();
        OutChanges.Add(FString::Printf(TEXT("%s=%s (%s%s) -> %s (%s)"),
            PropertyName, *OldName, *OldId,
            (Meta && !Meta->ShutdownDate.IsEmpty()) ? *FString::Printf(TEXT(", shut down %s"), *Meta->ShutdownDate) : TEXT(", shut down"),
            *NewName, *Registry.GetModelId(Default)));
        Selected = Default;
    }
}

int32 UN2CSettings::MigrateRetiredModelSelections()
{
    TArray<FString> Changes;
    MigrateIfRetired(OpenAI_Model, N2CDefaultModels::OpenAI, TEXT("OpenAI_Model"), Changes);
    MigrateIfRetired(AnthropicModel, N2CDefaultModels::Anthropic, TEXT("AnthropicModel"), Changes);
    MigrateIfRetired(Gemini_Model, N2CDefaultModels::Gemini, TEXT("Gemini_Model"), Changes);
    MigrateIfRetired(DeepSeekModel, N2CDefaultModels::DeepSeek, TEXT("DeepSeekModel"), Changes);

    if (Changes.Num() > 0)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("Saved model selection no longer served by its provider, using the current default instead: %s. NodeToCode saves the new selection to the project's config at editor start, so this is reported once; choose another model in Project Settings > Plugins > Node to Code."),
            *FString::Join(Changes, TEXT("; "))), TEXT("N2CSettings"));
    }
    return Changes.Num();
}

namespace
{
    /** Leaves the override off for a value at the old always-sent default, turns it on for any other value */
    template <typename TValue>
    void MigrateOllamaSampler(bool& bOverride, TValue Value, TValue OldDefault, const TCHAR* Name, TArray<FString>& OutChanges)
    {
        if (bOverride)
        {
            return;
        }
        if (FMath::IsNearlyEqual(static_cast<double>(Value), static_cast<double>(OldDefault), 1e-4))
        {
            return;
        }
        bOverride = true;
        OutChanges.Add(FString::Printf(TEXT("%s=%s kept as an override"), Name, *LexToString(Value)));
    }
}

int32 UN2CSettings::MigrateLegacyOllamaDefaults(FN2COllamaConfig& Config, TArray<FString>& OutChanges)
{
    const int32 Before = OutChanges.Num();
    // The old always-sent defaults; num_ctx 8192 cut a 32K server default down and truncated large prompts
    if (Config.NumCtx == 8192)
    {
        Config.NumCtx = 0;
        OutChanges.Add(TEXT("NumCtx 8192 -> 0 (server context length)"));
    }
    if (Config.NumPredict == 8192)
    {
        Config.NumPredict = 0;
        OutChanges.Add(TEXT("NumPredict 8192 -> 0 (server default)"));
    }
    if (!Config.bUseSystemPrompts)
    {
        Config.bUseSystemPrompts = true;
        OutChanges.Add(TEXT("bUseSystemPrompts False -> True"));
    }
    MigrateOllamaSampler(Config.bOverride_Temperature, Config.Temperature, 0.0f, TEXT("Temperature"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_TopP, Config.TopP, 0.5f, TEXT("TopP"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_TopK, Config.TopK, 40, TEXT("TopK"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_MinP, Config.MinP, 0.05f, TEXT("MinP"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_RepeatPenalty, Config.RepeatPenalty, 1.1f, TEXT("RepeatPenalty"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_Mirostat, Config.Mirostat, 0, TEXT("Mirostat"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_MirostatEta, Config.MirostatEta, 0.1f, TEXT("MirostatEta"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_MirostatTau, Config.MirostatTau, 5.0f, TEXT("MirostatTau"), OutChanges);
    MigrateOllamaSampler(Config.bOverride_Seed, Config.Seed, 0, TEXT("Seed"), OutChanges);
    return OutChanges.Num() - Before;
}

int32 UN2CSettings::MigrateLegacyLMStudioMaxTokens(const FString& ConfigFilename, int32& InOutLMStudioMaxOutputTokens, TArray<FString>& OutChanges)
{
    // PR #20's setting and its default; the default was sent on every request whether or not the user chose it
    static const TCHAR* const LegacyKey = TEXT("LMStudioMaxTokens");
    constexpr int32 LegacyDefault = 32768;

    int32 Legacy = 0;
    if (InOutLMStudioMaxOutputTokens > 0 || !GConfig || ConfigFilename.IsEmpty()
        || !GConfig->GetInt(*StaticClass()->GetPathName(), LegacyKey, Legacy, ConfigFilename))
    {
        return 0;
    }
    if (Legacy <= 0 || Legacy == LegacyDefault)
    {
        // Not a choice the user made: Auto (already 0) applies, and the stale key is dropped on the next save
        return 0;
    }
    InOutLMStudioMaxOutputTokens = Legacy;
    OutChanges.Add(FString::Printf(TEXT("LM Studio %s=%d -> Max Output Tokens %d"), LegacyKey, Legacy, Legacy));
    return 1;
}

int32 UN2CSettings::MigrateSavedSettings()
{
    TArray<FString> Changes;
    if (SettingsVersion < 1)
    {
        MigrateLegacyOllamaDefaults(OllamaConfig, Changes);
    }
    if (SettingsVersion < 2)
    {
        // Max Output Tokens keys are new, so they load as 0 = Auto; only PR #20's LM Studio key carries over
        MigrateLegacyLMStudioMaxTokens(GetClass()->GetConfigName(), LMStudioMaxOutputTokens, Changes);
    }

    if (Changes.Num() > 0)
    {
        FN2CLogger::Get().Log(FString::Printf(
            TEXT("Settings saved before NodeToCode 2.0 updated so the server and model defaults apply: %s. Saving Project Settings > Plugins > Node to Code stores this."),
            *FString::Join(Changes, TEXT("; "))), EN2CLogSeverity::Info, TEXT("N2CSettings"));
    }
    return Changes.Num();
}

namespace N2CLegacy12
{
    /** A price NodeToCode 1.2.x put in its pricing maps (constructor InitializePricing, per million tokens) */
    struct FPrice
    {
        const TCHAR* Model;
        float Input;
        float Output;
    };

    // origin/main (1.2.x maintenance line). The v1.2.3 tag fills a subset of these with the same prices, plus
    // Gemini_2_5_ProExp at 0/0, a value 2.0 no longer has (it cannot load, so it never reaches the map).
    const FPrice OpenAIPrices[] = {
        { TEXT("GPT4o_2024_08_06"), 2.5f, 10.0f }, { TEXT("GPT4o_Mini_2024_07_18"), 0.15f, 0.6f },
        { TEXT("GPT_4_1"), 2.0f, 8.0f }, { TEXT("GPT_o1"), 15.0f, 60.0f }, { TEXT("GPT_o3_mini"), 1.1f, 4.4f },
        { TEXT("GPT_o3"), 15.0f, 60.0f }, { TEXT("GPT_o4_mini"), 1.1f, 4.4f },
        { TEXT("GPT_o1_Preview"), 15.0f, 60.0f }, { TEXT("GPT_o1_Mini"), 1.1f, 4.4f },
    };
    const FPrice AnthropicPrices[] = {
        { TEXT("Claude4_Opus"), 15.0f, 75.0f }, { TEXT("Claude4_Sonnet"), 3.0f, 15.0f },
        { TEXT("Claude3_7_Sonnet"), 3.0f, 15.0f }, { TEXT("Claude3_5_Sonnet"), 3.0f, 15.0f },
        { TEXT("Claude3_5_Haiku"), 0.8f, 4.0f },
    };
    const FPrice GeminiPrices[] = {
        { TEXT("Gemini_2_5_Pro"), 1.25f, 10.0f }, { TEXT("Gemini_2_5_Flash"), 0.0f, 0.0f },
        { TEXT("Gemini_Flash_2_0"), 1.0f, 0.4f }, { TEXT("Gemini_Flash_Lite_2_0"), 0.075f, 0.3f },
        { TEXT("Gemini_1_5_Flash"), 0.075f, 0.3f }, { TEXT("Gemini_1_5_Pro"), 1.25f, 5.0f },
        { TEXT("Gemini_2_0_ProExp_02_05"), 0.0f, 0.0f }, { TEXT("Gemini_2_0_FlashThinkingExp"), 0.0f, 0.0f },
    };
    const FPrice DeepSeekPrices[] = {
        { TEXT("DeepSeek_R1"), 0.55f, 2.19f }, { TEXT("DeepSeek_V3"), 0.14f, 0.28f },
    };

    /** Model enum names only 1.2.x saved (all retired or removed in 2.0); one in the config is a trace of 1.2.x */
    const TCHAR* const ModelNames[] = {
        TEXT("Claude3_7_Sonnet"), TEXT("Claude3_5_Sonnet"), TEXT("Claude3_5_Haiku"), TEXT("Claude4_Opus"),
        TEXT("Claude4_Sonnet"), TEXT("GPT_o1"), TEXT("GPT_o3_mini"), TEXT("GPT_o1_Preview"), TEXT("GPT_o1_Mini"),
        TEXT("GPT_o4_mini"), TEXT("Gemini_2_5_ProExp"), TEXT("Gemini_2_5_Pro"), TEXT("Gemini_2_5_Flash"),
        TEXT("Gemini_Flash_2_0"), TEXT("Gemini_Flash_Lite_2_0"), TEXT("Gemini_1_5_Flash"), TEXT("Gemini_1_5_Pro"),
        TEXT("Gemini_2_0_ProExp_02_05"), TEXT("Gemini_2_0_FlashThinkingExp"),
    };

    /** The per-language theme maps 1.2.x saved; 2.0 has one CodeEditorTheme instead */
    const TCHAR* const ThemeKeys[] = {
        TEXT("CPPThemes"), TEXT("PythonThemes"), TEXT("JavaScriptThemes"), TEXT("CSharpThemes"),
        TEXT("SwiftThemes"), TEXT("PseudocodeThemes"),
    };

    /** What 1.2.x's ConfigureHttpTimeouts wrote to [HTTP] in DefaultEngine.ini (UHttpTimeoutConfig) */
    const TCHAR* const HttpSection = TEXT("HTTP");
    const TPair<const TCHAR*, float> HttpKeys[] = {
        { TEXT("HttpConnectionTimeout"), 300.0f }, { TEXT("HttpActivityTimeout"), 3600.0f },
    };

    /** The name part of a saved enum value ("EN2CGeminiModel::Gemini_2_5_Pro" or "Gemini_2_5_Pro") */
    FString ShortEnumName(FString Value)
    {
        Value.TrimStartAndEndInline();
        Value.TrimQuotesInline();
        int32 Separator = INDEX_NONE;
        if (Value.FindLastChar(TEXT(':'), Separator))
        {
            Value.RightChopInline(Separator + 1);
        }
        return Value;
    }

    bool IsLegacyModelName(const FString& Name)
    {
        for (const TCHAR* Model : ModelNames)
        {
            if (Name == Model)
            {
                return true;
            }
        }
        return false;
    }

    /** True when the engine can import this saved text into the property, as LoadConfig does */
    bool CanImport(const FProperty* Property, const FString& Text)
    {
        void* Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Value);
        FOutputDeviceNull Quiet;
        const bool bImported = Property->ImportText_Direct(*Text, Value, nullptr, PPF_None, &Quiet) != nullptr;
        Property->DestroyValue(Value);
        FMemory::Free(Value);
        return bImported;
    }

    bool IsKnownEnumName(const UEnum* Enum, const FString& Name)
    {
        return Enum->GetValueByNameString(Name) != INDEX_NONE
            || Enum->GetValueByNameString(FString::Printf(TEXT("%s::%s"), *Enum->GetName(), *Name)) != INDEX_NONE;
    }

    template <typename TEnum, typename TPricing>
    bool DropPrices(TMap<TEnum, TPricing>& Map, const TCHAR* PropertyName, TConstArrayView<FPrice> Legacy, TArray<FString>& OutDropped)
    {
        const UEnum* Enum = StaticEnum<TEnum>();
        const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
        bool bChanged = false;
        for (auto It = Map.CreateIterator(); It; ++It)
        {
            const FString Name = Enum ? Enum->GetNameStringByValue(static_cast<int64>(It.Key())) : FString();
            const FN2CModelMetadata* Meta = Registry.GetModelMetadata(It.Key());
            const FPrice* Old = Legacy.FindByPredicate([&Name](const FPrice& Price) { return Name == Price.Model; });

            const TCHAR* Reason = nullptr;
            if (!Meta)
            {
                Reason = TEXT("unknown model");
            }
            else if (Meta->bIsRetired)
            {
                Reason = TEXT("retired model");
            }
            else if (Old && FMath::IsNearlyEqual(It.Value().InputCost, Old->Input, 1e-4f)
                && FMath::IsNearlyEqual(It.Value().OutputCost, Old->Output, 1e-4f))
            {
                Reason = TEXT("NodeToCode 1.2.x built-in price");
            }
            if (!Reason)
            {
                continue;
            }
            OutDropped.Add(FString::Printf(TEXT("%s[%s] %s/%s (%s)"), PropertyName,
                Name.IsEmpty() ? TEXT("?") : *Name, *FString::SanitizeFloat(It.Value().InputCost),
                *FString::SanitizeFloat(It.Value().OutputCost), Reason));
            It.RemoveCurrent();
            bChanged = true;
        }
        return bChanged;
    }
}

FN2CLegacyMigrationTargets FN2CLegacyMigrationTargets::ForProject()
{
    FN2CLegacyMigrationTargets Targets;
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    Targets.ReadConfig = Settings->GetClass()->GetConfigName();
    Targets.WriteIni = Settings->GetDefaultConfigFilename();
    Targets.bReloadGConfigAfterWrite = true;
    Targets.EngineIni = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
    return Targets;
}

TArray<FName> UN2CSettings::DropLegacyPricing(TArray<FString>& OutDropped)
{
    TArray<FName> Changed;
    if (N2CLegacy12::DropPrices(OpenAIModelPricing, TEXT("OpenAIModelPricing"), N2CLegacy12::OpenAIPrices, OutDropped))
    {
        Changed.Add(GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAIModelPricing));
    }
    if (N2CLegacy12::DropPrices(AnthropicModelPricing, TEXT("AnthropicModelPricing"), N2CLegacy12::AnthropicPrices, OutDropped))
    {
        Changed.Add(GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModelPricing));
    }
    if (N2CLegacy12::DropPrices(GeminiModelPricing, TEXT("GeminiModelPricing"), N2CLegacy12::GeminiPrices, OutDropped))
    {
        Changed.Add(GET_MEMBER_NAME_CHECKED(UN2CSettings, GeminiModelPricing));
    }
    if (N2CLegacy12::DropPrices(DeepSeekModelPricing, TEXT("DeepSeekModelPricing"), N2CLegacy12::DeepSeekPrices, OutDropped))
    {
        Changed.Add(GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekModelPricing));
    }
    return Changed;
}

bool UN2CSettings::RemoveLegacyHttpTimeouts(const FString& EngineIniPath, TArray<FString>& OutRemoved)
{
    FString Text;
    if (EngineIniPath.IsEmpty() || !FFileHelper::LoadFileToString(Text, *EngineIniPath))
    {
        return true;
    }

    // Find the 1.2.x values in [HTTP], matching keys the way the engine's single-property update does ("Key=")
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines, false);
    const FString SectionHeader = FString::Printf(TEXT("[%s]"), N2CLegacy12::HttpSection);
    TArray<TPair<FString, FString>> ToRemove;
    bool bInSection = false;
    for (const FString& RawLine : Lines)
    {
        const FString Line = RawLine.TrimStartAndEnd();
        if (Line.StartsWith(TEXT("[")))
        {
            bInSection = Line == SectionHeader;
            continue;
        }
        if (!bInSection)
        {
            continue;
        }
        for (const TPair<const TCHAR*, float>& Key : N2CLegacy12::HttpKeys)
        {
            const FString Prefix = FString::Printf(TEXT("%s="), Key.Key);
            if (!Line.StartsWith(Prefix, ESearchCase::CaseSensitive))
            {
                continue;
            }
            const FString Value = Line.RightChop(Prefix.Len()).TrimStartAndEnd();
            if (Value.IsNumeric() && FMath::IsNearlyEqual(FCString::Atof(*Value), Key.Value, 1e-3f))
            {
                ToRemove.AddUnique(TPair<FString, FString>(Key.Key, Value));
            }
        }
    }
    if (ToRemove.Num() == 0)
    {
        return true;
    }
    if (IFileManager::Get().IsReadOnly(*EngineIniPath))
    {
        return false;
    }

    bool bWritten = true;
    for (const TPair<FString, FString>& Key : ToRemove)
    {
        // An empty config file has no value for the key, so the update removes its line from the file on disk
        FConfigFile NoValue;
        if (NoValue.UpdateSinglePropertyInSection(*EngineIniPath, *Key.Key, N2CLegacy12::HttpSection))
        {
            OutRemoved.Add(FString::Printf(TEXT("[%s] %s=%s"), N2CLegacy12::HttpSection, *Key.Key, *Key.Value));
        }
        else
        {
            bWritten = false;
        }
    }
    return bWritten;
}

bool UN2CSettings::WriteConfigProperties(const TArray<FName>& PropertyNames, const FString& IniPath, bool bReloadGConfig)
{
    if (PropertyNames.Num() == 0)
    {
        return true;
    }
    const FString FullPath = FPaths::ConvertRelativePathToFull(IniPath);
    if (IFileManager::Get().FileExists(*FullPath) && IFileManager::Get().IsReadOnly(*FullPath))
    {
        return false;
    }

    // As UObject::UpdateSinglePropertyInConfigFile: save the object into a sandbox config, then let the config
    // system rewrite only these keys in the file on disk, keeping every other line (stale 1.2.x keys included)
    FConfigCacheIni Sandbox(EConfigCacheType::Temporary);
    FConfigFile& Saved = Sandbox.Add(IniPath, FConfigFile());
    SaveConfig(CPF_Config, *IniPath, &Sandbox, false);
    TArray<FString> Sections;
    Saved.GetKeys(Sections);
    if (Sections.Num() == 0)
    {
        return false;
    }

    const FString ConfigName = GetClass()->ClassConfigName.ToString();
    if (bReloadGConfig)
    {
        // Keep changes GConfig has not written yet; the reload below replaces its copy with the file on disk
        GConfig->Flush(false, *ConfigName);
    }
    bool bWritten = true;
    for (const FName& Name : PropertyNames)
    {
        bWritten &= Saved.UpdateSinglePropertyInSection(*IniPath, *Name.ToString(), *Sections[0]);
    }
    if (bReloadGConfig)
    {
        FConfigContext Context = FConfigContext::ForceReloadIntoGConfig();
        Context.bWriteDestIni = false;
        Context.Load(*ConfigName);
    }
    return bWritten;
}

FN2CLegacyMigrationReport UN2CSettings::MigrateLegacy12Settings(const FN2CLegacyMigrationTargets& Targets)
{
    FN2CLegacyMigrationReport Report;
    const FString Section = GetClass()->GetPathName();
    auto ReadSaved = [&Targets, &Section](const TCHAR* Key, FString& OutValue)
    {
        return GConfig && !Targets.ReadConfig.IsEmpty() && GConfig->GetString(*Section, Key, OutValue, Targets.ReadConfig);
    };

    // Both normally ran when the config was loaded; they change nothing and log nothing the second time
    MigrateRetiredModelSelections();
    MigrateSavedSettings();

    // Every start: write back enum values that did not load as saved, so their warnings are not repeated
    TArray<FName> ToSave;
    bool bLegacyTrace = false;
    for (TFieldIterator<FProperty> It(GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (!Property->HasAnyPropertyFlags(CPF_Config))
        {
            continue;
        }
        FString Saved;
        if (Property->IsA<FArrayProperty>() || Property->ArrayDim != 1 || !ReadSaved(*Property->GetName(), Saved))
        {
            continue;
        }
        const UEnum* Enum = nullptr;
        int64 Value = 0;
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            Enum = EnumProperty->GetEnum();
            Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(this));
        }
        else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            Enum = ByteProperty->Enum;
            Value = ByteProperty->GetPropertyValue_InContainer(this);
        }
        if (!Enum)
        {
            // Any other value the engine could not import (a pricing map keyed by a model 2.0 does not have loses
            // the whole map) failed on load and would fail again on every start until rewritten
            if (!N2CLegacy12::CanImport(Property, Saved))
            {
                Report.ResetEnumValues.Add(FString::Printf(TEXT("%s (it names a value 2.0 does not have, so the whole entry was skipped)"), *Property->GetName()));
                ToSave.Add(Property->GetFName());
            }
            continue;
        }
        const FString SavedName = N2CLegacy12::ShortEnumName(Saved);
        const FString CurrentName = Enum->GetNameStringByValue(Value);
        if (SavedName.IsEmpty() || SavedName == CurrentName)
        {
            continue;
        }
        if (!N2CLegacy12::IsKnownEnumName(Enum, SavedName))
        {
            Report.ResetEnumValues.Add(FString::Printf(TEXT("%s=%s -> %s"), *Property->GetName(), *SavedName, *CurrentName));
        }
        bLegacyTrace |= N2CLegacy12::IsLegacyModelName(SavedName);
        ToSave.Add(Property->GetFName());
    }
    if (Report.ResetEnumValues.Num() > 0)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("Saved settings hold values NodeToCode 2.0 does not have, so these load as the 2.0 value instead: %s. The new value is saved to %s; choose another in Project Settings > Plugins > Node to Code."),
            *FString::Join(Report.ResetEnumValues, TEXT("; ")), *Targets.WriteIni), TEXT("N2CSettings"));
    }

    // Once: what NodeToCode 1.2.x left in this project
    bool bStamp = false;
    TArray<FString> ThemeKeys;
    if (SettingsVersion < 3)
    {
        Report.bRanLegacySteps = true;
        FString Unused;

        // The version 1 and 2 results exist only in memory until saved; once the version is stamped they no longer run
        if (SettingsVersion < 1 && ReadSaved(TEXT("OllamaConfig"), Unused))
        {
            ToSave.AddUnique(GET_MEMBER_NAME_CHECKED(UN2CSettings, OllamaConfig));
            bStamp = true;
        }
        if (SettingsVersion < 2 && LMStudioMaxOutputTokens > 0 && !ReadSaved(TEXT("LMStudioMaxOutputTokens"), Unused))
        {
            ToSave.AddUnique(GET_MEMBER_NAME_CHECKED(UN2CSettings, LMStudioMaxOutputTokens));
            bStamp = true;
        }

        for (const FName& Changed : DropLegacyPricing(Report.DroppedPrices))
        {
            ToSave.AddUnique(Changed);
            bStamp = true;
        }

        for (const TCHAR* Key : N2CLegacy12::ThemeKeys)
        {
            if (ReadSaved(Key, Unused))
            {
                Report.LegacyThemeKeys.Add(Key);
            }
        }

        if (!RemoveLegacyHttpTimeouts(Targets.EngineIni, Report.RemovedEngineKeys))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(
                TEXT("Could not remove the HTTP timeouts NodeToCode 1.2.x wrote to %s ([HTTP] HttpConnectionTimeout=300, HttpActivityTimeout=3600): the file is read-only. Check it out and restart the editor, or delete the two lines by hand."),
                *Targets.EngineIni), TEXT("N2CSettings"));
        }

        bLegacyTrace |= Report.LegacyThemeKeys.Num() > 0 || Report.RemovedEngineKeys.Num() > 0 || Report.DroppedPrices.Num() > 0;
        bStamp |= bLegacyTrace;

        if (Report.LegacyThemeKeys.Num() > 0)
        {
            FN2CLogger::Get().LogWarning(FString::Printf(
                TEXT("Code editor themes saved by NodeToCode 1.2.x (%s in %s) are not used by 2.0, which draws every language with one theme: Project Settings > Plugins > Node to Code > Theming > Code Editor Theme. If you changed theme colours in 1.2.x, set them there again; the old entries are left in the file so you can copy the colours, and can then be deleted. This is reported once."),
                *FString::Join(Report.LegacyThemeKeys, TEXT(", ")), *Targets.WriteIni), TEXT("N2CSettings"));
        }

        if (bLegacyTrace && !Targets.bThrottleCPUWhenNotForeground)
        {
            Report.bReportedThrottle = true;
            FN2CLogger::Get().LogWarning(
                TEXT("NodeToCode 1.2.x turned off Editor Preferences > General > Performance > Use Less CPU when in Background on every editor start. 2.0 no longer changes it and keeps the editor at full speed only while a translation runs. The option is still off; NodeToCode cannot tell whether 1.2.x or you turned it off, so it is left as it is. Turn it back on there if you want the editor to slow down in the background. This is reported once."),
                TEXT("N2CSettings"));
        }
    }

    const int32 PreviousVersion = SettingsVersion;
    if (bStamp)
    {
        SettingsVersion = CurrentSettingsVersion;
        ToSave.AddUnique(GET_MEMBER_NAME_CHECKED(UN2CSettings, SettingsVersion));
    }
    if (ToSave.Num() > 0)
    {
        if (WriteConfigProperties(ToSave, Targets.WriteIni, Targets.bReloadGConfigAfterWrite))
        {
            Report.bStampedVersion = bStamp;
            for (const FName& Name : ToSave)
            {
                Report.PersistedProperties.Add(Name.ToString());
            }
        }
        else
        {
            Report.bWriteFailed = true;
            SettingsVersion = PreviousVersion;
            FN2CLogger::Get().LogWarning(FString::Printf(
                TEXT("Could not save the updated NodeToCode settings (%s) to %s: the file is read-only or its folder is missing. Check it out or make it writable; until then the warnings above repeat on each start."),
                *FString::JoinBy(ToSave, TEXT(", "), [](const FName& Name) { return Name.ToString(); }), *Targets.WriteIni),
                TEXT("N2CSettings"));
        }
    }

    TArray<FString> Done;
    if (Report.PersistedProperties.Num() > 0)
    {
        Done.Add(FString::Printf(TEXT("saved %s to %s"), *FString::Join(Report.PersistedProperties, TEXT(", ")), *Targets.WriteIni));
    }
    if (Report.DroppedPrices.Num() > 0)
    {
        Done.Add(FString::Printf(TEXT("dropped pricing entries so the model registry's prices apply: %s"), *FString::Join(Report.DroppedPrices, TEXT(", "))));
    }
    if (Report.RemovedEngineKeys.Num() > 0)
    {
        Done.Add(FString::Printf(TEXT("removed %s from %s (2.0 sets HTTP timeouts per request; the engine defaults apply from the next start)"),
            *FString::Join(Report.RemovedEngineKeys, TEXT(", ")), *Targets.EngineIni));
    }
    if (Done.Num() > 0)
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("Updated settings left by an earlier NodeToCode version: %s."),
            *FString::Join(Done, TEXT("; "))), EN2CLogSeverity::Info, TEXT("N2CSettings"));
    }
    return Report;
}

TArray<FString> UN2CSettings::FindRemovedSignInSelections(const FString& ConfigFilename)
{
    // Config keys of the sign-in selectors removed in 2.0, whose value "OAuth" chose subscription or Google
    // account sign-in. Kept only to tell users who had them selected; nothing reads them otherwise.
    static const TCHAR* const RemovedKeys[][2] = {
        { TEXT("AnthropicAuthMethod"), TEXT("Anthropic") },
        { TEXT("GeminiAuthMethod"), TEXT("Gemini") },
    };

    TArray<FString> Providers;
    if (!GConfig || ConfigFilename.IsEmpty())
    {
        return Providers;
    }
    const FString Section = StaticClass()->GetPathName();
    for (const auto& Key : RemovedKeys)
    {
        FString Value;
        if (GConfig->GetString(*Section, Key[0], Value, ConfigFilename) && Value.TrimStartAndEnd().Equals(TEXT("OAuth"), ESearchCase::IgnoreCase))
        {
            Providers.Add(Key[1]);
        }
    }
    return Providers;
}

int32 UN2CSettings::ReportRemovedSignInSelections() const
{
    const TArray<FString> Providers = FindRemovedSignInSelections(GetClass()->GetConfigName());
    static bool bReported = false;
    if (Providers.Num() > 0 && !bReported)
    {
        bReported = true;
        FN2CLogger::Get().LogWarning(FString::Printf(
            TEXT("Claude Pro/Max and Google account sign-in were removed in NodeToCode 2.0. %s now use the API key set in Project Settings > Plugins > Node to Code > LLM Services; the old sign-in setting is ignored and is dropped the next time these settings are saved. See %s"),
            *FString::Join(Providers, TEXT(" and ")), N2CSignInRemoval::DocsUrl), TEXT("N2CSettings"));
    }
    return Providers.Num();
}

FText UN2CSettings::GetSectionText() const
{
    return LOCTEXT("SettingsSection", "Node to Code");
}

FString UN2CSettings::GetActiveApiKey() const
{
    if (Provider == EN2CLLMProvider::LMStudio)
    {
        return TEXT("lm-studio"); // LM Studio just requires a dummy API key for its OpenAI endpoint
    }
    return FN2CSecretStore::Get().GetApiKey(Provider);
}

FString UN2CSettings::GetActiveModel() const
{
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return FN2CLLMModelUtils::GetOpenAIModelValue(OpenAI_Model);
        case EN2CLLMProvider::Anthropic:
            return FN2CLLMModelUtils::GetAnthropicModelValue(AnthropicModel);
        case EN2CLLMProvider::Gemini:
            return FN2CLLMModelUtils::GetGeminiModelValue(Gemini_Model);
        case EN2CLLMProvider::DeepSeek:
            return FN2CLLMModelUtils::GetDeepSeekModelValue(DeepSeekModel);
        case EN2CLLMProvider::Ollama:
            return OllamaModel;
        case EN2CLLMProvider::LMStudio:
            return LMStudioModel;
        default:
            return FString();
    }
}

void UN2CSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
    Super::PreEditChange(PropertyAboutToChange);
    
    // Store the property being changed for use in PostEditChangeProperty
    LastEditedProperty = PropertyAboutToChange;

    // The values being edited are already migrated (MigrateSavedSettings ran on load), so the config saved after
    // this edit carries the current layout version and the migrations do not run on it again
    SettingsVersion = CurrentSettingsVersion;
}

bool UN2CSettings::IsColorProperty(const FProperty* Property) const
{
    if (!Property)
    {
        return false;
    }

    // Check if this is a color property within our FN2CCodeEditorColors struct
    const FStructProperty* StructProp = CastField<FStructProperty>(Property);
    if (StructProp && StructProp->Struct == TBaseStructure<FColor>::Get())
    {
        const FString PropertyPath = Property->GetPathName();
        return PropertyPath.Contains(TEXT("Colors"));
    }

    return false;
}

void UN2CSettings::CopyToClipboard(const FString& Text)
{
    #if PLATFORM_MAC
    
    FPlatformApplicationMisc::ClipboardCopy(*Text);
    FN2CLogger::Get().Log(TEXT("Copied text to clipboard"), EN2CLogSeverity::Info);
    return
    
    #endif
    
    FPlatformApplicationMisc::ClipboardCopy(*Text);
    FN2CLogger::Get().Log(TEXT("Copied text to clipboard"), EN2CLogSeverity::Info);                                                                                                          
}

void UN2CSettings::InitializePricing()
{
    // Pricing defaults are now centralized in FN2CLLMModelRegistry.
    // This method is kept for backward compatibility but does nothing.
    // Users can override pricing in config files; getters fall back to the registry.
}

void UN2CSettings::ValidateReferenceSourcePaths()
{
    TArray<FFilePath> ValidPaths;
    for (const FFilePath& Path : ReferenceSourceFilePaths)
    {
        if (FPaths::FileExists(Path.FilePath))
        {
            ValidPaths.Add(Path);
        }
        else
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Reference source file not found: %s"), *Path.FilePath));
        }
    }
    ReferenceSourceFilePaths = ValidPaths;
    
    // Validate custom translation output directory if set
    if (!CustomTranslationOutputDirectory.Path.IsEmpty())
    {
        if (!FPaths::DirectoryExists(CustomTranslationOutputDirectory.Path))
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Custom translation output directory does not exist: %s. Will attempt to create it when needed."), 
                *CustomTranslationOutputDirectory.Path));
        }
    }
}

void UN2CSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Update token estimate when reference files change
    const FProperty* Property = PropertyChangedEvent.Property;
    if (!Property)
    {
        return;
    }

    if (Property)
    {
        const FName PropertyName = Property->GetFName();
        
        // Handle API key changes
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAI_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::OpenAI, OpenAI_API_Key_UI);
            RefreshApiKeySources();
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Anthropic_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::Anthropic, Anthropic_API_Key_UI);
            RefreshApiKeySources();
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::Gemini, Gemini_API_Key_UI);
            RefreshApiKeySources();
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeek_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::DeepSeek, DeepSeek_API_Key_UI);
            RefreshApiKeySources();
            return;
        }

        // Update logger severity when MinSeverity changes
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, MinSeverity))
        {
            FN2CLogger::Get().SetMinSeverity(MinSeverity);
        }

        // Notify token estimation service when provider or model changes
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, OpenAI_Model) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, AnthropicModel) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_Model) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeekModel) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, OllamaModel) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, LMStudioModel))
        {
            NotifyModelSettingsChanged();
        }

        // Show restart notification when bEnableDynamicToolDiscovery changes
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, bEnableDynamicToolDiscovery))
        {
            ShowRestartEditorNotification();
        }

        // Show restart notification when bEnablePythonScriptOnlyMode changes
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, bEnablePythonScriptOnlyMode))
        {
            ShowRestartEditorNotification();
        }

        // Check for both array changes and changes to FilePath within the struct                                                                                                                         
        const bool bIsFilePathChange = PropertyName == GET_MEMBER_NAME_CHECKED(FFilePath, FilePath);                                                                                              
        const bool bIsArrayChange = PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, ReferenceSourceFilePaths);

        if (bIsFilePathChange || bIsArrayChange)
        {
            EstimatedReferenceTokens = GetReferenceFilesTokenEstimate();
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Estimated reference file tokens: %d"), EstimatedReferenceTokens),
                EN2CLogSeverity::Info);

            // UpdateSinglePropertyInConfigFile() does not support FFilePath arrays as a property
            if (bIsArrayChange) { return; }

        }

        // Reinitialize code editor style when theme colors change
        const FProperty* MemberProp = PropertyChangedEvent.MemberProperty;
        if (MemberProp)
        {
            const FName MemberName = MemberProp->GetFName();
            if (MemberName == GET_MEMBER_NAME_CHECKED(UN2CSettings, CodeEditorTheme))
            {
                FN2CCodeEditorStyle::Reinitialize();
            }

            if (MemberName == GET_MEMBER_NAME_CHECKED(UN2CSettings, UIColors))
            {
                N2CStyle::UpdateButtonStyles();
                N2CStyle::UpdateBorderBrushes();
                N2CStyle::UpdateComboBoxStyle();
                N2CStyle::UpdateWidgetStyles();
            }
        }
    }
    const FString ConfigPath = GetDefaultConfigFilename();
    
    // Add debug logging for config save
    UpdateSinglePropertyInConfigFile(Property, ConfigPath);

    // PreEditChange stamped the current layout version on the values being saved; save it with them so the
    // load-time migrations do not run on this config again
    if (Property && Property->GetFName() != GET_MEMBER_NAME_CHECKED(UN2CSettings, SettingsVersion))
    {
        if (const FProperty* VersionProperty = GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CSettings, SettingsVersion)))
        {
            UpdateSinglePropertyInConfigFile(VersionProperty, ConfigPath);
        }
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Saving settings to: %s"), *ConfigPath),
        EN2CLogSeverity::Info
    );
}

FString UN2CSettings::DescribeApiKeySource(EN2CLLMProvider InProvider)
{
    const TArray<FString> Variables = FN2CSecretStore::GetEnvironmentVariableNames(InProvider);
    FString VariableName;
    switch (FN2CSecretStore::Get().GetApiKeySource(InProvider, &VariableName))
    {
        case EN2CApiKeySource::Store:
            return TEXT("Saved key (the API Key field)");
        case EN2CApiKeySource::Environment:
            return FString::Printf(TEXT("Environment variable %s (value not shown). A key entered in the API Key field takes priority."),
                *VariableName);
        case EN2CApiKeySource::None:
        default:
            return FString::Printf(TEXT("No key. Enter one in the API Key field, or set %s in the editor's environment."),
                *FString::Join(Variables, TEXT(" or ")));
    }
}

void UN2CSettings::RefreshApiKeySources()
{
    OpenAI_API_Key_Source = DescribeApiKeySource(EN2CLLMProvider::OpenAI);
    Anthropic_API_Key_Source = DescribeApiKeySource(EN2CLLMProvider::Anthropic);
    Gemini_API_Key_Source = DescribeApiKeySource(EN2CLLMProvider::Gemini);
    DeepSeek_API_Key_Source = DescribeApiKeySource(EN2CLLMProvider::DeepSeek);
}

FString UN2CSettings::GetActiveApiKeyForProvider(EN2CLLMProvider InProvider) const
{
    if (InProvider == EN2CLLMProvider::LMStudio)
    {
        return TEXT("lm-studio"); // LM Studio just requires a dummy API key for its OpenAI endpoint
    }
    return FN2CSecretStore::Get().GetApiKey(InProvider);
}

FString UN2CSettings::GetActiveModelForProvider(EN2CLLMProvider InProvider) const
{
    switch (InProvider)
    {
        case EN2CLLMProvider::OpenAI:
            return FN2CLLMModelUtils::GetOpenAIModelValue(OpenAI_Model);
        case EN2CLLMProvider::Anthropic:
            return FN2CLLMModelUtils::GetAnthropicModelValue(AnthropicModel);
        case EN2CLLMProvider::Gemini:
            return FN2CLLMModelUtils::GetGeminiModelValue(Gemini_Model);
        case EN2CLLMProvider::DeepSeek:
            return FN2CLLMModelUtils::GetDeepSeekModelValue(DeepSeekModel);
        case EN2CLLMProvider::Ollama:
            return OllamaModel;
        case EN2CLLMProvider::LMStudio:
            return LMStudioModel;
        default:
            return FString();
    }
}

FString UN2CSettings::GetActiveReasoningEffortForProvider(EN2CLLMProvider InProvider) const
{
    // Only DeepSeek switches thinking per request on a shared model ID
    if (InProvider == EN2CLLMProvider::DeepSeek)
    {
        if (const FN2CModelMetadata* Meta = FN2CLLMModelRegistry::Get().GetModelMetadata(DeepSeekModel))
        {
            return Meta->ReasoningEffort;
        }
    }
    return FString();
}

int32 UN2CSettings::GetMaxOutputTokensForProvider(EN2CLLMProvider InProvider) const
{
    switch (InProvider)
    {
        case EN2CLLMProvider::Anthropic:
            return FMath::Max(AnthropicMaxOutputTokens, 0);
        case EN2CLLMProvider::OpenAI:
            return FMath::Max(OpenAIMaxOutputTokens, 0);
        case EN2CLLMProvider::Gemini:
            return FMath::Max(GeminiMaxOutputTokens, 0);
        case EN2CLLMProvider::DeepSeek:
            return FMath::Max(DeepSeekMaxOutputTokens, 0);
        case EN2CLLMProvider::LMStudio:
            return FMath::Max(LMStudioMaxOutputTokens, 0);
        default:
            // Ollama: OllamaConfig.NumPredict
            return 0;
    }
}

void UN2CSettings::ResolveRequestModel(EN2CLLMProvider InProvider, const FString& ModelIdOverride, FN2CLLMConfig& InOutConfig) const
{
    InOutConfig.MaxOutputTokens = GetMaxOutputTokensForProvider(InProvider);
    const FString SelectedModel = GetActiveModelForProvider(InProvider);
    if (ModelIdOverride.IsEmpty() || ModelIdOverride == SelectedModel)
    {
        InOutConfig.Model = SelectedModel;
        InOutConfig.ReasoningEffort = GetActiveReasoningEffortForProvider(InProvider);
    }
    else
    {
        InOutConfig.Model = ModelIdOverride;
        InOutConfig.ReasoningEffort.Reset();
    }
}

#undef LOCTEXT_NAMESPACE
const FN2CCodeEditorColors* UN2CSettings::GetThemeColors(EN2CCodeLanguage Language, const FName& ThemeName) const
{
    return CodeEditorTheme.Themes.Find(FName("Unreal Engine"));
}

#undef LOCTEXT_NAMESPACE

#if WITH_EDITOR
void UN2CSettings::ShowRestartEditorNotification()
{
    #define LOCTEXT_NAMESPACE "NodeToCode"
    
    // Don't show multiple notifications
    TSharedPtr<SNotificationItem> NotificationPin = RestartNotificationPtr.Pin();
    if (NotificationPin.IsValid())
    {
        return;
    }
    
    FNotificationInfo Info(LOCTEXT("MCPServerRestartRequired", "MCP Server settings changed. Restart the editor to apply changes."));
    
    // Add restart button
    Info.ButtonDetails.Add(FNotificationButtonInfo(
        LOCTEXT("RestartNow", "Restart Now"),
        LOCTEXT("RestartNowToolTip", "Restart the editor now to apply the MCP server changes."),
        FSimpleDelegate::CreateLambda([]()
        {
            // Request editor restart
            FUnrealEdMisc::Get().RestartEditor(false);
        })
    ));
    
    // Add dismiss button
    Info.ButtonDetails.Add(FNotificationButtonInfo(
        LOCTEXT("RestartLater", "Restart Later"),
        LOCTEXT("RestartLaterToolTip", "Dismiss this notification. The MCP server changes will be applied on next editor restart."),
        FSimpleDelegate::CreateLambda([this]()
        {
            TSharedPtr<SNotificationItem> Notification = RestartNotificationPtr.Pin();
            if (Notification.IsValid())
            {
                Notification->SetCompletionState(SNotificationItem::CS_None);
                Notification->ExpireAndFadeout();
                RestartNotificationPtr.Reset();
            }
        })
    ));
    
    // Configure notification
    Info.bFireAndForget = false;
    Info.WidthOverride = 400.0f;
    Info.bUseLargeFont = false;
    Info.bUseThrobber = false;
    Info.bUseSuccessFailIcons = false;
    Info.ExpireDuration = 0.0f; // Don't auto-expire
    
    // Show notification
    RestartNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
    TSharedPtr<SNotificationItem> Notification = RestartNotificationPtr.Pin();
    if (Notification.IsValid())
    {
        Notification->SetCompletionState(SNotificationItem::CS_Pending);
    }
    
    #undef LOCTEXT_NAMESPACE
}
#endif

void UN2CSettings::NotifyModelSettingsChanged()
{
    FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
    Service.RefreshModelInfo();
    Service.OnModelChanged.Broadcast();
}
