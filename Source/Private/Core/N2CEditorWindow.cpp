// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CEditorWindow.h"
#include "Core/N2CWidgetContainer.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Docking/SDockTab.h"

const FName SN2CEditorWindow::TabId(TEXT("NodeToCodeEditor"));
TWeakPtr<SDockTab> SN2CEditorWindow::ActiveTab;

void SN2CEditorWindow::RegisterTabSpawner()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabId,
        FOnSpawnTab::CreateStatic(&SN2CEditorWindow::SpawnTab))
        .SetDisplayName(NSLOCTEXT("NodeToCode", "TabTitle", "Node to Code"))
        .SetMenuType(ETabSpawnerMenuType::Hidden)
        .SetIcon(FSlateIcon("NodeToCodeStyle", "NodeToCode.ToolbarButton"));

    FN2CLogger::Get().Log(TEXT("Registered Node to Code tab spawner"), EN2CLogSeverity::Info);
}

void SN2CEditorWindow::UnregisterTabSpawner()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
    FN2CLogger::Get().Log(TEXT("Unregistered Node to Code tab spawner"), EN2CLogSeverity::Info);
}

TSharedRef<SDockTab> SN2CEditorWindow::SpawnTab(const FSpawnTabArgs& Args)
{
    // Check if we already have an active tab
    if (TSharedPtr<SDockTab> ExistingTab = ActiveTab.Pin())
    {
        // Bring the existing tab to front
        ExistingTab->DrawAttention();
        return ExistingTab.ToSharedRef();
    }

    // Create new tab
    TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        // Add OnTabClosed handler immediately during construction
        .OnTabClosed_Static(&SN2CEditorWindow::OnTabClosed);

    // Store the active tab reference before creating content
    // This prevents potential recursive spawning
    ActiveTab = SpawnedTab;

    TSharedRef<SN2CEditorWindow> EditorWindow = SNew(SN2CEditorWindow);
    SpawnedTab->SetContent(EditorWindow);

    return SpawnedTab;
}

void SN2CEditorWindow::OnTabClosed(TSharedRef<SDockTab> ClosedTab)
{
    // Clear the active tab reference
    if (ActiveTab.Pin() == ClosedTab)
    {
        ActiveTab.Reset();
    }
}

void SN2CEditorWindow::Construct(const FArguments& InArgs)
{
    // 1) Load the widget blueprint
    FString AssetPath = TEXT("/Script/Blutility.EditorUtilityWidgetBlueprint'/NodeToCode/UI/NodeToCodeUI.NodeToCodeUI'");
    UEditorUtilityWidgetBlueprint* EditorBP = LoadObject<UEditorUtilityWidgetBlueprint>(nullptr, *AssetPath);
    if (!EditorBP)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to load NodeToCodeUI blueprint at path: %s"), 
            *AssetPath));
        return;
    }

    // 2) Manually create the widget (instead of SpawnAndRegisterTab)
    UClass* WidgetClass = EditorBP->GeneratedClass;
    if (!WidgetClass || !WidgetClass->IsChildOf(UEditorUtilityWidget::StaticClass()))
    {
        FN2CLogger::Get().LogError(TEXT("Loaded blueprint is not a valid Editor Utility Widget class"));
        return;
    }

    // Use our persistent container as the outer
    UEditorUtilityWidget* EditorWidget = NewObject<UEditorUtilityWidget>(
        UN2CWidgetContainer::Get(),
        WidgetClass
    );
    if (!EditorWidget)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create Editor Utility Widget instance"));
        return;
    }

    // 3) Embed the widget's Slate widget in our Nomad tab
    ChildSlot
    [
        EditorWidget->TakeWidget()
    ];

    FN2CLogger::Get().Log(TEXT("Successfully created and embedded NodeToCodeUI widget"), EN2CLogSeverity::Info);
}
