// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CEditorIntegration.h"
#include "Core/N2CBlueprintEditorTracker.h"

#include "BlueprintEditorModes.h"
#include "Core/N2CNodeCollector.h"
#include "BlueprintEditorModule.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CEditorWindow.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "Core/Widgets/SN2CGraphEditorWrapper.h"
#include "Core/Widgets/SN2CGraphOverlay.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "Models/N2CTranslation.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "GraphEditor.h"
#include "SGraphPanel.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "TimerManager.h"
#include "Engine/Blueprint.h"

#include "HAL/PlatformApplicationMisc.h"

FN2CEditorIntegration& FN2CEditorIntegration::Get()
{
    static FN2CEditorIntegration Instance;
    return Instance;
}

void FN2CEditorIntegration::StoreActiveBlueprintEditor(TWeakPtr<FBlueprintEditor> Editor)
{
    FN2CBlueprintEditorTracker::Get().NotifyEditorActivated(Editor.Pin());
}

TSharedPtr<FBlueprintEditor> FN2CEditorIntegration::GetActiveBlueprintEditor() const
{
    return FN2CBlueprintEditorTracker::Get().GetFocusedEditor();
}

UEdGraph* FN2CEditorIntegration::GetFocusedGraphFromActiveEditor() const
{
    return FN2CBlueprintEditorTracker::Get().GetFocusedGraph();
}

bool FN2CEditorIntegration::CollectNodesFromGraph(UEdGraph* Graph, TArray<UK2Node*>& OutNodes) const
{
    if (!Graph)
    {
        FN2CLogger::Get().LogError(TEXT("CollectNodesFromGraph: Graph is null"));
        return false;
    }

    FN2CNodeCollector& Collector = FN2CNodeCollector::Get();
    bool bSuccess = Collector.CollectNodesFromGraph(Graph, OutNodes);
    
    if (bSuccess)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("CollectNodesFromGraph: Successfully collected %d nodes"), OutNodes.Num()),
            EN2CLogSeverity::Info
        );
    }
    else
    {
        FN2CLogger::Get().LogError(TEXT("CollectNodesFromGraph: Failed to collect nodes"));
    }
    
    return bSuccess;
}

bool FN2CEditorIntegration::TranslateNodesToN2CBlueprint(const TArray<UK2Node*>& CollectedNodes, FN2CBlueprint& OutN2CBlueprint,
    FString* OutError) const
{
    TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(CollectedNodes);
    if (Capture.HasValue())
    {
        OutN2CBlueprint = MoveTemp(Capture.GetValue().Blueprint);

        // Validate the result
        bool bIsValid = OutN2CBlueprint.IsValid();

        if (bIsValid)
        {
            FN2CLogger::Get().Log(TEXT("TranslateNodesToN2CBlueprint: Translation validation successful"), EN2CLogSeverity::Info);
        }
        else
        {
            FN2CLogger::Get().LogError(TEXT("TranslateNodesToN2CBlueprint: Translation validation failed"));
            if (OutError)
            {
                *OutError = TEXT("The captured Blueprint did not validate.");
            }
        }

        return bIsValid;
    }

    // The capture says why (a graph of only ghost default events, card 0137), so callers can pass it on (card 0433)
    FN2CLogger::Get().LogError(FString::Printf(TEXT("TranslateNodesToN2CBlueprint: Failed to generate N2C structure: %s"), *Capture.GetError()));
    if (OutError)
    {
        *OutError = Capture.GetError();
    }
    return false;
}

bool FN2CEditorIntegration::TranslateNodesToN2CBlueprintWithMaps(const TArray<UK2Node*>& CollectedNodes, FN2CCapture& OutCapture,
    const FN2CCaptureOptions& Options, FString* OutError) const
{
    TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureNodes(CollectedNodes, Options);
    if (Capture.HasValue())
    {
        // The blueprint and its ID maps come from the same capture, which nothing else can change
        OutCapture = Capture.StealValue();

        // Validate the result
        bool bIsValid = OutCapture.Blueprint.IsValid();

        if (bIsValid)
        {
            FN2CLogger::Get().Log(TEXT("TranslateNodesToN2CBlueprintWithMaps: Translation validation successful"), EN2CLogSeverity::Info);
            FN2CLogger::Get().Log(FString::Printf(TEXT("TranslateNodesToN2CBlueprintWithMaps: Preserved %d node IDs and %d pin IDs"),
                OutCapture.NodeGuids.Num(), OutCapture.Pins.Num()), EN2CLogSeverity::Info);
        }
        else
        {
            FN2CLogger::Get().LogError(TEXT("TranslateNodesToN2CBlueprintWithMaps: Translation validation failed"));
            if (OutError)
            {
                *OutError = TEXT("The captured Blueprint did not validate.");
            }
        }

        return bIsValid;
    }

    FN2CLogger::Get().LogError(FString::Printf(TEXT("TranslateNodesToN2CBlueprintWithMaps: Failed to generate N2C structure: %s"), *Capture.GetError()));
    if (OutError)
    {
        *OutError = Capture.GetError();
    }
    return false;
}

