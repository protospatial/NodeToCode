// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CGraphOverlayInjector.h"

#include "BlueprintEditor.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/Widgets/SN2CGraphOverlay.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SGraphPanel.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "TimerManager.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SOverlay.h"

namespace N2COverlayInjectionPrivate
{
    /** The widget type SNew gives an SGraphEditor (it declares no widget class of its own) */
    const FName GraphEditorType(TEXT("SGraphEditor"));

    bool IsExactly(const SWidget& Widget, const FName Type)
    {
        return Widget.GetType() == Type;
    }

    /** The first ancestor of Widget of the given type, within MaxDepth parents */
    TSharedPtr<SWidget> FindAncestorOfType(const TSharedPtr<SWidget>& Widget, const FName Type, int32 MaxDepth = 64)
    {
        TSharedPtr<SWidget> Current = Widget.IsValid() ? Widget->GetParentWidget() : nullptr;
        for (int32 Depth = 0; Current.IsValid() && Depth < MaxDepth; ++Depth)
        {
            if (IsExactly(*Current, Type))
            {
                return Current;
            }
            Current = Current->GetParentWidget();
        }
        return nullptr;
    }

    /** Off: no poll. The overlay then follows only the editor-open and tab-foregrounded events */
    static TAutoConsoleVariable<bool> CVarOverlayPoll(
        TEXT("n2c.OverlayPoll"),
        true,
        TEXT("1 (default) checks every 2 s that each open Blueprint editor's focused graph has its NodeToCode overlay and ")
        TEXT("injects a missing one (a graph opened inside an existing tab raises no tab event). A graph that cannot get ")
        TEXT("one is retried with a growing delay and given up after a few tries. 0 turns the check off."),
        ECVF_Default);
}

FN2CGraphOverlayInjector& FN2CGraphOverlayInjector::Get()
{
    static FN2CGraphOverlayInjector Instance;
    return Instance;
}

void FN2CGraphOverlayInjector::Initialize()
{
    // OnAssetOpenedInEditor fires for every editor opened, also when several assets open at once (OnAssetEditorOpened
    // is not broadcast then), and passes the editor instance
    if (GEditor && !OnAssetOpenedInEditorHandle.IsValid())
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            OnAssetOpenedInEditorHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(
                this, &FN2CGraphOverlayInjector::HandleAssetEditorOpened);
        }
    }

    // OnTabForegrounded fires for both cross-editor tab switches and graph switches (EventGraph -> a function) within
    // one Blueprint editor
    if (!OnTabForegroundedHandle.IsValid())
    {
        OnTabForegroundedHandle = FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(
            FOnActiveTabChanged::FDelegate::CreateRaw(this, &FN2CGraphOverlayInjector::HandleTabForegrounded));
    }

    // A safety net behind the editor-open and tab-foregrounded hooks: a graph opened inside an existing tab (history
    // navigation, a collapsed graph) or a rebuilt graph editor raises no tab event. Quiet (Debug), backs off per graph
    // and gives up after MaxPollInjectionAttempts; n2c.OverlayPoll 0 turns it off (card 0293)
    if (GEditor && !PollTimerHandle.IsValid())
    {
        GEditor->GetTimerManager()->SetTimer(
            PollTimerHandle,
            FTimerDelegate::CreateRaw(this, &FN2CGraphOverlayInjector::PollOverlays),
            PollInterval,
            true);
    }

    FN2CLogger::Get().Log(TEXT("Graph overlay injector initialized"), EN2CLogSeverity::Debug);
}

void FN2CGraphOverlayInjector::Shutdown()
{
    if (GEditor)
    {
        for (FTimerHandle* Handle : { &InitialInjectTimerHandle, &TabChangeInjectTimerHandle, &PollTimerHandle })
        {
            if (Handle->IsValid())
            {
                GEditor->GetTimerManager()->ClearTimer(*Handle);
            }
            Handle->Invalidate();
        }
    }

    if (OnTabForegroundedHandle.IsValid())
    {
        FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(OnTabForegroundedHandle);
        OnTabForegroundedHandle.Reset();
    }

    if (OnAssetOpenedInEditorHandle.IsValid())
    {
        if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
        {
            AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(OnAssetOpenedInEditorHandle);
        }
        OnAssetOpenedInEditorHandle.Reset();
    }

    // Take the injected overlays out of the engine's graph editors: no widget of this module stays in their trees
    RemoveInjectedOverlays();
}

