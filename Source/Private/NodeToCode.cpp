// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "NodeToCode.h"

#include <Core/N2CWidgetContainer.h>

#include "HttpModule.h"
#include "Models/N2CLogging.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Code Editor/Widgets/N2CCodeEditorWidgetFactory.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Models/N2CStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#if WITH_EDITOR
#include "UnrealEdMisc.h"
#endif

DEFINE_LOG_CATEGORY(LogNodeToCode);

#define LOCTEXT_NAMESPACE "FNodeToCodeModule"

void FNodeToCodeModule::StartupModule()
{
    // Initialize logging
    FN2CLogger::Get().Log(TEXT("NodeToCode plugin starting up"), EN2CLogSeverity::Info);

    // Configure HTTP timeout settings for LLM operations
    ConfigureHttpTimeouts();
    
    // Force disable "Use Less CPU when in Background" setting to prevent HTTP request issues when the editor is not focused
    if (GEditor)
    {
        UEditorPerformanceSettings* PerfSettings = GetMutableDefault<UEditorPerformanceSettings>();
        if (PerfSettings)
        {
            PerfSettings->bThrottleCPUWhenNotForeground = false;
            PerfSettings->SaveConfig();
            FN2CLogger::Get().Log(TEXT("Disabled 'Use Less CPU when in Background' setting"), EN2CLogSeverity::Info);
        }
    }

    // Load user secrets
    UN2CUserSecrets* UserSecrets = NewObject<UN2CUserSecrets>();
    UserSecrets->LoadSecrets();
    
    // Apply configured log severity from settings
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings)
    {
        FN2CLogger::Get().SetMinSeverity(Settings->MinSeverity);
        FN2CLogger::Get().Log(TEXT("Applied log severity from settings"), EN2CLogSeverity::Debug);
    }

    
    // Initialize style system
    N2CStyle::Initialize();
    FN2CLogger::Get().Log(TEXT("Node to Code style initialized"), EN2CLogSeverity::Debug);

    // Initialize code editor style system
    FN2CCodeEditorStyle::Initialize();
    FN2CLogger::Get().Log(TEXT("Code editor style initialized"), EN2CLogSeverity::Debug);

    // Initialize editor integration
    FN2CEditorIntegration::Get().Initialize();
    FN2CLogger::Get().Log(TEXT("Editor integration initialized"), EN2CLogSeverity::Debug);
    
    // Register widget factory
    FN2CCodeEditorWidgetFactory::Register();
    FN2CLogger::Get().Log(TEXT("Widget factory registered"), EN2CLogSeverity::Debug);
    
    // Verify syntax factory is working
    auto CPPSyntax = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(EN2CCodeLanguage::Cpp);
    auto PythonSyntax = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(EN2CCodeLanguage::Python);
    auto JSSyntax = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(EN2CCodeLanguage::JavaScript);
    auto CSharpSyntax = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(EN2CCodeLanguage::CSharp);
    auto SwiftSyntax = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(EN2CCodeLanguage::Swift);
    auto PseudocodeSyntax = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(EN2CCodeLanguage::Pseudocode);

    if (!CPPSyntax || !PythonSyntax || !JSSyntax || !CSharpSyntax || !SwiftSyntax || !PseudocodeSyntax)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize syntax definitions"), TEXT("NodeToCode"));
    }
    else 
    {
        FN2CLogger::Get().Log(TEXT("Syntax definitions initialized successfully"), EN2CLogSeverity::Debug);
    }
}

void FNodeToCodeModule::ShutdownModule()
{
    // Unregister menu extensions
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    // Shutdown editor integration
    FN2CEditorIntegration::Get().Shutdown();

    // Unregister widget factory
    FN2CCodeEditorWidgetFactory::Unregister();

    // Shutdown code editor style system
    FN2CCodeEditorStyle::Shutdown();

    // Shutdown style system
    N2CStyle::Shutdown();
    
    // Clean up widget container
    if (!GExitPurge)  // Only clean up if not already in exit purge
    {
        UN2CWidgetContainer::Reset();
    }

    FN2CLogger::Get().Log(TEXT("NodeToCode plugin shutting down"), EN2CLogSeverity::Info);
}

void FNodeToCodeModule::ConfigureHttpTimeouts()
{
    FN2CLogger::Get().Log(TEXT("Checking HTTP timeout settings for Ollama support..."), EN2CLogSeverity::Info);
    
    // Get the project's DefaultEngine.ini path
    FString DefaultEngineIniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
    
    // Check if the file exists
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.FileExists(*DefaultEngineIniPath))
    {
        FN2CLogger::Get().LogWarning(TEXT("Could not find DefaultEngine.ini"), TEXT("NodeToCode"));
        return;
    }
    
    // Create config object
    UHttpTimeoutConfig* TimeoutConfig = NewObject<UHttpTimeoutConfig>();
    
    // Check current settings
    TimeoutConfig->LoadConfig();
    if (TimeoutConfig->HttpConnectionTimeout >= 300.0f && 
    TimeoutConfig->HttpActivityTimeout >= 3600.0f)
    {
        FN2CLogger::Get().Log(TEXT("HTTP timeout settings already configured correctly"), EN2CLogSeverity::Info);
        return;
    }

    // Apply our settings values
    TimeoutConfig->HttpConnectionTimeout = 300.0f;
    TimeoutConfig->HttpActivityTimeout = 3600.0f;
    
    // Save the config, which writes to the specified ini file
    TimeoutConfig->TryUpdateDefaultConfigFile(*DefaultEngineIniPath);
    
    FN2CLogger::Get().Log(
        TEXT("Added HTTP timeout settings to DefaultEngine.ini to support long-running Ollama requests"), 
        EN2CLogSeverity::Info
    );
    
    // Apply the changes immediately
    FConfigCacheIni::LoadGlobalIniFile(GEngineIni, TEXT("Engine"));
    FHttpModule::Get().UpdateConfigs();

    // Show notification that restart is required for full effect
    ShowRestartRequiredNotification();
}

void FNodeToCodeModule::ShowRestartRequiredNotification()
{
#if WITH_EDITOR
    if (!GIsEditor)
    {
        return;
    }

    FNotificationInfo Info(LOCTEXT("HttpSettingsChangedTitle", "Node To Code Plugin"));
    Info.Text = LOCTEXT("HttpSettingsChangedMessage", 
                       "HTTP timeout settings have been updated for Node To Code. Please restart the editor for them to take effect.");
    Info.bFireAndForget = true;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 10.0f;
    Info.bUseThrobber = false;
    Info.bUseSuccessFailIcons = true;
    Info.bUseLargeFont = false;

    TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
    if (NotificationItem.IsValid())
    {
        NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
    }
#endif
}


#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FNodeToCodeModule, NodeToCode)