FString FN2CEditorIntegration::SerializeN2CBlueprintToJson(const FN2CBlueprint& Blueprint, bool bPrettyPrint) const
{
    return FN2CSerializer::ToJson(Blueprint, bPrettyPrint ? EN2CJsonStyle::Pretty : EN2CJsonStyle::Condensed);
}

FString FN2CEditorIntegration::GetFocusedBlueprintAsJson(bool bPrettyPrint, FString& OutErrorMsg, FN2CBlueprint* OutCapture)
{
    // Get the active Blueprint editor
    TSharedPtr<FBlueprintEditor> Editor = GetActiveBlueprintEditor();
    if (!Editor.IsValid())
    {
        OutErrorMsg = TEXT("No active Blueprint Editor.");
        return FString();
    }

    // Get the focused graph
    UEdGraph* FocusedGraph = GetFocusedGraphFromActiveEditor();
    if (!FocusedGraph)
    {
        OutErrorMsg = TEXT("No focused graph in the active Blueprint Editor.");
        return FString();
    }

    return GetGraphAsJson(FocusedGraph, bPrettyPrint, OutErrorMsg, OutCapture);
}

FString FN2CEditorIntegration::GetGraphAsJson(UEdGraph* Graph, bool bPrettyPrint, FString& OutErrorMsg, FN2CBlueprint* OutCapture) const
{
    // Collect nodes from the graph
    TArray<UK2Node*> CollectedNodes;
    if (!CollectNodesFromGraph(Graph, CollectedNodes) || CollectedNodes.IsEmpty())
    {
        OutErrorMsg = TEXT("Failed to collect nodes or no nodes found in the focused graph.");
        return FString();
    }

    // Translate nodes to N2CBlueprint structure. Ghost default events are left out (card 0137); a graph of nothing
    // else fails with the capture's own message rather than a generic one (card 0433)
    FN2CBlueprint N2CBlueprintData;
    FString CaptureError;
    if (!TranslateNodesToN2CBlueprint(CollectedNodes, N2CBlueprintData, &CaptureError))
    {
        OutErrorMsg = CaptureError.IsEmpty() ? FString(TEXT("Failed to translate collected nodes into N2CBlueprint structure.")) : CaptureError;
        return FString();
    }

    // Serialize to JSON
    FString JsonOutput = SerializeN2CBlueprintToJson(N2CBlueprintData, bPrettyPrint);
    if (JsonOutput.IsEmpty())
    {
        OutErrorMsg = TEXT("Failed to serialize N2CBlueprint to JSON.");
        return FString();
    }

    if (OutCapture)
    {
        *OutCapture = MoveTemp(N2CBlueprintData);
    }

    OutErrorMsg = TEXT("Success.");
    return JsonOutput;
}

void FN2CEditorIntegration::ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    FN2CLogger::Get().Log(TEXT("ExecuteCopyJsonForEditor called"), EN2CLogSeverity::Debug);

    // Store the editor as active
    StoreActiveBlueprintEditor(InEditor);

    // Use the new helper method to get JSON with pretty printing for clipboard
    FString ErrorMsg;
    FString JsonOutput = GetFocusedBlueprintAsJson(true /* pretty print for clipboard */, ErrorMsg);

    if (JsonOutput.IsEmpty())
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to get Blueprint JSON: %s"), *ErrorMsg));
        return;
    }

    // Copy JSON to clipboard
    FPlatformApplicationMisc::ClipboardCopy(*JsonOutput);

    // Show notification
    FNotificationInfo Info(NSLOCTEXT("NodeToCode", "BlueprintJsonCopied", "Blueprint JSON copied to clipboard"));
    Info.bFireAndForget = true;
    Info.FadeInDuration = 0.2f;
    Info.FadeOutDuration = 0.5f;
    Info.ExpireDuration = 2.0f;
    FSlateNotificationManager::Get().AddNotification(Info);

    FN2CLogger::Get().Log(TEXT("Blueprint JSON copied to clipboard successfully"), EN2CLogSeverity::Info);
}

