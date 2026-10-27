// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "Engine/TimerHandle.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FBlueprintEditor;
class IAssetEditorInstance;
class SDockTab;
class SGraphEditor;
class SN2CGraphOverlay;
class SOverlay;
class UEdGraph;

/**
 * Puts the NodeToCode overlay (SN2CGraphOverlay) into Blueprint graph editors (card 0344).
 *
 * The one place that walks the engine's graph editor widgets, which differ across UE 5.6-5.8 (commit d3113e2):
 * - Into the exact SGraphEditor that shows the graph (FindGraphEditorFor), into the SOverlay around its graph panel
 *   (FindGraphEditorOverlay), one overlay per graph editor widget (card 0293).
 * - Triggers: a Blueprint editor opening (retried every 0.5 s until it shows a graph), a graph tab coming to the front,
 *   and a 2 s poll (n2c.OverlayPoll) for a graph that changes inside an existing tab. The poll backs off per graph and
 *   gives up after MaxPollInjectionAttempts; a tab brought to the front always tries again.
 * - Editors come from FN2CBlueprintEditorTracker; injection never changes which editor is focused (card 0150).
 * - Shutdown takes every overlay out again (SOverlay::RemoveSlot).
 */
class FN2CGraphOverlayInjector
{
public:
    static FN2CGraphOverlayInjector& Get();

    /** Subscribes to editor-open and tab-foregrounded events and starts the poll */
    void Initialize();

    /** Stops the timers, unsubscribes and removes every injected overlay */
    void Shutdown();

    /**
     * Injects the overlay into the graph editor a document tab holds, if it has none yet. Only a tab whose content is
     * exactly an SGraphEditor showing Graph qualifies (card 0293): a tab showing another graph is refused.
     * @return true when that graph editor has an overlay afterwards
     */
    bool InjectIntoTab(TSharedPtr<SDockTab> Tab, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor);

    /**
     * Injects the overlay into the graph editor that shows the editor's focused graph: its own document tab, else
     * SGraphEditor::FindGraphEditorForGraph when that widget sits in this editor's tabs. Nothing else: when no such
     * graph editor exists the attempt is recorded and the poll retries it later with a backoff (card 0293).
     * @return true when the focused graph's editor has an overlay afterwards
     */
    bool InjectForFocusedGraph(TWeakPtr<FBlueprintEditor> Editor);

    /**
     * The body of every injection pass (the poll, a tab coming to the front): each open Blueprint editor's focused graph
     * without a live overlay gets one. bRespectBackoff: skip graphs that are backing off or given up (the poll); a tab
     * event passes false and always tries.
     */
    void InjectMissingOverlays(bool bRespectBackoff);

    /**
     * Whether a graph editor showing this graph object carries a live N2C overlay. Overlays are tracked per SGraphEditor
     * widget, not per GraphGuid: copies of a Blueprint made in the Content Browser keep the original's graph GUIDs, and
     * each copy's graph needs its own (card 0192).
     */
    bool HasLiveOverlay(const UEdGraph* Graph) const;

    /** Overlays injected since the editor started (each SN2CGraphOverlay built and added counts once) */
    int32 GetInjectionCount() const { return InjectionCount; }

    /**
     * The graph editor that shows Graph inside Editor (exact SGraphEditor type, current graph is Graph), or null.
     * Looks in Editor's document tabs for Graph first, then SGraphEditor::FindGraphEditorForGraph limited to widgets in
     * Editor's own tabs (a diff window can show the same graph).
     */
    static TSharedPtr<SGraphEditor> FindGraphEditorFor(const TSharedPtr<FBlueprintEditor>& Editor, const UEdGraph* Graph);

    /** Removes every injected overlay from its graph editor (SOverlay::RemoveSlot) and forgets them. Run by Shutdown */
    void RemoveInjectedOverlays();

private:
    FN2CGraphOverlayInjector() = default;

    /** One injected overlay: the SOverlay slot it sits in and the graph it acts on */
    struct FInjectedOverlay
    {
        TWeakPtr<SOverlay> Host;
        TWeakPtr<SN2CGraphOverlay> Overlay;
        TWeakObjectPtr<UEdGraph> Graph;
    };

    /**
     * The injected overlays, keyed by the SGraphEditor widget they sit in (card 0293), so one graph editor never gets
     * two. Weak: a closed tab or a rebuilt graph editor leaves a stale entry, removed on the next look.
     */
    TMap<TWeakPtr<SGraphEditor>, FInjectedOverlay> InjectedGraphOverlays;

    /**
     * True if GraphEditor carries a live overlay for Graph. Drops a stale entry, and removes an overlay left in a graph
     * editor that now shows another graph.
     */
    bool CheckLiveOverlay(const TSharedRef<SGraphEditor>& GraphEditor, const UEdGraph* Graph);

    /** Injects the overlay into GraphEditor (which must show Graph) unless it has one */
    bool InjectOverlay(const TSharedRef<SGraphEditor>& GraphEditor, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor);

    /**
     * The SOverlay SGraphEditorImpl puts around its graph panel (SGraphEditor > SBox > SGraphEditorImpl > SOverlay):
     * found by walking up from the graph panel, with a recursive search (logged at Debug) as the fallback.
     */
    static TSharedPtr<SOverlay> FindGraphEditorOverlay(const TSharedRef<SGraphEditor>& GraphEditor);

    /** Failed injection attempts of a graph: the poll backs off between them and gives up after a limit */
    struct FInjectionRetry
    {
        int32 Failures = 0;
        double NextAttemptTime = 0.0;
    };
    TMap<TWeakObjectPtr<UEdGraph>, FInjectionRetry> InjectionRetries;

    /** Records a failed attempt for Graph (logged at Debug; once at Info when the poll gives up) */
    void RecordInjectionFailure(const UEdGraph* Graph, const TCHAR* Why);

    /** Whether the poll may try Graph now (not backing off, not given up) */
    bool IsInjectionDue(const UEdGraph* Graph, double Now) const;

    /** Drops entries for closed or rebuilt graph editors and for graphs that no longer exist */
    void PruneStaleEntries();

    /** The 2 s poll: prune, then InjectMissingOverlays(true) unless n2c.OverlayPoll is 0 */
    void PollOverlays();

    /** A Blueprint editor opened: inject into its focused graph once it has one (retried every 0.5 s) */
    void HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance);

    /** A graph tab came to the front or went behind: an injection pass shortly after (mode switches settle first) */
    void HandleTabForegrounded(TSharedPtr<SDockTab> NewlyForegrounded, TSharedPtr<SDockTab> Backgrounded);

    /** Attempts before the poll gives up on a graph (an event, a tab coming to the front, still tries) */
    static constexpr int32 MaxPollInjectionAttempts = 6;

    /** Seconds between poll passes */
    static constexpr float PollInterval = 2.0f;

    /** Retries of the editor-open injection (for editors that open without a focused graph, e.g. a Widget Blueprint's Designer) */
    static constexpr int32 MaxInitialInjectRetries = 20;

    /** Seconds between editor-open injection retries */
    static constexpr float InitialInjectRetryInterval = 0.5f;

    /** Delay after a tab event before the injection pass */
    static constexpr float TabChangeInjectDelay = 0.1f;

    FTimerHandle InitialInjectTimerHandle;
    FTimerHandle TabChangeInjectTimerHandle;
    FTimerHandle PollTimerHandle;
    int32 InitialInjectRetryCount = 0;
    int32 InjectionCount = 0;

    FDelegateHandle OnTabForegroundedHandle;
    FDelegateHandle OnAssetOpenedInEditorHandle;
};
