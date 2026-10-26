// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CLogging.h"
#include "Code Editor/Widgets/N2CCodeEditorWidgetFactory.h"
#include "Code Editor/Models/N2CCodeEditorStyle.h"

#define LOCTEXT_NAMESPACE "N2CCodeEditorWidgetFactory"

TSharedPtr<FN2CCodeEditorWidgetFactory> FN2CCodeEditorWidgetFactory::Instance = nullptr;

void FN2CCodeEditorWidgetFactory::Register()
{
    N2C_LOG_FUNCTION_ENTRY();
    if (!Instance.IsValid())
    {
        Instance = MakeShared<FN2CCodeEditorWidgetFactory>();
        Instance->RegisterFactory();
    }
}

void FN2CCodeEditorWidgetFactory::Unregister()
{
    N2C_LOG_FUNCTION_ENTRY();
    if (Instance.IsValid())
    {
        Instance->UnregisterFactory();
        Instance.Reset();
    }
}

void FN2CCodeEditorWidgetFactory::RegisterFactory()
{
    N2C_LOG_FUNCTION_ENTRY();
    // We don't need to register for asset notifications or handle widget blueprints
    // The code editor is a standalone widget that doesn't need Blueprint integration
}


void FN2CCodeEditorWidgetFactory::UnregisterFactory()
{
    N2C_LOG_FUNCTION_ENTRY();
    // Just clean up the style
    if (FN2CCodeEditorStyle::Get().GetStyleSetName() != NAME_None)
    {
        FN2CCodeEditorStyle::Shutdown();
    }
}

#undef LOCTEXT_NAMESPACE