void FN2CEditorIntegration::Initialize()
{
    // Register tab spawner
    SN2CEditorWindow::RegisterTabSpawner();

    // Track the focused Blueprint editor from the user's tab and focus changes (card 0150)
    FN2CBlueprintEditorTracker::Get().Initialize();

    // Subscribe to asset editor opened events. OnAssetOpenedInEditor fires for every editor opened, also when several
    // assets open at once (OnAssetEditorOpened is not broadcast then), and passes the editor instance
    if (GEditor)
    {
        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (ensure(AssetEditorSubsystem))
        {
            OnAssetEditorOpenedHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(this, &FN2CEditorIntegration::HandleAssetEditorOpened);
            FN2CLogger::Get().Log(TEXT("N2C Editor Integration: Subscribed to OnAssetOpenedInEditor via AssetEditorSubsystem"), EN2CLogSeverity::Info);
        }
    }

    // Subscribe to tab foregrounded events - more reliable than OnActiveTabChanged.
    // OnTabForegrounded fires for both cross-editor tab switches AND internal graph switches
    // (e.g., EventGraph -> Function) within the same Blueprint editor.
    OnTabForegroundedHandle = FGlobalTabmanager::Get()->OnTabForegrounded_Subscribe(
        FOnActiveTabChanged::FDelegate::CreateRaw(this, &FN2CEditorIntegration::OnTabForegrounded)
    );
    FN2CLogger::Get().Log(TEXT("N2C Editor Integration: Subscribed to OnTabForegrounded"), EN2CLogSeverity::Info);

    // A safety net behind the editor-open and tab-foregrounded hooks: a graph opened inside an existing tab (history
    // navigation, a collapsed graph) or a rebuilt graph editor raises no tab event. Quiet (Debug), backs off per graph
    // and gives up after MaxPollInjectionAttempts; n2c.OverlayPoll 0 turns it off (card 0293)
    if (GEditor)
    {
        GEditor->GetTimerManager()->SetTimer(
            OverlayPollingTimerHandle,
            FTimerDelegate::CreateLambda([this]()
            {
                CleanupStaleWrappers();
                PollOverlays();
            }),
            2.0f,
            true);
    }

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration initialized"), EN2CLogSeverity::Info);
}

void FN2CEditorIntegration::Shutdown()
{
    // Clear both timer handles
    if (GEditor)
    {
        if (InitialWrapTimerHandle.IsValid())
        {
            GEditor->GetTimerManager()->ClearTimer(InitialWrapTimerHandle);
        }
        if (TabChangeWrapTimerHandle.IsValid())
        {
            GEditor->GetTimerManager()->ClearTimer(TabChangeWrapTimerHandle);
        }
    }

    // Unsubscribe from global tab foregrounded events
    if (OnTabForegroundedHandle.IsValid())
    {
        FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(OnTabForegroundedHandle);
        OnTabForegroundedHandle.Reset();
    }

    // Clear the overlay polling timer
    if (GEditor && OverlayPollingTimerHandle.IsValid())
    {
        GEditor->GetTimerManager()->ClearTimer(OverlayPollingTimerHandle);
    }

    // Unregister tab spawner
    SN2CEditorWindow::UnregisterTabSpawner();

    // Clear wrapped tabs (legacy)
    WrappedTabs.Empty();

    // Take the injected overlays out of the engine's graph editors: no widget of this module stays in their trees
    RemoveInjectedOverlays();

    // Unsubscribe from asset editor events
    if (GEditor)
    {
        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (AssetEditorSubsystem)
        {
            AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(OnAssetEditorOpenedHandle);
            OnAssetEditorOpenedHandle.Reset();
        }
    }

    FN2CBlueprintEditorTracker::Get().Shutdown();

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration shutdown"), EN2CLogSeverity::Info);
}


TSharedPtr<FBlueprintEditor> FN2CEditorIntegration::GetBlueprintEditorFromTab() const
{
    // Mark as deprecated
    FN2CLogger::Get().LogWarning(TEXT("GetBlueprintEditorFromTab is deprecated - editors should be accessed directly"));
    return nullptr;
}

