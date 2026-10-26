// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "NodeToCode.h"

#include <Core/N2CWidgetContainer.h>

#include "HttpModule.h"
#include "Models/N2CLogging.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CSecretStore.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTagManager.h"
#include "Editor/EditorEngine.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "Core/N2CUserSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Code Editor/Widgets/N2CCodeEditorWidgetFactory.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Core/Style/N2CStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "Core/Services/N2CTokenEstimationService.h"
#if WITH_EDITOR
#include "UnrealEdMisc.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#endif

DEFINE_LOG_CATEGORY(LogNodeToCode);

#define LOCTEXT_NAMESPACE "FNodeToCodeModule"

void FNodeToCodeModule::StartupModule()
{
    // Initialize logging
    FN2CLogger::Get().Log(TEXT("NodeToCode plugin starting up"), EN2CLogSeverity::Info);

    // On editor exit, UObjects (the rooted graph state singleton included) are purged before ShutdownModule runs,
    // so running MCP translations are cancelled and state marked dirty without a save is flushed here, while the
    // LLM services and singletons still exist
    PreExitHandle = FCoreDelegates::OnPreExit.AddStatic(&FNodeToCodeModule::HandlePreExit);
    EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddStatic(&FNodeToCodeModule::CancelAsyncTasks);

    // HTTP timeouts are set per request by the LLM HTTP handler; nothing is written to project config.
    // While a translation is in flight, keep the editor at full speed even when it is in the background, without
    // changing the user's "Use Less CPU when in Background" preference.
    if (GEditor)
    {
        UEditorEngine::FShouldDisableCPUThrottling ShouldDisable = UEditorEngine::FShouldDisableCPUThrottling::CreateLambda([]()
        {
            const UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
            return FN2CEditorIntegration::Get().IsAnyTranslationInProgress()
                || UN2CBatchTranslationOrchestrator::Get().IsBatchInProgress()
                || (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing);
        });
        DisableThrottleHandle = ShouldDisable.GetHandle();
        GEditor->ShouldDisableCPUThrottlingDelegates.Add(MoveTemp(ShouldDisable));
    }

    // Load the API keys once, moving a project's Saved/NodeToCode/User/secrets.json to the per-user file
    FN2CSecretStore::Get();

    // Save what loading a NodeToCode 1.2.x config changed (retired models, unknown values) and clean up, once, what
    // 1.2.x left in the project. Not in commandlets, so builds and cooks never rewrite config.
    if (!IsRunningCommandlet())
    {
        FN2CLegacyMigrationTargets Targets = FN2CLegacyMigrationTargets::ForProject();
        if (const UEditorPerformanceSettings* Performance = GetDefault<UEditorPerformanceSettings>())
        {
            Targets.bThrottleCPUWhenNotForeground = Performance->bThrottleCPUWhenNotForeground;
        }
        GetMutableDefault<UN2CSettings>()->MigrateLegacy12Settings(Targets);
    }
    
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

    // Initialize token estimation service
    FN2CTokenEstimationService::Get().Initialize();
    FN2CLogger::Get().Log(TEXT("Token estimation service initialized"), EN2CLogSeverity::Debug);

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

    // Start the MCP HTTP server on McpServerPort or the next free port
    const UN2CSettings* McpSettings = GetDefault<UN2CSettings>();

    // Not in commandlets or unattended runs (build agents, smoke tests) unless -N2CMcp is on the command
    // line, and never when the user turned the server off.
    const bool bForceMcp = FParse::Param(FCommandLine::Get(), TEXT("N2CMcp"));
    FString McpSkipReason;
    if (!UN2CUserSettings::IsMcpServerEnabled())
    {
        McpSkipReason = TEXT("Enable MCP Server is off in Project Settings > Plugins > Node to Code (User)");
    }
    else if (!bForceMcp && IsRunningCommandlet())
    {
        McpSkipReason = TEXT("running a commandlet (pass -N2CMcp to start it anyway)");
    }
    else if (!bForceMcp && FApp::IsUnattended())
    {
        McpSkipReason = TEXT("running unattended (pass -N2CMcp to start it anyway)");
    }
    if (McpSkipReason.IsEmpty() && McpSettings && McpSettings->McpToolProfile == EN2CMcpToolProfile::PythonScripting && !UN2CUserSettings::IsMcpPythonExecutionAllowed())
    {
        FN2CLogger::Get().LogWarning(TEXT("Tool Profile is Python Scripting Only but Allow MCP Python Execution is off: only the list, search and get script tools are available"));
    }

    if (!McpSkipReason.IsEmpty())
    {
        FN2CLogger::Get().Log(FString::Printf(TEXT("MCP server not started: %s"), *McpSkipReason), EN2CLogSeverity::Info);
        if (UN2CSettings* MutableSettings = GetMutableDefault<UN2CSettings>())
        {
            MutableSettings->McpServerStatus = FString::Printf(TEXT("Not started: %s"), *McpSkipReason);
        }
    }
    else
    {
        // Logs and notifies on its own when the port is taken
        FN2CMcpHttpServerManager::Get().StartConfiguredServers();
    }
}

void FNodeToCodeModule::CancelAsyncTasks()
{
    // A running translation holds an in-flight LLM request on UObjects that are about to be purged: cancel it now, so
    // no completion or ticker runs after the purge and exit does not wait for the LLM
    FN2CToolAsyncTaskManager::Get().CancelAllTasks();
}

void FNodeToCodeModule::HandlePreExit()
{
    CancelAsyncTasks();
    ShutdownStateManagers();
}

void FNodeToCodeModule::ShutdownStateManagers()
{
    UN2CTagManager::ShutdownInstance();
    UN2CGraphStateManager::ShutdownInstance();
}

void FNodeToCodeModule::ShutdownModule()
{
    FCoreDelegates::OnPreExit.Remove(PreExitHandle);
    PreExitHandle.Reset();
    FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
    EnginePreExitHandle.Reset();

    if (GEditor && DisableThrottleHandle.IsValid())
    {
        GEditor->ShouldDisableCPUThrottlingDelegates.RemoveAll([this](const UEditorEngine::FShouldDisableCPUThrottling& D)
        {
            return D.GetHandle() == DisableThrottleHandle;
        });
        DisableThrottleHandle.Reset();
    }

    // Cancel running MCP async tasks while their requests can still be answered (on exit, HandlePreExit already did
    // this and there is nothing left to cancel)
    FN2CToolAsyncTaskManager::Get().CancelAllTasks();

    // Unbind the MCP routes (other plugins' HTTPServer listeners keep running)
    FN2CMcpHttpServerManager::Get().StopConfiguredServers();

    // Drop a queued action-database refresh: its ticker calls into this module
    FN2CMcpBlueprintUtils::CancelPendingActionDatabaseRefresh();

    // Unregister menu extensions
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);

    // Shutdown token estimation service
    FN2CTokenEstimationService::Get().Shutdown();
    FN2CLogger::Get().Log(TEXT("Token estimation service shutdown"), EN2CLogSeverity::Debug);

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

        // Unloaded without exiting (OnPreExit already did this on exit): save graph state that was marked dirty
        // without a save, and release the rooted singletons
        ShutdownStateManagers();
    }

    FN2CLogger::Get().Log(TEXT("NodeToCode plugin shutting down"), EN2CLogSeverity::Info);
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FNodeToCodeModule, NodeToCode)