TSharedPtr<SOverlay> FN2CGraphOverlayInjector::FindGraphEditorOverlay(const TSharedRef<SGraphEditor>& GraphEditor)
{
    using namespace N2COverlayInjectionPrivate;
    const FName OverlayType = SOverlay::StaticWidgetClass().GetWidgetType();

    // SGraphEditorImpl wraps its graph panel (on 5.8 inside an SInvalidationPanel) in the SOverlay its own overlays
    // sit in: walk up from the panel to it, stopping at the graph editor itself
    if (SGraphPanel* Panel = GraphEditor->GetGraphPanel())
    {
        TSharedPtr<SWidget> Current = Panel->GetParentWidget();
        for (int32 Depth = 0; Current.IsValid() && Depth < 8 && Current != StaticCastSharedRef<SWidget>(GraphEditor); ++Depth)
        {
            if (IsExactly(*Current, OverlayType))
            {
                return StaticCastSharedPtr<SOverlay>(Current);
            }
            Current = Current->GetParentWidget();
        }
    }

    // Fallback: the first SOverlay below the graph editor, depth first (an engine whose tree differs)
    TFunction<TSharedPtr<SOverlay>(const TSharedRef<SWidget>&, int32)> FindOverlayRecursive =
        [&FindOverlayRecursive, OverlayType](const TSharedRef<SWidget>& Widget, int32 Depth) -> TSharedPtr<SOverlay>
    {
        if (Depth > 10)
        {
            return nullptr;
        }
        if (IsExactly(*Widget, OverlayType))
        {
            return StaticCastSharedRef<SOverlay>(Widget);
        }
        if (FChildren* ChildWidgets = Widget->GetChildren())
        {
            for (int32 i = 0; i < ChildWidgets->Num(); i++)
            {
                if (TSharedPtr<SOverlay> FoundOverlay = FindOverlayRecursive(ChildWidgets->GetChildAt(i), Depth + 1))
                {
                    return FoundOverlay;
                }
            }
        }
        return nullptr;
    };

    FN2CLogger::Get().Log(TEXT("FindGraphEditorOverlay: graph panel has no SOverlay parent, searching the graph editor's tree"), EN2CLogSeverity::Debug);
    return FindOverlayRecursive(GraphEditor, 0);
}

TSharedPtr<SGraphEditor> FN2CGraphOverlayInjector::FindGraphEditorFor(const TSharedPtr<FBlueprintEditor>& Editor, const UEdGraph* Graph)
{
    using namespace N2COverlayInjectionPrivate;
    if (!Editor.IsValid() || !Graph)
    {
        return nullptr;
    }

    // The graph's own document tab in this editor
    TArray<TSharedPtr<SDockTab>> Tabs;
    Editor->FindOpenTabsContainingDocument(Graph, Tabs);
    for (const TSharedPtr<SDockTab>& Tab : Tabs)
    {
        if (!Tab.IsValid())
        {
            continue;
        }
        const TSharedRef<SWidget> Content = Tab->GetContent();
        if (IsExactly(*Content, GraphEditorType))
        {
            const TSharedRef<SGraphEditor> GraphEditor = StaticCastSharedRef<SGraphEditor>(Content);
            if (GraphEditor->GetCurrentGraph() == Graph)
            {
                return GraphEditor;
            }
        }
    }

    // A graph shown in a tab registered under another document (a history navigation inside a tab): the graph editor
    // that shows it, if it sits in one of this editor's tabs. Never another graph's editor (review editor-integration-10)
    const TSharedPtr<SGraphEditor> Found = SGraphEditor::FindGraphEditorForGraph(Graph);
    if (!Found.IsValid() || !IsExactly(*Found, GraphEditorType) || Found->GetCurrentGraph() != Graph)
    {
        return nullptr;
    }
    const TSharedPtr<SWidget> OwningTab = FindAncestorOfType(Found, SDockTab::StaticWidgetClass().GetWidgetType());
    const TSharedPtr<FTabManager> EditorTabManager = Editor->GetTabManager();
    if (!OwningTab.IsValid() || !EditorTabManager.IsValid()
        || StaticCastSharedPtr<SDockTab>(OwningTab)->GetTabManagerPtr() != EditorTabManager)
    {
        return nullptr;
    }
    return Found;
}