void FN2CEditorIntegration::HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance)
{
    if (!Asset || !EditorInstance)
    {
        return;
    }

    // Check if the asset is a Blueprint or a child class of Blueprint
    UBlueprint* OpenedBlueprint = Cast<UBlueprint>(Asset);
    if (!OpenedBlueprint)
    {
        return; // Not a Blueprint, so ignore
    }

    // Only real Blueprint editors: some UBlueprint subclasses open in their own toolkits. The newly opened editor
    // becomes the focused one through FN2CBlueprintEditorTracker, which hears the same event
    TSharedPtr<FBlueprintEditor> BlueprintEditorShared = FN2CBlueprintEditorTracker::GetEditorFor(OpenedBlueprint);
    if (BlueprintEditorShared.IsValid())
    {
        TWeakPtr<FBlueprintEditor> WeakEditor(BlueprintEditorShared);

        // Schedule deferred wrapping of graph tabs with retry logic.
        // Widget Blueprints may open in Designer mode where GetFocusedGraph() returns nullptr.
        // The looping timer retries until the graph becomes available or max retries is reached.
        if (GEditor)
        {
            // Clear any existing initial wrap timer (does NOT cancel the tab-change timer)
            if (InitialWrapTimerHandle.IsValid())
            {
                GEditor->GetTimerManager()->ClearTimer(InitialWrapTimerHandle);
            }

            // Reset retry counter
            InitialWrapRetryCount = 0;

            GEditor->GetTimerManager()->SetTimer(
                InitialWrapTimerHandle,
                FTimerDelegate::CreateLambda([this, WeakEditor]()
                {
                    // Clean up stale wrappers on first attempt
                    if (InitialWrapRetryCount == 0)
                    {
                        CleanupStaleWrappers();
                    }

                    TSharedPtr<FBlueprintEditor> Editor = WeakEditor.Pin();
                    if (!Editor.IsValid())
                    {
                        // Editor was closed, stop retrying
                        if (GEditor && InitialWrapTimerHandle.IsValid())
                        {
                            GEditor->GetTimerManager()->ClearTimer(InitialWrapTimerHandle);
                        }
                        return;
                    }

                    // Check if a focused graph is available
                    UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
                    if (FocusedGraph)
                    {
                        // Graph is available - wrap it and stop the timer
                        TryWrapFocusedGraphTab(WeakEditor);
                        if (GEditor && InitialWrapTimerHandle.IsValid())
                        {
                            GEditor->GetTimerManager()->ClearTimer(InitialWrapTimerHandle);
                        }
                        return;
                    }

                    // No focused graph yet (e.g., Widget BP in Designer mode) - retry
                    InitialWrapRetryCount++;
                    if (InitialWrapRetryCount >= MaxInitialWrapRetries)
                    {
                        // Give up on initial wrap. The overlay will be injected when the
                        // user switches to Graph mode via OnTabForegrounded or the polling timer.
                        FN2CLogger::Get().Log(
                            FString::Printf(TEXT("Initial overlay wrap: stopped after %d retries (editor likely in non-graph mode)"), MaxInitialWrapRetries),
                            EN2CLogSeverity::Debug
                        );
                        if (GEditor && InitialWrapTimerHandle.IsValid())
                        {
                            GEditor->GetTimerManager()->ClearTimer(InitialWrapTimerHandle);
                        }
                    }
                }),
                InitialWrapRetryInterval, // 500ms between attempts
                true // Loop until cleared
            );
        }

    }
}


TArray<FName> FN2CEditorIntegration::GetAvailableThemes(EN2CCodeLanguage Language) const
{
    TArray<FName> ThemeNames;
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    Settings->CodeEditorTheme.Themes.GetKeys(ThemeNames);
    return ThemeNames;
}

FName FN2CEditorIntegration::GetDefaultTheme(EN2CCodeLanguage Language) const
{
    return TEXT("Unreal Engine");
}

