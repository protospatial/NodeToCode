// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CCapture.h"
#include "Utils/N2CLogger.h"
#include "LLM/IN2CLLMService.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CLLMTypes.h"
#include "Models/N2CBlueprint.h"

// Forward declarations
class SN2CGraphEditorWrapper;
class SDockTab;
class SN2CGraphOverlay;
class SGraphEditor;
class SOverlay;

/**
 * Delegate for when translation state changes globally
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CTranslationStateChanged, bool /* bIsTranslating */);

/**
 * Delegate for when a graph overlay requests translation
 * The main window can subscribe to show progress modal
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnN2COverlayTranslationRequested, const FGuid& /* GraphGuid */, const FString& /* GraphName */, const FString& /* BlueprintPath */);

/**
 * Provider, model and language overrides for one translation, resolved against the IDs that
 * get-available-llm-providers and get-available-translation-targets offer (lowercase, matched case-insensitively).
 * An unset Provider or Language means "use the settings default".
 */
struct FN2CTranslationOverrides
{
    TOptional<EN2CLLMProvider> Provider;
    TOptional<EN2CCodeLanguage> Language;
    FString ModelId;

    /** Things the caller should hear about but that do not stop the translation (an unrecognised model ID) */
    TArray<FString> Warnings;
};

/**
 * @class FN2CEditorIntegration
 * @brief Handles integration with the Blueprint Editor
 *
 * Manages Blueprint Editor toolbar extensions and provides
 * access to the active Blueprint Editor instance.
 */
class FN2CEditorIntegration
{
public:
    static FN2CEditorIntegration& Get();

    /** Initialize integration with Blueprint Editor */
    void Initialize();

    /** Cleanup integration */
    void Shutdown();

    /** Get available themes for a language */
    TArray<FName> GetAvailableThemes(EN2CCodeLanguage Language) const;

    /** Get the default theme for a language */
    FName GetDefaultTheme(EN2CCodeLanguage Language) const;

    /**
     * Marks Editor as the one the user activated (an overlay button, the toolbar commands). Forwards to
     * FN2CBlueprintEditorTracker::NotifyEditorActivated.
     */
    void StoreActiveBlueprintEditor(TWeakPtr<FBlueprintEditor> Editor);

    /**
     * The focused Blueprint editor: the one the user last activated (FN2CBlueprintEditorTracker::GetFocusedEditor,
     * which falls back to the most recently activated editor still open when that one closes)
     */
    TSharedPtr<FBlueprintEditor> GetActiveBlueprintEditor() const;

    /** The graph the focused Blueprint editor shows (FN2CBlueprintEditorTracker::GetFocusedGraph) */
    UEdGraph* GetFocusedGraphFromActiveEditor() const;
    
    /** Collects K2Nodes from a given UEdGraph */
    bool CollectNodesFromGraph(UEdGraph* Graph, TArray<UK2Node*>& OutNodes) const;

    /**
     * Captures an array of K2Nodes into an FN2CBlueprint structure (a thin wrapper over N2C::CaptureNodes). On failure
     * OutError, when given, receives why: the capture's own error (for a graph of only ghost default events, "Nothing
     * to capture: the graph holds only unwired default events (ghost nodes)") or that the result does not validate.
     */
    bool TranslateNodesToN2CBlueprint(const TArray<UK2Node*>& CollectedNodes, FN2CBlueprint& OutN2CBlueprint,
        FString* OutError = nullptr) const;

    /**
     * Captures an array of K2Nodes with the capture's ID-to-GUID maps (OutCapture.NodeGuids and Pins: one entry per
     * captured node and pin). False when nothing was captured or the result does not validate; OutError, when given,
     * then says which, as for TranslateNodesToN2CBlueprint.
     */
    bool TranslateNodesToN2CBlueprintWithMaps(const TArray<UK2Node*>& CollectedNodes, FN2CCapture& OutCapture,
        const FN2CCaptureOptions& Options = FN2CCaptureOptions(), FString* OutError = nullptr) const;
    
    /** Serializes an FN2CBlueprint structure to a JSON string */
    FString SerializeN2CBlueprintToJson(const FN2CBlueprint& Blueprint, bool bPrettyPrint) const;

    /** New high-level function to be called by the MCP tool handler
     * Returns N2CJSON string for the focused Blueprint, or an empty string on failure.
     * Populates OutErrorMsg if an error occurs. OutCapture, when given, receives the Blueprint the JSON was written from.
     */
    FString GetFocusedBlueprintAsJson(bool bPrettyPrint, FString& OutErrorMsg, FN2CBlueprint* OutCapture = nullptr);

