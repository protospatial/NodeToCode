// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"

class SN2CEditorWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SN2CEditorWindow)
    {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    /** Register the tab spawner */
    static void RegisterTabSpawner();

    /** Unregister the tab spawner */
    static void UnregisterTabSpawner();

    /** Create and show the window as a dockable tab */
    static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

    /** Handler for when the tab is closed */
    static void OnTabClosed(TSharedRef<SDockTab> ClosedTab);

    /** Get the tab identifier */
    static const FName TabId;

private:
    /** The currently active tab */
    static TWeakPtr<SDockTab> ActiveTab;
};