void FN2CEditorIntegration::TranslateBlueprintNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor)
{
    // Check if translation is already in progress
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (LLMModule && LLMModule->GetSystemStatus() == EN2CSystemStatus::Processing)
    {
        FN2CLogger::Get().LogWarning(TEXT("Translation already in progress, please wait"));
        return;
    }

    FN2CLogger::Get().Log(TEXT("ExecuteCollectNodesForEditor called"), EN2CLogSeverity::Debug);

    // Show the window as a tab
    FGlobalTabmanager::Get()->TryInvokeTab(SN2CEditorWindow::TabId);
    FN2CLogger::Get().Log(TEXT("Node to Code window shown"), EN2CLogSeverity::Debug);

    // Store the editor as active
    StoreActiveBlueprintEditor(InEditor);

    // Use the new helper method to get JSON
    FString ErrorMsg;
    FN2CBlueprint Captured;
    FString JsonOutput = GetFocusedBlueprintAsJson(false /* no pretty print */, ErrorMsg, &Captured);

    if (JsonOutput.IsEmpty())
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to get Blueprint JSON: %s"), *ErrorMsg));
        return;
    }

    // Log the JSON output
    FN2CLogger::Get().LogLLMContent(TEXT("JSON Output"), TEXT("N2C JSON for translation"), JsonOutput);

    if (LLMModule->Initialize())
    {
        // Send JSON to LLM service
        // The translation is saved with the Blueprint this JSON was captured from
        LLMModule->ProcessN2CJson(JsonOutput, Captured, FOnLLMResponseReceived::CreateLambda(
            [](const FString& Response)
            {
                FN2CLogger::Get().LogLLMContent(TEXT("LLM Response"), TEXT("LLM response"), Response);
            
                // Create translation response struct
                FN2CTranslationResponse TranslationResponse;
            
                // Get active service's response parser
                TScriptInterface<IN2CLLMService> ActiveService = UN2CLLMModule::Get()->GetActiveService();
                if (ActiveService.GetInterface())
                {
                    UN2CResponseParserBase* Parser = ActiveService->GetResponseParser();
                    if (Parser)
                    {
                        if (Parser->ParseLLMResponse(Response, TranslationResponse))
                        {
                            // Log successful parsing
                            FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                        }
                        else
                        {
                            FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to parse LLM response: %s"),
                                *TranslationResponse.Error.ToDisplayString()));
                        }
                    }
                    else
                    {
                        FN2CLogger::Get().LogError(TEXT("No response parser available"));
                    }
                }
                else
                {
                    FN2CLogger::Get().LogError(TEXT("No active LLM service"));
                }
            }));
    }
    else
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize LLM Module"));
    }
}

void FN2CEditorIntegration::TranslateFocusedBlueprintGraph()
{
    // Get the latest active blueprint editor
    TSharedPtr<FBlueprintEditor> ActiveEditor = GetActiveBlueprintEditor();
    
    if (!ActiveEditor.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("TranslateFocusedBlueprintGraph: No active Blueprint editor found"));
        return;
    }
    
    // Call the existing translation function with the active editor
    TranslateBlueprintNodesForEditor(ActiveEditor);
}

FN2CLLMRequestHandleRef FN2CEditorIntegration::TranslateFocusedBlueprintAsync(
    const FString& ProviderIdOverride,
    const FString& ModelIdOverride,
    const FString& LanguageIdOverride,
    FOnLLMResponseReceived OnComplete)
{
    // Editor and UObject work: the whole request is built and sent on the game thread, so every object it creates is
    // an ordinary (collectable) object and no state is shared with a worker thread.
    check(IsInGameThread());

    FString ErrorMsg;
    const FString JsonInput = GetFocusedBlueprintAsJson(false /*bPrettyPrint*/, ErrorMsg);
    if (JsonInput.IsEmpty())
    {
        FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
        Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(FString::Printf(TEXT("Failed to get Blueprint JSON: %s"), *ErrorMsg)));
        return Handle;
    }

    return TranslateJsonAsync(JsonInput, ProviderIdOverride, ModelIdOverride, LanguageIdOverride, MoveTemp(OnComplete));
}

FN2CLLMRequestHandleRef FN2CEditorIntegration::TranslateJsonAsync(
    const FString& JsonInput,
    const FString& ProviderIdOverride,
    const FString& ModelIdOverride,
    const FString& LanguageIdOverride,
    FOnLLMResponseReceived OnComplete)
{
    check(IsInGameThread());

    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (!LLMModule || !LLMModule->IsInitialized())
    {
        // Attempt to initialize if not already
        if (!LLMModule || !LLMModule->Initialize())
        {
            FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
            Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(TEXT("LLMModule failed to initialize.")));
            return Handle;
        }
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();

    // Unknown provider or language IDs fail the request rather than silently falling back to the defaults
    FN2CTranslationOverrides Overrides;
    FString OverrideError;
    if (!ResolveTranslationOverrides(ProviderIdOverride, ModelIdOverride, LanguageIdOverride, Overrides, OverrideError))
    {
        FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
        Handle->Complete(OnComplete, FN2CTranslationError::MakeErrorJson(OverrideError, EN2CTranslationErrorKind::InvalidRequest));
        return Handle;
    }
    for (const FString& Warning : Overrides.Warnings)
    {
        FN2CLogger::Get().LogWarning(Warning);
    }

    // Start from the settings as they are now, not the module's config from its last Initialize: a provider picked
    // since then (Project Settings, the quick picker) is the default for this request
    FN2CLLMConfig RequestConfig = UN2CLLMModule::MakeConfigFromSettings();

    const EN2CLLMProvider FinalProvider = Overrides.Provider.Get(RequestConfig.Provider);
    RequestConfig.Provider = FinalProvider; // its service resolves the key for this provider when the request is sent

    // The override model, or the final provider's selected model with its thinking mode
    Settings->ResolveRequestModel(FinalProvider, ModelIdOverride, RequestConfig);

    const EN2CCodeLanguage TargetLanguage = Overrides.Language.Get(Settings->TargetLanguage);

    // Now call the LLMModule method that handles overrides
    return LLMModule->ProcessN2CJsonWithOverrides(JsonInput, RequestConfig, TargetLanguage, OnComplete);
}

