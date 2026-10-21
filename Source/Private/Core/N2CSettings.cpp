// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettings.h"
#include "Core/N2CUserSecrets.h"
#include "Code Editor/Widgets/SN2CCodeEditor.h"
#include "Async/AsyncWork.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LLM/N2CLLMModels.h"
#include "Utils/N2CLogger.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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
            return GetAnthropicModel();
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

FString UN2CSettings::GetAnthropicModel() const
{
    const FString CustomModel = AnthropicCustomModelId.TrimStartAndEnd();
    if (!CustomModel.IsEmpty())
    {
        return CustomModel;
    }
    return AnthropicModelId.IsEmpty() ? FN2CLLMModelUtils::DefaultAnthropicModel : AnthropicModelId;
}

TArray<FString> UN2CSettings::GetAnthropicModelOptions() const
{
    TArray<FString> Options = AvailableAnthropicModels.Num() > 0
        ? AvailableAnthropicModels
        : FN2CLLMModelUtils::FallbackAnthropicModels;

    // Keep the saved selection visible even if it is not in the list
    if (!AnthropicModelId.IsEmpty())
    {
        Options.AddUnique(AnthropicModelId);
    }
    return Options;
}

void UN2CSettings::RefreshAnthropicModels()
{
    FetchAnthropicModels(true);
}

void UN2CSettings::FetchAnthropicModels(bool bWarnIfNoApiKey)
{
    if (!UserSecrets)
    {
        UserSecrets = NewObject<UN2CUserSecrets>();
        UserSecrets->LoadSecrets();
    }

    const FString ApiKey = UserSecrets->Anthropic_API_Key;
    if (ApiKey.IsEmpty())
    {
        if (bWarnIfNoApiKey)
        {
            FN2CLogger::Get().LogWarning(TEXT("Set an Anthropic API key before refreshing the model list"), TEXT("AnthropicModels"));
        }
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(TEXT("https://api.anthropic.com/v1/models?limit=100"));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("x-api-key"), ApiKey);
    Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));

    TWeakObjectPtr<UN2CSettings> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            UN2CSettings* Settings = WeakThis.Get();
            if (!Settings)
            {
                return;
            }

            if (!bSucceeded || !Response.IsValid())
            {
                FN2CLogger::Get().LogWarning(TEXT("Could not reach the Anthropic Models API, using the built-in model list"), TEXT("AnthropicModels"));
                return;
            }

            if (Response->GetResponseCode() != 200)
            {
                FN2CLogger::Get().LogWarning(
                    FString::Printf(TEXT("Anthropic Models API returned HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString()),
                    TEXT("AnthropicModels"));
                return;
            }

            TSharedPtr<FJsonObject> JsonObject;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
            const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
            if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid() || !JsonObject->TryGetArrayField(TEXT("data"), Data))
            {
                FN2CLogger::Get().LogWarning(TEXT("Could not parse the Anthropic Models API response"), TEXT("AnthropicModels"));
                return;
            }

            // The API returns the newest models first
            TArray<FString> ModelIds;
            for (const TSharedPtr<FJsonValue>& Value : *Data)
            {
                const TSharedPtr<FJsonObject>* ModelObject = nullptr;
                FString ModelId;
                if (Value.IsValid() && Value->TryGetObject(ModelObject) && (*ModelObject)->TryGetStringField(TEXT("id"), ModelId))
                {
                    ModelIds.Add(ModelId);
                }
            }

            if (ModelIds.Num() == 0)
            {
                FN2CLogger::Get().LogWarning(TEXT("Anthropic Models API returned no models, using the built-in model list"), TEXT("AnthropicModels"));
                return;
            }

            Settings->AvailableAnthropicModels = MoveTemp(ModelIds);
            FN2CLogger::Get().Log(
                FString::Printf(TEXT("Loaded %d Anthropic models"), Settings->AvailableAnthropicModels.Num()),
                EN2CLogSeverity::Info);

            if (Settings->AnthropicCustomModelId.TrimStartAndEnd().IsEmpty()
                && !Settings->AvailableAnthropicModels.Contains(Settings->AnthropicModelId))
            {
                FN2CLogger::Get().LogWarning(
                    FString::Printf(TEXT("Selected Anthropic model '%s' is not available for this API key. Pick another model in Project Settings > Node to Code."), *Settings->AnthropicModelId),
                    TEXT("AnthropicModels"));
            }
        });

    Request->ProcessRequest();
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
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_4_1, FN2COpenAIPricing(2.0f, 8.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o1, FN2COpenAIPricing(15.0f, 60.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o3_mini, FN2COpenAIPricing(1.1f, 4.4f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o3, FN2COpenAIPricing(15.0f, 60.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o4_mini, FN2COpenAIPricing(1.1f, 4.4f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o1_Preview, FN2COpenAIPricing(15.0f, 60.0f));
    OpenAIModelPricing.Add(EN2COpenAIModel::GPT_o1_Mini, FN2COpenAIPricing(1.1f, 4.4f));

    AnthropicModelPricing.Add(TEXT("claude-opus-5-5"), FN2CAnthropicPricing(4.0f, 20.0f));
    AnthropicModelPricing.Add(TEXT("claude-sonnet-5-5"), FN2CAnthropicPricing(2.0f, 10.0f));
    AnthropicModelPricing.Add(TEXT("claude-haiku-5-5"), FN2CAnthropicPricing(0.1f, 0.5f));

    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_2_5_Pro, FN2CGeminiPricing(1.25f, 10.0f));
    GeminiModelPricing.Add(EN2CGeminiModel::Gemini_2_5_Flash, FN2CGeminiPricing(0.0f, 0.0f));
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
            FetchAnthropicModels(false);
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
