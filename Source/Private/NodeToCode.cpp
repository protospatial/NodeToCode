// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "NodeToCode.h"

#include <Core/N2CWidgetContainer.h>

#include "Models/N2CLogging.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"
#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Code Editor/Widgets/N2CCodeEditorWidgetFactory.h"
#include "Models/N2CStyle.h"


DEFINE_LOG_CATEGORY(LogNodeToCode);

#define LOCTEXT_NAMESPACE "FNodeToCodeModule"

void FNodeToCodeModule::StartupModule()
{
    // Initialize logging
    FN2CLogger::Get().Log(TEXT("NodeToCode plugin starting up"), EN2CLogSeverity::Info);
    
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

    if (!CPPSyntax || !PythonSyntax || !JSSyntax || !CSharpSyntax || !SwiftSyntax)
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


#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FNodeToCodeModule, NodeToCode)
