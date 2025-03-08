// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettings.h"
#include "Core/N2CUserSecrets.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Async/AsyncWork.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LLM/N2CLLMModels.h"
#include "Utils/N2CLogger.h"

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
    
    // Initialize pricing for each model
    InitializePricing();

    // Validate reference source paths on startup
    ValidateReferenceSourcePaths();
    
    if (!UserSecrets)
    {
        UserSecrets = GetMutableDefault<UN2CUserSecrets>();
        
        // For UE 5.5 compatibility, ensure the config is loaded
        FString UserSecretsConfigPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
            FPaths::ProjectSavedDir(), TEXT("Config"), FPlatformProperties::PlatformName(), 
            TEXT("EditorNodeToCodeSecrets.ini")));
        
        // Check if the file exists before attempting to load
        if (FPaths::FileExists(UserSecretsConfigPath))
        {
            // Force reload config to ensure we have the latest values
            GConfig->LoadFile(UserSecretsConfigPath);
            UserSecrets->LoadConfig(nullptr, nullptr, UE::LCPF_PropagateToChildDefaultObjects);
            
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Loaded user secrets from: %s"), *UserSecretsConfigPath),
                EN2CLogSeverity::Info);
        }
    }

    // Initialize API Keys
    OpenAI_API_Key_UI = UserSecrets->OpenAI_API_Key;
    Anthropic_API_Key_UI = UserSecrets->Anthropic_API_Key;
    Gemini_API_Key_UI = UserSecrets->Gemini_API_Key;
    DeepSeek_API_Key_UI = UserSecrets->DeepSeek_API_Key;
    
    // Initialize token estimate
    EstimatedReferenceTokens = GetReferenceFilesTokenEstimate();

    // Set tooltip for ReferenceSourceFilePaths
    FProperty* ReferenceFilesProperty = GetClass()->FindPropertyByName(TEXT("ReferenceSourceFilePaths"));
    if (ReferenceFilesProperty)
    {
        ReferenceFilesProperty->SetMetaData(TEXT("ToolTip"), TEXT("Source files to include as context in LLM prompts"));
    }
}

FText UN2CSettings::GetSectionText() const
{
    return LOCTEXT("SettingsSection", "Node to Code");
}

FString UN2CSettings::GetActiveApiKey() const
{
    if (!UserSecrets)
    {
        UserSecrets = GetMutableDefault<UN2CUserSecrets>();
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
    // Initialize default pricing
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT4o_2024_08_06, FN2COpenAIPricing(2.5f, 10.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT4o_Mini_2024_07_18, FN2COpenAIPricing(0.15f, 0.6f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o1, FN2COpenAIPricing(15.0f, 60.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o3_mini, FN2COpenAIPricing(1.1f, 4.4f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o1_Preview, FN2COpenAIPricing(15.0f, 60.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o1_Mini, FN2COpenAIPricing(1.1f, 4.4f));

    AnthropicModelPricing.Add(EN2CAnthropicModel::Claude3_7_Sonnet, FN2CAnthropicPricing(3.0f, 15.0f));
    AnthropicModelPricing.Add(EN2CAnthropicModel::Claude3_5_Sonnet, FN2CAnthropicPricing(3.0f, 15.0f));
    AnthropicModelPricing.Add(EN2CAnthropicModel::Claude3_5_Haiku, FN2CAnthropicPricing(0.8f, 4.0f));

    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_Flash_2_0, FN2CGeminiPricing(1.0f, 0.4f));
    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_Flash_Lite_2_0, FN2CGeminiPricing(0.075f, 0.3f));
    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_1_5_Flash, FN2CGeminiPricing(0.075f, 0.3f));
    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_1_5_Pro, FN2CGeminiPricing(1.25f, 5.0f));
    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_2_0_ProExp_02_05, FN2CGeminiPricing(0.0f, 0.0f));
    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_2_0_FlashThinkingExp, FN2CGeminiPricing(0.0f, 0.0f));
    
    DeepSeekModelPricing.Add(EN2CDeepSeekModel::DeepSeek_R1, FN2CDeepSeekPricing(0.55f, 2.19f));
    DeepSeekModelPricing.Add(EN2CDeepSeekModel::DeepSeek_V3, FN2CDeepSeekPricing(0.14f, 0.28f));
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
                UserSecrets = GetMutableDefault<UN2CUserSecrets>();
            }
            UserSecrets->OpenAI_API_Key = OpenAI_API_Key_UI;
            UserSecrets->SaveConfig(CPF_Config, *UserSecrets->GetDefaultConfigFilename());
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Anthropic_API_Key_UI))
        {
            if (!UserSecrets)
            {
                UserSecrets = GetMutableDefault<UN2CUserSecrets>();
            }
            UserSecrets->Anthropic_API_Key = Anthropic_API_Key_UI;
            UserSecrets->SaveConfig(CPF_Config, *UserSecrets->GetDefaultConfigFilename());
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, Gemini_API_Key_UI))
        {
            if (!UserSecrets)
            {
                UserSecrets = GetMutableDefault<UN2CUserSecrets>();
            }
            UserSecrets->Gemini_API_Key = Gemini_API_Key_UI;
            UserSecrets->SaveConfig(CPF_Config, *UserSecrets->GetDefaultConfigFilename());
            return;
        }
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, DeepSeek_API_Key_UI))
        {
            if (!UserSecrets)
            {
                UserSecrets = GetMutableDefault<UN2CUserSecrets>();
            }
            UserSecrets->DeepSeek_API_Key = DeepSeek_API_Key_UI;
            UserSecrets->SaveConfig(CPF_Config, *UserSecrets->GetDefaultConfigFilename());
            return;
        }

        // Update logger severity when MinSeverity changes
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UN2CSettings, MinSeverity))
        {
            FN2CLogger::Get().SetMinSeverity(MinSeverity);
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
    }
    const FString ConfigPath = GetDefaultConfigFilename();
    
    // Add debug logging for config save
    UpdateSinglePropertyInConfigFile(Property, ConfigPath);

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Saving settings to: %s"), *ConfigPath),
        EN2CLogSeverity::Info
    );
}

#undef LOCTEXT_NAMESPACE
const FN2CCodeEditorColors* UN2CSettings::GetThemeColors(EN2CCodeLanguage Language, const FName& ThemeName) const
{
    const FN2CCodeEditorThemes* Themes = nullptr;
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            Themes = &CPPThemes;
            break;
        case EN2CCodeLanguage::Python:
            Themes = &PythonThemes;
            break;
        case EN2CCodeLanguage::JavaScript:
            Themes = &JavaScriptThemes;
            break;
        case EN2CCodeLanguage::CSharp:
            Themes = &CSharpThemes;
            break;
        case EN2CCodeLanguage::Swift:
            Themes = &SwiftThemes;
            break;
        case EN2CCodeLanguage::Pseudocode:
            Themes = &PseudocodeThemes;
            break;
    }

    if (Themes)
    {
        if (const FN2CCodeEditorColors* Colors = Themes->Themes.Find(ThemeName))
        {
            return Colors;
        }
        // Fallback to "Unreal Engine" theme if specified theme not found
        return Themes->Themes.Find(FName("Unreal Engine"));
    }

    return nullptr;
}