bool FN2CGraphOverlayInjector::InjectIntoTab(TSharedPtr<SDockTab> Tab, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor)
{
    if (!Tab.IsValid() || !Graph)
    {
        return false;
    }

    // Exactly an SGraphEditor (not SGraphEditorImpl or any other widget whose name contains it) showing this graph
    const TSharedRef<SWidget> TabContent = Tab->GetContent();
    if (!N2COverlayInjectionPrivate::IsExactly(*TabContent, N2COverlayInjectionPrivate::GraphEditorType))
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("InjectIntoTab: tab content is not an SGraphEditor (type=%s)"), *TabContent->GetTypeAsString()),
            EN2CLogSeverity::Debug);
        return false;
    }
    const TSharedRef<SGraphEditor> GraphEditor = StaticCastSharedRef<SGraphEditor>(TabContent);
    if (GraphEditor->GetCurrentGraph() != Graph)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("InjectIntoTab: the tab shows another graph than '%s'"), *Graph->GetName()),
            EN2CLogSeverity::Debug);
        return false;
    }
    return InjectOverlay(GraphEditor, Graph, Editor);
}

bool FN2CGraphOverlayInjector::InjectOverlay(const TSharedRef<SGraphEditor>& GraphEditor, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor)
{
    if (!Graph || GraphEditor->GetCurrentGraph() != Graph)
    {
        return false;
    }

    // One overlay per graph editor widget. Keyed by the widget, not the graph's GUID (which a copy of this Blueprint
    // shares, card 0192) and not the graph alone (a graph editor rebuilt for the same graph needs a new one)
    if (CheckLiveOverlay(GraphEditor, Graph))
    {
        return true;
    }

    if (!Editor.IsValid())
    {
        FN2CLogger::Get().Log(TEXT("InjectOverlay: the Blueprint editor is gone"), EN2CLogSeverity::Debug);
        return false;
    }

    const TSharedPtr<SOverlay> GraphOverlay = FindGraphEditorOverlay(GraphEditor);
    if (!GraphOverlay.IsValid())
    {
        RecordInjectionFailure(Graph, TEXT("no SOverlay in its graph editor"));
        return false;
    }

    // Get graph info for the overlay
    const FGuid GraphGuid = Graph->GraphGuid;
    const FString GraphName = Graph->GetName();
    FString BlueprintPath;
    if (const UBlueprint* OwningBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph))
    {
        BlueprintPath = OwningBlueprint->GetPathName();
    }

    TSharedPtr<SN2CGraphOverlay> OverlayWidget;
    GraphOverlay->AddSlot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Bottom)
        .Padding(FMargin(10.0f, 0.0f, 0.0f, 10.0f))
        [
            SAssignNew(OverlayWidget, SN2CGraphOverlay)
            .GraphGuid(GraphGuid)
            .BlueprintPath(BlueprintPath)
            .GraphName(GraphName)
            .BlueprintEditor(Editor)
            .Graph(Graph)
        ];

    FInjectedOverlay& Entry = InjectedGraphOverlays.Add(TWeakPtr<SGraphEditor>(GraphEditor));
    Entry.Host = GraphOverlay;
    Entry.Overlay = OverlayWidget;
    Entry.Graph = Graph;
    InjectionRetries.Remove(TWeakObjectPtr<UEdGraph>(Graph));
    ++InjectionCount;

    FN2CLogger::Get().Log(FString::Printf(TEXT("Overlay injected for graph '%s'"), *GraphName), EN2CLogSeverity::Debug);
    return true;
}

bool FN2CGraphOverlayInjector::HasLiveOverlay(const UEdGraph* Graph) const
{
    if (!Graph)
    {
        return false;
    }
    for (const TPair<TWeakPtr<SGraphEditor>, FInjectedOverlay>& Pair : InjectedGraphOverlays)
    {
        const TSharedPtr<SGraphEditor> GraphEditor = Pair.Key.Pin();
        if (GraphEditor.IsValid() && Pair.Value.Overlay.IsValid() && Pair.Value.Graph.Get() == Graph
            && GraphEditor->GetCurrentGraph() == Graph)
        {
            return true;
        }
    }
    return false;
}