namespace N2CTranslationOverridesPrivate
{
    /** Lowercase IDs of an enum's offered entries (no _MAX, no Hidden entries), with their values */
    static TArray<TPair<FString, int64>> ListIds(const UEnum* Enum)
    {
        TArray<TPair<FString, int64>> Ids;
        for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index) // -1: the generated _MAX entry
        {
            if (Enum->HasMetaData(TEXT("Hidden"), Index))
            {
                continue;
            }
            FString Id = Enum->GetNameStringByIndex(Index);
            int32 Separator = INDEX_NONE;
            if (Id.FindLastChar(TEXT(':'), Separator))
            {
                Id.RightChopInline(Separator + 1);
            }
            Ids.Emplace(Id.ToLower(), Enum->GetValueByIndex(Index));
        }
        return Ids;
    }

    /** The target-language table's MCP ids (cpp, python, ..., pseudocode), with their enum values */
    static TArray<TPair<FString, int64>> ListLanguageIds()
    {
        TArray<TPair<FString, int64>> Ids;
        for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
        {
            Ids.Emplace(Language.GetMcpId(), static_cast<int64>(Language.Enum));
        }
        return Ids;
    }

    /** The value whose ID equals Id ignoring case, or INDEX_NONE */
    static int64 FindId(const TArray<TPair<FString, int64>>& Ids, const FString& Id)
    {
        const FString Wanted = Id.TrimStartAndEnd();
        for (const TPair<FString, int64>& Pair : Ids)
        {
            if (Pair.Key.Equals(Wanted, ESearchCase::IgnoreCase))
            {
                return Pair.Value;
            }
        }
        return INDEX_NONE;
    }

    static TArray<FString> Keys(const TArray<TPair<FString, int64>>& Ids)
    {
        TArray<FString> Out;
        for (const TPair<FString, int64>& Pair : Ids)
        {
            Out.Add(Pair.Key);
        }
        return Out;
    }
}

TArray<FString> FN2CEditorIntegration::GetTranslationProviderIds()
{
    return N2CTranslationOverridesPrivate::Keys(N2CTranslationOverridesPrivate::ListIds(StaticEnum<EN2CLLMProvider>()));
}

TArray<FString> FN2CEditorIntegration::GetTranslationLanguageIds()
{
    return N2CTranslationOverridesPrivate::Keys(N2CTranslationOverridesPrivate::ListLanguageIds());
}

