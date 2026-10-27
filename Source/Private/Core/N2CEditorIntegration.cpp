// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CEditorIntegration.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CGraphOverlayInjector.h"

#include "Core/N2CNodeCollector.h"
#include "BlueprintEditorModule.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CEditorWindow.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "Models/N2CTranslation.h"

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

void FN2CEditorIntegration::Initialize()
{
    // Register tab spawner
    SN2CEditorWindow::RegisterTabSpawner();

    // Track the focused Blueprint editor from the user's tab and focus changes (card 0150)
    FN2CBlueprintEditorTracker::Get().Initialize();

    // Keep the graph overlays in the Blueprint editors' graph editors (card 0344)
    FN2CGraphOverlayInjector::Get().Initialize();

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration initialized"), EN2CLogSeverity::Info);
}

void FN2CEditorIntegration::Shutdown()
{
    // Stops its timers and takes the injected overlays out of the engine's graph editors
    FN2CGraphOverlayInjector::Get().Shutdown();

    // Unregister tab spawner
    SN2CEditorWindow::UnregisterTabSpawner();

    FN2CBlueprintEditorTracker::Get().Shutdown();

    FN2CLogger::Get().Log(TEXT("N2C Editor Integration shutdown"), EN2CLogSeverity::Info);
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