bool FN2CGraphOverlayInjector::CheckLiveOverlay(const TSharedRef<SGraphEditor>& GraphEditor, const UEdGraph* Graph)
{
    const TWeakPtr<SGraphEditor> Key(GraphEditor);
    FInjectedOverlay* Existing = InjectedGraphOverlays.Find(Key);
    if (!Existing)
    {
        return false;
    }
    if (Existing->Overlay.IsValid() && Existing->Graph.Get() == Graph)
    {
        return true;
    }

    // An overlay for another graph (or a destroyed one): take it out of the graph editor before a new one goes in
    const TSharedPtr<SOverlay> Host = Existing->Host.Pin();
    const TSharedPtr<SN2CGraphOverlay> Old = Existing->Overlay.Pin();
    if (Host.IsValid() && Old.IsValid())
    {
        Host->RemoveSlot(Old.ToSharedRef());
    }
    InjectedGraphOverlays.Remove(Key);
    return false;
}

void FN2CGraphOverlayInjector::RemoveInjectedOverlays()
{
    for (const TPair<TWeakPtr<SGraphEditor>, FInjectedOverlay>& Pair : InjectedGraphOverlays)
    {
        const TSharedPtr<SOverlay> Host = Pair.Value.Host.Pin();
        const TSharedPtr<SN2CGraphOverlay> Overlay = Pair.Value.Overlay.Pin();
        if (Host.IsValid() && Overlay.IsValid())
        {
            Host->RemoveSlot(Overlay.ToSharedRef());
        }
    }
    InjectedGraphOverlays.Empty();
    InjectionRetries.Empty();
}

void FN2CGraphOverlayInjector::RecordInjectionFailure(const UEdGraph* Graph, const TCHAR* Why)
{
    if (!Graph)
    {
        return;
    }
    FInjectionRetry& Retry = InjectionRetries.FindOrAdd(TWeakObjectPtr<UEdGraph>(const_cast<UEdGraph*>(Graph)));
    ++Retry.Failures;
    // 2 s, 4 s, 8 s ... between the poll's attempts, at most 60 s
    const double Delay = FMath::Min(60.0, 2.0 * FMath::Pow(2.0, static_cast<double>(FMath::Min(Retry.Failures - 1, 5))));
    Retry.NextAttemptTime = FPlatformTime::Seconds() + Delay;

    if (Retry.Failures == MaxPollInjectionAttempts)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("No NodeToCode overlay for graph '%s' (%s): stopped retrying after %d attempts; bringing its tab to the front tries again"),
                *Graph->GetName(), Why, Retry.Failures),
            EN2CLogSeverity::Info);
    }
    else
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("No NodeToCode overlay for graph '%s' yet (%s), attempt %d"), *Graph->GetName(), Why, Retry.Failures),
            EN2CLogSeverity::Debug);
    }
}

bool FN2CGraphOverlayInjector::IsInjectionDue(const UEdGraph* Graph, double Now) const
{
    const FInjectionRetry* Retry = InjectionRetries.Find(TWeakObjectPtr<UEdGraph>(const_cast<UEdGraph*>(Graph)));
    return !Retry || (Retry->Failures < MaxPollInjectionAttempts && Now >= Retry->NextAttemptTime);
}

bool FN2CGraphOverlayInjector::InjectForFocusedGraph(TWeakPtr<FBlueprintEditor> WeakEditor)
{
    TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
    if (!Editor.IsValid())
    {
        return false;
    }

    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
    if (!FocusedGraph)
    {
        return false;
    }

    // Only the graph editor that shows the focused graph. The old fallback took the first other graph with an open
    // tab and put this graph's overlay there (review editor-integration-10): when nothing shows the graph, wait
    const TSharedPtr<SGraphEditor> GraphEditor = FindGraphEditorFor(Editor, FocusedGraph);
    if (!GraphEditor.IsValid())
    {
        RecordInjectionFailure(FocusedGraph, TEXT("no graph editor in its Blueprint editor shows it"));
        return false;
    }
    return InjectOverlay(GraphEditor.ToSharedRef(), FocusedGraph, WeakEditor);
}

void FN2CGraphOverlayInjector::InjectMissingOverlays(bool bRespectBackoff)
{
    // Each open Blueprint editor's focused graph without an overlay gets one (not only the focused editor). Overlay
    // upkeep only: a background editor's rebuilt tab must not take the focus, so the tracker is not told (card 0150)
    const double Now = FPlatformTime::Seconds();
    for (const TSharedPtr<FBlueprintEditor>& Editor : FN2CBlueprintEditorTracker::GetOpenBlueprintEditors())
    {
        UEdGraph* FocusedGraph = Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
        if (!FocusedGraph || HasLiveOverlay(FocusedGraph) || (bRespectBackoff && !IsInjectionDue(FocusedGraph, Now)))
        {
            continue;
        }
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Overlay pass: injecting the overlay for graph '%s'"), *FocusedGraph->GetName()),
            EN2CLogSeverity::Debug);
        InjectForFocusedGraph(Editor);
    }
}