bool FN2CEditorIntegration::ResolveTranslationOverrides(const FString& ProviderId, const FString& ModelId, const FString& LanguageId,
    FN2CTranslationOverrides& OutOverrides, FString& OutError)
{
    using namespace N2CTranslationOverridesPrivate;
    OutOverrides = FN2CTranslationOverrides();
    OutError.Reset();

    if (!ProviderId.TrimStartAndEnd().IsEmpty())
    {
        const TArray<TPair<FString, int64>> Ids = ListIds(StaticEnum<EN2CLLMProvider>());
        const int64 Value = FindId(Ids, ProviderId);
        if (Value == INDEX_NONE)
        {
            OutError = FString::Printf(TEXT("Unknown provider '%s'. Valid providers: %s"), *ProviderId, *FString::Join(Keys(Ids), TEXT(", ")));
            return false;
        }
        OutOverrides.Provider = static_cast<EN2CLLMProvider>(Value);
    }

    if (!LanguageId.TrimStartAndEnd().IsEmpty())
    {
        const TArray<TPair<FString, int64>> Ids = ListLanguageIds();
        const int64 Value = FindId(Ids, LanguageId);
        if (Value == INDEX_NONE)
        {
            OutError = FString::Printf(TEXT("Unknown language '%s'. Valid languages: %s"), *LanguageId, *FString::Join(Keys(Ids), TEXT(", ")));
            return false;
        }
        OutOverrides.Language = static_cast<EN2CCodeLanguage>(Value);
    }

    OutOverrides.ModelId = ModelId.TrimStartAndEnd();
    if (!OutOverrides.ModelId.IsEmpty())
    {
        // Local providers serve whatever models the user installed: only cloud model IDs can be checked
        const EN2CLLMProvider Provider = OutOverrides.Provider.Get(GetDefault<UN2CSettings>()->Provider);
        if (Provider != EN2CLLMProvider::Ollama && Provider != EN2CLLMProvider::LMStudio)
        {
            const FString ProviderName = StaticEnum<EN2CLLMProvider>()->GetNameStringByValue(static_cast<int64>(Provider)).ToLower();
            bool bListed = false;
            bool bRetired = false;
            TArray<FString> LiveIds;
            for (const FN2CModelMetadata& Meta : FN2CLLMModelRegistry::Get().GetAllModelsForProvider(Provider))
            {
                if (Meta.ModelId == OutOverrides.ModelId)
                {
                    bListed = true;
                    bRetired = Meta.bIsRetired;
                }
                if (!Meta.bIsRetired)
                {
                    LiveIds.AddUnique(Meta.ModelId);
                }
            }
            if (!bListed || bRetired)
            {
                OutOverrides.Warnings.Add(FString::Printf(
                    TEXT("Model '%s' is %s for provider '%s'; it was sent to the provider as given. Listed models: %s"),
                    *OutOverrides.ModelId, bRetired ? TEXT("retired") : TEXT("not in NodeToCode's model list"), *ProviderName,
                    *FString::Join(LiveIds, TEXT(", "))));
            }
        }
    }

    return true;
}

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

TSharedPtr<SOverlay> FN2CEditorIntegration::FindGraphEditorOverlay(const TSharedRef<SGraphEditor>& GraphEditor)
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

TSharedPtr<SGraphEditor> FN2CEditorIntegration::FindGraphEditorFor(const TSharedPtr<FBlueprintEditor>& Editor, const UEdGraph* Graph)
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

bool FN2CEditorIntegration::WrapGraphTabIfNeeded(TSharedPtr<SDockTab> Tab, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor)
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
            FString::Printf(TEXT("WrapGraphTabIfNeeded: tab content is not an SGraphEditor (type=%s)"), *TabContent->GetTypeAsString()),
            EN2CLogSeverity::Debug);
        return false;
    }
    const TSharedRef<SGraphEditor> GraphEditor = StaticCastSharedRef<SGraphEditor>(TabContent);
    if (GraphEditor->GetCurrentGraph() != Graph)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("WrapGraphTabIfNeeded: the tab shows another graph than '%s'"), *Graph->GetName()),
            EN2CLogSeverity::Debug);
        return false;
    }
    return InjectOverlay(GraphEditor, Graph, Editor);
}

bool FN2CEditorIntegration::InjectOverlay(const TSharedRef<SGraphEditor>& GraphEditor, UEdGraph* Graph, TWeakPtr<FBlueprintEditor> Editor)
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

    FN2CLogger::Get().Log(FString::Printf(TEXT("Overlay injected for graph '%s'"), *GraphName), EN2CLogSeverity::Debug);
    return true;
}

bool FN2CEditorIntegration::HasLiveOverlay(const UEdGraph* Graph) const
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

bool FN2CEditorIntegration::CheckLiveOverlay(const TSharedRef<SGraphEditor>& GraphEditor, const UEdGraph* Graph)
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

void FN2CEditorIntegration::RemoveInjectedOverlays()
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

void FN2CEditorIntegration::RecordInjectionFailure(const UEdGraph* Graph, const TCHAR* Why)
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

bool FN2CEditorIntegration::IsInjectionDue(const UEdGraph* Graph, double Now) const
{
    const FInjectionRetry* Retry = InjectionRetries.Find(TWeakObjectPtr<UEdGraph>(const_cast<UEdGraph*>(Graph)));
    return !Retry || (Retry->Failures < MaxPollInjectionAttempts && Now >= Retry->NextAttemptTime);
}

bool FN2CEditorIntegration::TryWrapFocusedGraphTab(TWeakPtr<FBlueprintEditor> WeakEditor)
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

