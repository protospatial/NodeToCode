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
    const FN2CSecretStore& Secrets = FN2CSecretStore::Get();
    OpenAI_API_Key_UI = Secrets.GetApiKey(EN2CLLMProvider::OpenAI);
    Anthropic_API_Key_UI = Secrets.GetApiKey(EN2CLLMProvider::Anthropic);
    Gemini_API_Key_UI = Secrets.GetApiKey(EN2CLLMProvider::Gemini);
    DeepSeek_API_Key_UI = Secrets.GetApiKey(EN2CLLMProvider::DeepSeek);
    
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
            TEXT("Saved model selection no longer served by its provider, using the current default instead: %s. Choose a model in Project Settings > Plugins > Node to Code to save the choice; until then this repeats on each start."),
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
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Anthropic_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::Anthropic, Anthropic_API_Key_UI);
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::Gemini, Gemini_API_Key_UI);
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeek_API_Key_UI))
        {
            FN2CSecretStore::Get().SetApiKey(EN2CLLMProvider::DeepSeek, DeepSeek_API_Key_UI);
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

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Saving settings to: %s"), *ConfigPath),
        EN2CLogSeverity::Info
    );
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