void FN2CGraphOverlayInjector::PruneStaleEntries()
{
    // Graph editors closed or rebuilt, graphs of closed Blueprints
    for (auto It = InjectedGraphOverlays.CreateIterator(); It; ++It)
    {
        if (!It->Key.IsValid() || !It->Value.Overlay.IsValid() || !It->Value.Graph.IsValid())
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = InjectionRetries.CreateIterator(); It; ++It)
    {
        if (!It->Key.IsValid())
        {
            It.RemoveCurrent();
        }
    }
}

void FN2CGraphOverlayInjector::PollOverlays()
{
    PruneStaleEntries();
    if (!GEditor || !N2COverlayInjectionPrivate::CVarOverlayPoll.GetValueOnGameThread())
    {
        return;
    }
    InjectMissingOverlays(true);
}

void FN2CGraphOverlayInjector::HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
    UBlueprint* OpenedBlueprint = Cast<UBlueprint>(Asset);
    if (!OpenedBlueprint || !EditorInstance || !GEditor)
    {
        return;
    }

    // Only real Blueprint editors: some UBlueprint subclasses open in their own toolkits. The newly opened editor
    // becomes the focused one through FN2CBlueprintEditorTracker, which hears the same event
    const TSharedPtr<FBlueprintEditor> BlueprintEditor = FN2CBlueprintEditorTracker::GetEditorFor(OpenedBlueprint);
    if (!BlueprintEditor.IsValid())
    {
        return;
    }
    const TWeakPtr<FBlueprintEditor> WeakEditor(BlueprintEditor);

    // Deferred, with retries: a Widget Blueprint may open in Designer mode, where GetFocusedGraph() is null until the
    // user switches to Graph. A later editor opening restarts this for that editor (the tab events and the poll still
    // cover the earlier one)
    GEditor->GetTimerManager()->ClearTimer(InitialInjectTimerHandle);
    InitialInjectRetryCount = 0;
    GEditor->GetTimerManager()->SetTimer(
        InitialInjectTimerHandle,
        FTimerDelegate::CreateLambda([this, WeakEditor]()
        {
            if (InitialInjectRetryCount == 0)
            {
                PruneStaleEntries();
            }

            const TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
            if (Editor.IsValid() && Editor->GetFocusedGraph())
            {
                InjectForFocusedGraph(WeakEditor);
            }
            else if (Editor.IsValid() && ++InitialInjectRetryCount < MaxInitialInjectRetries)
            {
                return; // no focused graph yet: try again
            }
            else if (Editor.IsValid())
            {
                // The overlay goes in when the user switches to a graph (tab event or poll)
                FN2CLogger::Get().Log(
                    FString::Printf(TEXT("Initial overlay injection: stopped after %d retries (editor likely in non-graph mode)"), MaxInitialInjectRetries),
                    EN2CLogSeverity::Debug);
            }
            if (GEditor)
            {
                GEditor->GetTimerManager()->ClearTimer(InitialInjectTimerHandle);
            }
        }),
        InitialInjectRetryInterval,
        true);
}

void FN2CGraphOverlayInjector::HandleTabForegrounded(TSharedPtr<SDockTab> NewlyForegrounded, TSharedPtr<SDockTab> Backgrounded)
{
    // FGlobalTabmanager broadcasts TabForegrounded(NewForegroundTab, BackgroundedTab). Only graph tabs coming to the
    // front or going behind: Details, Palette and other tabs schedule nothing
    const auto IsGraphTab = [](const TSharedPtr<SDockTab>& Tab)
    {
        return Tab.IsValid() && N2COverlayInjectionPrivate::IsExactly(*Tab->GetContent(), N2COverlayInjectionPrivate::GraphEditorType);
    };
    if (!GEditor || (!IsGraphTab(NewlyForegrounded) && !IsGraphTab(Backgrounded)))
    {
        return;
    }

    // Shortly after, so mode switches (a Widget Blueprint's Designer -> Graph) complete first. A tab event always
    // tries, also for a graph the poll gave up on
    GEditor->GetTimerManager()->SetTimer(
        TabChangeInjectTimerHandle,
        FTimerDelegate::CreateLambda([this]() { InjectMissingOverlays(false); }),
        TabChangeInjectDelay,
        false);
}
