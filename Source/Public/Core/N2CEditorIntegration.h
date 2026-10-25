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

    /** Called by HandleAssetEditorOpened to update the active editor */
    void StoreActiveBlueprintEditor(TWeakPtr<FBlueprintEditor> Editor);
    
    /** Retrieves the last stored active Blueprint editor */
    TSharedPtr<FBlueprintEditor> GetActiveBlueprintEditor() const;

    /** Retrieves the focused graph from the stored active editor */
    UEdGraph* GetFocusedGraphFromActiveEditor() const;
    
    /** Collects K2Nodes from a given UEdGraph */
    bool CollectNodesFromGraph(UEdGraph* Graph, TArray<UK2Node*>& OutNodes) const;

    /** Captures an array of K2Nodes into an FN2CBlueprint structure (a thin wrapper over N2C::CaptureNodes) */
    bool TranslateNodesToN2CBlueprint(const TArray<UK2Node*>& CollectedNodes, FN2CBlueprint& OutN2CBlueprint) const;
    
    /**
     * Captures an array of K2Nodes with the capture's ID-to-GUID maps (OutCapture.NodeGuids and Pins: one entry per
     * captured node and pin). False when nothing was captured or the result does not validate.
     */
    bool TranslateNodesToN2CBlueprintWithMaps(const TArray<UK2Node*>& CollectedNodes, FN2CCapture& OutCapture) const;
    
    /** Serializes an FN2CBlueprint structure to a JSON string */
    FString SerializeN2CBlueprintToJson(const FN2CBlueprint& Blueprint, bool bPrettyPrint) const;

    /** New high-level function to be called by the MCP tool handler
     * Returns N2CJSON string for the focused Blueprint, or an empty string on failure.
     * Populates OutErrorMsg if an error occurs. OutCapture, when given, receives the Blueprint the JSON was written from.
     */
    FString GetFocusedBlueprintAsJson(bool bPrettyPrint, FString& OutErrorMsg, FN2CBlueprint* OutCapture = nullptr);

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
     * Wrap a graph tab with the NodeToCode overlay if not already wrapped
     * @param Tab The dock tab to wrap
     * @param Graph The graph being displayed in the tab
     * @param Editor The Blueprint editor instance
     */
    void WrapGraphTabIfNeeded(TSharedPtr<SDockTab> Tab, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor);

    /**
     * Try to wrap the currently focused graph tab in the given editor
     * @param Editor The Blueprint editor to check
     */
    void TryWrapFocusedGraphTab(TWeakPtr<FBlueprintEditor> Editor);

    /**
     * Clean up any stale wrapper references
     */
    void CleanupStaleWrappers();

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
    /** The currently active Blueprint editor */
    TWeakPtr<FBlueprintEditor> ActiveBlueprintEditor;

    /** Map of wrapped tabs to their wrapper widgets (legacy, kept for compatibility) */
    TMap<TWeakPtr<SDockTab>, TSharedPtr<SN2CGraphEditorWrapper>> WrappedTabs;

    /** Map of graph GUIDs to their overlay widgets (weak ptrs detect destruction on graph switch) */
    TMap<FGuid, TWeakPtr<SN2CGraphOverlay>> InjectedGraphOverlays;

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

    /** Handle for the OnAssetEditorOpened subscription (a lambda, so RemoveAll(this) cannot remove it) */
    FDelegateHandle OnAssetEditorOpenedHandle;

    /** Global flag tracking if any translation is in progress */
    bool bIsAnyTranslationInProgress = false;

    /** Handle tab foregrounded to wrap new graph tabs (more reliable than OnActiveTabChanged) */
    void OnTabForegrounded(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyForegrounded);

    /**
     * Find the SOverlay inside an SGraphEditor widget using the known widget tree structure.
     * Uses targeted traversal first (SGraphEditorImpl → SOverlay), with recursive fallback.
     */
    TSharedPtr<SOverlay> FindGraphEditorOverlay(TSharedPtr<SWidget> GraphEditorWidget);

    /** Execute collect nodes for a specific editor */
    void TranslateBlueprintNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute copy blueprint JSON to clipboard for a specific editor */
    void ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor);
    
    /** Handle asset editor opened callback */
    void HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance);

};