    /**
     * GetFocusedBlueprintAsJson's work on one graph: collect, capture (ghost default events left out, card 0137) and
     * serialize. Empty on failure with OutErrorMsg set; a graph of only ghost default events fails with the capture's
     * own message ("Nothing to capture: the graph holds only unwired default events (ghost nodes)", card 0433).
     */
    FString GetGraphAsJson(UEdGraph* Graph, bool bPrettyPrint, FString& OutErrorMsg, FN2CBlueprint* OutCapture = nullptr) const;

    /** Simple wrapper function that translates the focused Blueprint graph using the latest active editor */
    void TranslateFocusedBlueprintGraph();

    /**
     * Translates the focused Blueprint graph using specified or default LLM settings. Game thread only: collects the
     * graph JSON, sends the request and returns at once. OnComplete runs later on the game thread (or inside this call
     * for an error found before sending).
     * @param ProviderIdOverride Optional override for the LLM provider ID.
     * @param ModelIdOverride Optional override for the LLM model ID.
     * @param LanguageIdOverride Optional override for the target language ID.
     * @param OnComplete Delegate called with the translation result (JSON string) or an error message.
     * @return The request's handle; Cancel() stops the request and OnComplete.
     */
    FN2CLLMRequestHandleRef TranslateFocusedBlueprintAsync(
        const FString& ProviderIdOverride,
        const FString& ModelIdOverride,
        const FString& LanguageIdOverride,
        FOnLLMResponseReceived OnComplete
    );

    /**
     * Sends already collected N2C JSON for translation with the same overrides as TranslateFocusedBlueprintAsync.
     * Game thread only. Builds the request config from the settings and calls ProcessN2CJsonWithOverrides.
     */
    FN2CLLMRequestHandleRef TranslateJsonAsync(
        const FString& JsonInput,
        const FString& ProviderIdOverride,
        const FString& ModelIdOverride,
        const FString& LanguageIdOverride,
        FOnLLMResponseReceived OnComplete
    );

    /**
     * Resolves translation overrides. Provider and language IDs match the lowercase IDs the MCP listing tools return,
     * ignoring case; empty means the settings default. An unknown provider or language fails with OutError naming
     * the valid IDs. A model ID that the model registry does not list for a cloud provider is still used, with an
     * entry in OutOverrides.Warnings.
     * @return false when a provider or language ID is unknown
     */
    static bool ResolveTranslationOverrides(const FString& ProviderId, const FString& ModelId, const FString& LanguageId,
        FN2CTranslationOverrides& OutOverrides, FString& OutError);

    /** Provider IDs a translation accepts (lowercase enum names, as get-available-llm-providers returns them) */
    static TArray<FString> GetTranslationProviderIds();

    /** Language IDs a translation accepts (lowercase, as get-available-translation-targets returns them) */
    static TArray<FString> GetTranslationLanguageIds();

    /**
     * Injects the NodeToCode overlay into the graph editor a document tab holds, if it has none yet. Only a tab whose
     * content is exactly an SGraphEditor showing Graph qualifies (card 0293): a tab showing another graph is refused.
     * @param Tab The dock tab holding the graph editor
     * @param Graph The graph the tab shows
     * @param Editor The Blueprint editor instance
     * @return true when that graph editor has an overlay afterwards
     */
    bool WrapGraphTabIfNeeded(TSharedPtr<SDockTab> Tab, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor);

    /**
     * Injects the overlay into the graph editor that shows the editor's focused graph: its own document tab, else
     * SGraphEditor::FindGraphEditorForGraph when that widget sits in this editor's tabs. Nothing else: when no such
     * graph editor exists the attempt is recorded and the poll retries it later with a backoff (card 0293).
     * @param Editor The Blueprint editor to check
     * @return true when the focused graph's editor has an overlay afterwards
     */
    bool TryWrapFocusedGraphTab(TWeakPtr<FBlueprintEditor> Editor);

    /**
     * Clean up any stale wrapper references
     */
    void CleanupStaleWrappers();

    /**
     * Whether a graph editor showing this graph object carries a live N2C overlay. Overlays are tracked per SGraphEditor
     * widget, not per GraphGuid: copies of a Blueprint made in the Content Browser keep the original's graph GUIDs, and
     * each copy's graph needs its own.
     */
    bool HasLiveOverlay(const UEdGraph* Graph) const;