void FN2CEditorIntegration::PollOverlays()
{
    if (!GEditor || !N2COverlayInjectionPrivate::CVarOverlayPoll.GetValueOnGameThread())
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();
    for (const TSharedPtr<FBlueprintEditor>& Editor : FN2CBlueprintEditorTracker::GetOpenBlueprintEditors())
    {
        UEdGraph* FocusedGraph = Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
        if (!FocusedGraph || HasLiveOverlay(FocusedGraph) || !IsInjectionDue(FocusedGraph, Now))
        {
            continue;
        }
        // Overlay upkeep only: a background editor's rebuilt tab must not take the focus (card 0150)
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Overlay poll: injecting the overlay for graph '%s'"), *FocusedGraph->GetName()),
            EN2CLogSeverity::Debug);
        TryWrapFocusedGraphTab(Editor);
    }
}

void FN2CEditorIntegration::CleanupStaleWrappers()
{
    // Remove entries where the tab is no longer valid
    TArray<TWeakPtr<SDockTab>> StaleKeys;

    for (auto& Pair : WrappedTabs)
    {
        if (!Pair.Key.IsValid() || !Pair.Value.IsValid())
        {
            StaleKeys.Add(Pair.Key);
        }
    }

    for (const TWeakPtr<SDockTab>& Key : StaleKeys)
    {
        WrappedTabs.Remove(Key);
    }

    if (StaleKeys.Num() > 0)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Cleaned up %d stale wrapper references"), StaleKeys.Num()),
            EN2CLogSeverity::Debug
        );
    }

    // Clean up stale overlay tracking (graph editors closed or rebuilt, graphs of closed Blueprints)
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

void FN2CEditorIntegration::OnTabForegrounded(TSharedPtr<SDockTab> NewlyForegrounded, TSharedPtr<SDockTab> Backgrounded)
{
    // FGlobalTabmanager broadcasts TabForegrounded(NewForegroundTab, BackgroundedTab). The focused Blueprint editor
    // is tracked by FN2CBlueprintEditorTracker; this handler only keeps the overlays injected.
    // Early-out: only graph tabs coming to the front or going behind. This prevents non-graph tab activations
    // (Details, Palette, etc.) from scheduling/cancelling timers unnecessarily.
    const auto IsGraphTab = [](const TSharedPtr<SDockTab>& Tab)
    {
        return Tab.IsValid() && N2COverlayInjectionPrivate::IsExactly(*Tab->GetContent(), N2COverlayInjectionPrivate::GraphEditorType);
    };
    if (!IsGraphTab(NewlyForegrounded) && !IsGraphTab(Backgrounded))
    {
        return;
    }

    // A graph tab was foregrounded. Schedule overlay injection with a short delay
    // to let mode switches (e.g., Widget BP Designer->Graph) complete.
    if (GEditor)
    {
        if (TabChangeWrapTimerHandle.IsValid())
        {
            GEditor->GetTimerManager()->ClearTimer(TabChangeWrapTimerHandle);
        }

        GEditor->GetTimerManager()->SetTimer(
            TabChangeWrapTimerHandle,
            FTimerDelegate::CreateLambda([this]()
            {
                // Each open Blueprint editor's focused graph without an overlay gets one (not only the stored active
                // editor, which may be stale). The focused editor is not changed here: FN2CBlueprintEditorTracker
                // follows the tab events themselves (card 0150). A tab event always tries, also for a graph the
                // poll gave up on
                for (const TSharedPtr<FBlueprintEditor>& Editor : FN2CBlueprintEditorTracker::GetOpenBlueprintEditors())
                {
                    UEdGraph* FocusedGraph = Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
                    if (FocusedGraph && !HasLiveOverlay(FocusedGraph))
                    {
                        TryWrapFocusedGraphTab(Editor);
                    }
                }
            }),
            0.1f, // 100ms delay - gives mode switches time to complete
            false // Don't loop
        );
    }
}

void FN2CEditorIntegration::SetTranslationInProgress(bool bInProgress)
{
    if (bIsAnyTranslationInProgress != bInProgress)
    {
        bIsAnyTranslationInProgress = bInProgress;
        OnTranslationStateChanged.Broadcast(bInProgress);

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Global translation state changed: %s"),
                bInProgress ? TEXT("IN PROGRESS") : TEXT("IDLE")),
            EN2CLogSeverity::Info
        );
    }
}

void FN2CEditorIntegration::RequestOverlayTranslation(const FGuid& GraphGuid, const FString& GraphName, const FString& BlueprintPath)
{
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("RequestOverlayTranslation: Graph=%s (GUID=%s)"), *GraphName, *GraphGuid.ToString()),
        EN2CLogSeverity::Info
    );

    // Broadcast to any listeners (main window will show progress modal)
    OnOverlayTranslationRequested.Broadcast(GraphGuid, GraphName, BlueprintPath);
}
