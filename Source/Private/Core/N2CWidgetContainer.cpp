// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CWidgetContainer.h"
#include "Editor.h"
#include "Utils/N2CLogger.h"

UN2CWidgetContainer* UN2CWidgetContainer::Instance = nullptr;

UN2CWidgetContainer* UN2CWidgetContainer::Get()
{
    if (!Instance && !GExitPurge)
    {
        Instance = NewObject<UN2CWidgetContainer>(
            GetTransientPackage(),
            TEXT("NodeToCodeWidgetContainer")
        );
        if (Instance)
        {
            Instance->AddToRoot(); // Prevent GC
            FN2CLogger::Get().Log(TEXT("Created NodeToCode widget container"), EN2CLogSeverity::Debug);
        }
    }
    return Instance;
}

void UN2CWidgetContainer::Reset()
{
    if (Instance)
    {
        if (!Instance->IsValidLowLevel())
        {
            Instance->RemoveFromRoot();
            FN2CLogger::Get().Log(TEXT("Reset NodeToCode widget container"), EN2CLogSeverity::Debug);
        }
        Instance = nullptr;
    }
}

void UN2CWidgetContainer::BeginDestroy()
{
    // Log destruction for debugging
    FN2CLogger::Get().Log(TEXT("NodeToCode widget container being destroyed"), EN2CLogSeverity::Debug);

    // If we are the instance, clear it
    if (Instance == this)
    {
        Instance = nullptr;
    }

    Super::BeginDestroy();
}

UWorld* UN2CWidgetContainer::GetWorld() const
{
    // If we have a valid editor instance, return its world context
    if (GEditor)
    {
        return GEditor->GetEditorWorldContext().World();
    }
    return nullptr;
}