    /**
     * The graph editor that shows Graph inside Editor (exact SGraphEditor type, current graph is Graph), or null.
     * Looks in Editor's document tabs for Graph first, then SGraphEditor::FindGraphEditorForGraph limited to widgets in
     * Editor's own tabs (a diff window can show the same graph).
     */
    static TSharedPtr<SGraphEditor> FindGraphEditorFor(const TSharedPtr<FBlueprintEditor>& Editor, const UEdGraph* Graph);

    /** Removes every injected overlay from its graph editor (SOverlay::RemoveSlot) and forgets them. Run by Shutdown */
    void RemoveInjectedOverlays();

    // ==================== Global Translation State ====================

    /**
     * Check if any translation is currently in progress
     * This includes both single graph translations and batch translations
     */
    bool IsAnyTranslationInProgress() const { return bIsAnyTranslationInProgress; }

    /**
     * Set the global translation state
     * Called by overlays, main window, etc. when starting/completing translations
     */
    void SetTranslationInProgress(bool bInProgress);

    /**
     * Request a translation from a graph overlay
     * This broadcasts to the main window to show progress modal
     */
    void RequestOverlayTranslation(const FGuid& GraphGuid, const FString& GraphName, const FString& BlueprintPath);

    /** Delegate fired when global translation state changes */
    FOnN2CTranslationStateChanged OnTranslationStateChanged;

    /** Delegate fired when a graph overlay requests translation */
    FOnN2COverlayTranslationRequested OnOverlayTranslationRequested;

private:
    /** Constructor */
    FN2CEditorIntegration() = default;

public:
    /** Get Blueprint Editor from active tab */
    TSharedPtr<FBlueprintEditor> GetBlueprintEditorFromTab() const;

    /** Register for Blueprint Editor callbacks */
    void RegisterBlueprintEditorCallback();

private:
    /** Map of wrapped tabs to their wrapper widgets (legacy, kept for compatibility) */
    TMap<TWeakPtr<SDockTab>, TSharedPtr<SN2CGraphEditorWrapper>> WrappedTabs;

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

    /** The poll's body: injects an overlay for each open Blueprint editor's focused graph that lacks one */
    void PollOverlays();

    /** Attempts before the poll gives up on a graph (an event, a tab coming to the front, still tries) */
    static constexpr int32 MaxPollInjectionAttempts = 6;

    /** Timer handle for initial graph tab wrapping on editor open (with retry) */
    FTimerHandle InitialWrapTimerHandle;

    /** Timer handle for tab-change-triggered graph tab wrapping */
    FTimerHandle TabChangeWrapTimerHandle;

    /** Timer handle for periodic overlay health check (catches internal graph switches) */
    FTimerHandle OverlayPollingTimerHandle;

    /** Retry counter for initial wrap attempts (for editors that open without a focused graph) */
    int32 InitialWrapRetryCount = 0;

    /** Max retries before giving up on initial wrap */
    static constexpr int32 MaxInitialWrapRetries = 20;

    /** Interval between initial wrap retry attempts in seconds */
    static constexpr float InitialWrapRetryInterval = 0.5f;

    /** Delegate handle for tab foregrounded subscription */
    FDelegateHandle OnTabForegroundedHandle;

    /** Handle for the OnAssetOpenedInEditor subscription */
    FDelegateHandle OnAssetEditorOpenedHandle;

    /** Global flag tracking if any translation is in progress */
    bool bIsAnyTranslationInProgress = false;

    /** Handle tab foregrounded to keep graph overlays injected (the broadcast passes the new foreground tab first) */
    void OnTabForegrounded(TSharedPtr<SDockTab> NewlyForegrounded, TSharedPtr<SDockTab> Backgrounded);

    /**
     * The SOverlay SGraphEditorImpl puts around its graph panel (SGraphEditor > SBox > SGraphEditorImpl > SOverlay):
     * found by walking up from the graph panel, with a recursive search (logged at Debug) as the fallback.
     */
    static TSharedPtr<SOverlay> FindGraphEditorOverlay(const TSharedRef<SGraphEditor>& GraphEditor);

    /** Execute collect nodes for a specific editor */
    void TranslateBlueprintNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute copy blueprint JSON to clipboard for a specific editor */
    void ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor);
    
    /** Handle asset editor opened callback */
    void HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance);

};
