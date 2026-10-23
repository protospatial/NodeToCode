// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettings.h"
#include "Core/N2CUserSecrets.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Auth/N2CAnthropicOAuthTokenManager.h"
#include "Auth/N2CGoogleOAuthTokenManager.h"
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
    
    if (!UserSecrets)
    {
        UserSecrets = NewObject<UN2CUserSecrets>();
        UserSecrets->LoadSecrets();
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Loaded user secrets from: %s"), *UN2CUserSecrets::GetSecretsFilePath()),
            EN2CLogSeverity::Info);
    }

    // Initialize API Keys
    OpenAI_API_Key_UI = UserSecrets->OpenAI_API_Key;
    Anthropic_API_Key_UI = UserSecrets->Anthropic_API_Key;
    Gemini_API_Key_UI = UserSecrets->Gemini_API_Key;
    DeepSeek_API_Key_UI = UserSecrets->DeepSeek_API_Key;
    
    // Initialize token estimate
    EstimatedReferenceTokens = GetReferenceFilesTokenEstimate();

    // Initialize OAuth status
    RefreshOAuthStatus();
    RefreshGeminiOAuthStatus();

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
}

void UN2CSettings::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
    // The NodeToCode config branch is applied to the class default object by a config reload once the plugin's
    // config is mounted, which is after PostInitProperties; check the saved selections again
    Super::PostReloadConfig(PropertyThatWasLoaded);
    MigrateRetiredModelSelections();
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

FText UN2CSettings::GetSectionText() const
{
    return LOCTEXT("SettingsSection", "Node to Code");
}

FString UN2CSettings::GetActiveApiKey() const
{
    if (!UserSecrets)
    {
        // Create and load secrets if not already done
        UserSecrets = NewObject<UN2CUserSecrets>();
        UserSecrets->LoadSecrets();
    }

    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return UserSecrets->OpenAI_API_Key;
        case EN2CLLMProvider::Anthropic:
            return UserSecrets->Anthropic_API_Key;
        case EN2CLLMProvider::Gemini:
            return UserSecrets->Gemini_API_Key;
        case EN2CLLMProvider::DeepSeek:
            return UserSecrets->DeepSeek_API_Key;
        case EN2CLLMProvider::LMStudio:
            return "lm-studio"; // LM Studio just requires a dummy API key for its OpenAI endpoint
        default:
            return FString();
    }
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
            if (!UserSecrets)
            {
                UserSecrets = NewObject<UN2CUserSecrets>();
                UserSecrets->LoadSecrets();
            }
            UserSecrets->OpenAI_API_Key = OpenAI_API_Key_UI;
            UserSecrets->SaveSecrets();
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Anthropic_API_Key_UI))
        {
            if (!UserSecrets)
            {
                UserSecrets = NewObject<UN2CUserSecrets>();
                UserSecrets->LoadSecrets();
            }
            UserSecrets->Anthropic_API_Key = Anthropic_API_Key_UI;
            UserSecrets->SaveSecrets();
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_API_Key_UI))
        {
            if (!UserSecrets)
            {
                UserSecrets = NewObject<UN2CUserSecrets>();
                UserSecrets->LoadSecrets();
            }
            UserSecrets->Gemini_API_Key = Gemini_API_Key_UI;
            UserSecrets->SaveSecrets();
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeek_API_Key_UI))
        {
            if (!UserSecrets)
            {
                UserSecrets = NewObject<UN2CUserSecrets>();
                UserSecrets->LoadSecrets();
            }
            UserSecrets->DeepSeek_API_Key = DeepSeek_API_Key_UI;
            UserSecrets->SaveSecrets();
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
    if (!UserSecrets)
    {
        // Create and load secrets if not already done
        UserSecrets = NewObject<UN2CUserSecrets>();
        UserSecrets->LoadSecrets();
    }

    switch (InProvider)
    {
        case EN2CLLMProvider::OpenAI:
            return UserSecrets->OpenAI_API_Key;
        case EN2CLLMProvider::Anthropic:
            return UserSecrets->Anthropic_API_Key;
        case EN2CLLMProvider::Gemini:
            return UserSecrets->Gemini_API_Key;
        case EN2CLLMProvider::DeepSeek:
            return UserSecrets->DeepSeek_API_Key;
        case EN2CLLMProvider::LMStudio:
            return TEXT("lm-studio"); // LM Studio just requires a dummy API key for its OpenAI endpoint
        default:
            return FString();
    }
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

void UN2CSettings::RefreshOAuthStatus()
{
    UN2CAnthropicOAuthTokenManager* TokenManager = UN2CAnthropicOAuthTokenManager::Get();
    if (TokenManager && TokenManager->IsAuthenticated())
    {
        if (TokenManager->IsTokenExpired())
        {
            OAuthConnectionStatus = TEXT("Token expired - will refresh on next request");
        }
        else
        {
            OAuthConnectionStatus = FString::Printf(TEXT("Connected (expires: %s)"),
                *TokenManager->GetExpirationTimeString());
        }
    }
    else
    {
        OAuthConnectionStatus = TEXT("Not connected");
    }
}

void UN2CSettings::RefreshGeminiOAuthStatus()
{
    UN2CGoogleOAuthTokenManager* TokenManager = UN2CGoogleOAuthTokenManager::Get();
    if (TokenManager && TokenManager->IsAuthenticated())
    {
        if (TokenManager->IsTokenExpired())
        {
            GeminiOAuthConnectionStatus = TEXT("Token expired - will refresh on next request");
        }
        else
        {
            GeminiOAuthConnectionStatus = FString::Printf(TEXT("Connected (expires: %s)"),
                *TokenManager->GetExpirationTimeString());
        }
    }
    else
    {
        GeminiOAuthConnectionStatus = TEXT("Not connected");
    }
}

void UN2CSettings::NotifyModelSettingsChanged()
{
    FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
    Service.RefreshModelInfo();
    Service.OnModelChanged.Broadcast();
}
