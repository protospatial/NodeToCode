// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CResponseParserBase.h"
#include "Models/N2CBlueprint.h"
#include "N2CLLMModule.generated.h"

class UN2CBaseLLMService;

struct FN2CPendingNativeBatchRequest
{
    int32 RequestId = 0;
    FString RequestLabel;
    FString FormattedPayload;
    FOnLLMResponseReceived OnComplete;
};

/**
 * @class UN2CLLMModule
 * @brief Main module for managing LLM integration and translation requests
 */
UCLASS()
class NODETOCODE_API UN2CLLMModule : public UObject
{
    GENERATED_BODY()

public:
    /** Get the singleton instance */
    static UN2CLLMModule* Get();

    /** Blueprint accessible getter for the singleton instance */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module", meta = (DisplayName = "Get N2C LLM Module"))
    static UN2CLLMModule* GetBP() { return Get(); }

    /** Initialize module */
    bool Initialize();

    /** Process N2C JSON through LLM */
    void ProcessN2CJson(
        const FString& JsonInput,
        const FOnLLMResponseReceived& OnComplete
    );

    /** Get the current configuration */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    const FN2CLLMConfig& GetConfig() const { return Config; }

    /**
     * Apply a transient provider/model choice for the current translation operation.
     * This intentionally does not modify or save UN2CSettings::Provider.
     */
    void ApplyRequestProviderOverride(
        EN2CLLMProvider Provider,
        const FString& ApiKey,
        const FString& Model,
        const FString& CustomProviderName = FString())
    {
        Config.Provider = Provider;
        Config.ApiEndpoint.Empty();
        Config.ApiKey = ApiKey;
        Config.Model = Model;
        Config.CustomProviderName = CustomProviderName;
        Config.bUseSystemPrompts = true;
    }

    /** Check if module is initialized */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    bool IsInitialized() const { return bIsInitialized; }

    /** Get the active LLM service */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    TScriptInterface<IN2CLLMService> GetActiveService() const { return ActiveService; }

    /** Delegate for notifying when translation response is received */
    UPROPERTY(BlueprintAssignable, Category = "Node to Code | LLM Module")
    FOnTranslationResponseReceived OnTranslationResponseReceived;

    /** Delegate for notifying when translation request is sent */
    UPROPERTY(BlueprintAssignable, Category = "Node to Code | LLM Module")
    FOnTranslationRequestSent OnTranslationRequestSent;

    /** Get the current system status */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    EN2CSystemStatus GetSystemStatus() const { return CurrentStatus; }

    /** Number of requests that have been sent but have not fully finished processing. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    int32 GetInFlightRequestCount() const { return InFlightRequestCount; }

    /** True while any request or its completion work is still in progress. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    bool IsWorkInProgress() const { return InFlightRequestCount > 0 || CurrentStatus == EN2CSystemStatus::Initializing; }

    /** Number of raw provider responses retained for the current translation session. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    int32 GetRawResponseCount() const { return SessionRawResponses.Num(); }

    /** Raw provider requests/responses retained for the current translation session. */
    const TArray<FN2CRawResponseRecord>& GetRawResponseHistory() const { return SessionRawResponses; }

    /** Replay a captured provider request and replace that request's existing history/result in place. */
    bool ResendRawRequest(
        int32 RequestId,
        TFunction<void(bool)> OnComplete = TFunction<void(bool)>());

    /** Replay a captured request through an explicitly selected provider/model. */
    bool ResendRawRequest(
        int32 RequestId,
        const FN2CLLMConfig& RetryConfig,
        TFunction<void(bool)> OnComplete = TFunction<void(bool)>());

    /** Aggregate parsed response for the current translation session. */
    const FN2CTranslationResponse& GetSessionTranslationResponse() const { return SessionTranslationResponse; }

    /** Get the path to the latest translation */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    FString GetLatestTranslationPath() const { return LatestTranslationPath; }

    /** Base directory containing all saved translation batches. */
    FString GetTranslationBaseDirectory() const { return GetTranslationBasePath(); }

    /** Open the latest translation folder in file explorer */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    void OpenTranslationFolder(bool& Success);

    /** Save translation files to disk */
    bool SaveTranslationToDisk(const FN2CTranslationResponse& Response, const FN2CBlueprint& Blueprint);

    /** Begin a batch translation (e.g. Translate Entire Blueprint) - all translations in this batch will share the same root directory */
    void BeginBatchTranslation(const FString& BlueprintName);

    /** End a batch translation - clears the batch root path */
    void EndBatchTranslation();

private:
    /** Reset response/progress state at the start of a new user translation operation. */
    void ResetRequestSession();

    /** Merge one parsed response into the aggregate response exposed to the UI. */
    void AppendSessionResponse(const FN2CTranslationResponse& Response);

    /** Rebuild graph-phase aggregate state from authoritative per-request parsed responses. */
    FN2CTranslationResponse BuildGraphAggregateResponse() const;
    void RebuildSessionResponseFromGraphRequests();

    /** Queue same-frame whole-Blueprint requests for a provider-native batch API. */
    bool TryQueueNativeBatchRequest(
        const FString& JsonInput,
        const FString& SystemPrompt,
        const FOnLLMResponseReceived& OnComplete);
    void FlushNativeBatchRequests();
    void DispatchPendingBatchIndividually(TArray<FN2CPendingNativeBatchRequest> Requests, const FString& Reason);
    void HandleCompletedBatchItem(
        int32 RequestId,
        const FString& RequestLabel,
        const FString& RawRequest,
        const FString& Response,
        const FOnLLMResponseReceived& OnComplete);

    /** Start or replace the final semantic consolidation request using current graph results. */
    bool StartFinalConsolidation(
        int32 ExistingRequestId = INDEX_NONE,
        TFunction<void(bool)> OnComplete = TFunction<void(bool)>());
    void StartQueuedConsolidationIfReady();

    /** Start a replacement consolidation through an explicitly selected transient provider/model. */
    bool StartFinalConsolidationWithConfig(
        int32 ExistingRequestId,
        const FN2CLLMConfig& RequestConfig,
        TFunction<void(bool)> OnComplete);

    /** Create/release an isolated provider service for one selected retry. */
    TScriptInterface<IN2CLLMService> CreateTransientService(const FN2CLLMConfig& ServiceConfig);
    void ReleaseTransientService(UObject* ServiceObject);

    /** Persist current raw request/response history into the active/latest translation batch. */
    void PersistRequestHistory() const;

    /** Finish one in-flight request and update aggregate status. */
    void FinishRequest(bool bSuccess);

    /** Pretty-print a JSON provider response while leaving non-JSON text untouched. */
    FString FormatRawResponseForDisplay(const FString& RawResponse) const;

    /** Generate file paths for translation */
    FString GenerateTranslationRootPath(const FString& BlueprintName) const;

    /** Get the base path where translations are saved */
    FString GetTranslationBasePath() const;
    
    /** Get the appropriate file extension for the target language */
    FString GetFileExtensionForLanguage(EN2CCodeLanguage Language) const;
    
    /** Create directory if it doesn't exist */
    bool EnsureDirectoryExists(const FString& DirectoryPath) const;

    /** Save graph files with batch-specific features (sanitized names, ClassItSelf special handling) */
    void SaveGraphFilesWithBatchFeatures(
        const FN2CTranslationResponse& Response,
        const FString& RootPath,
        EN2CCodeLanguage TargetLanguage) const;

    /** Save graph files using original simple logic (for single translations) */
    void SaveGraphFilesOriginal(
        const FN2CTranslationResponse& Response,
        const FString& RootPath,
        EN2CCodeLanguage TargetLanguage) const;

    /** Save raw LLM response to disk when parsing fails for debugging */
    void SaveRawResponseToDisk(const FString& RawResponse);
    
    /** Initialize components */
    bool InitializeComponents();

    /** Initialize the provider registry with all available providers */
    void InitializeProviderRegistry();

    /** Create appropriate service for provider */
    bool CreateServiceForProvider(EN2CLLMProvider Provider);

    /** Current configuration */
    UPROPERTY()
    FN2CLLMConfig Config;

    /** System prompt manager */
    UPROPERTY()
    class UN2CSystemPromptManager* PromptManager;

    /** HTTP handler */
    UPROPERTY()
    class UN2CHttpHandlerBase* HttpHandler;

    /** Response parser */
    UPROPERTY()
    class UN2CResponseParserBase* ResponseParser;

    /** Active LLM service */
    TScriptInterface<class IN2CLLMService> ActiveService;

    /** Keep transient retry services alive until their asynchronous request completes. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UN2CBaseLLMService>> RetryServiceObjects;

    /** Current system status */
    UPROPERTY()
    EN2CSystemStatus CurrentStatus;
    
    /** Path to the latest translation */
    UPROPERTY()
    FString LatestTranslationPath;
    
    /** Cached root path for the current translation batch (e.g. one Translate Entire Blueprint run) */
    FString CurrentBatchRootPath;

    /** Aggregate parsed response currently exposed to the UI. */
    FN2CTranslationResponse SessionTranslationResponse;

    /** Successful graph-phase responses keyed by their stable request id. */
    TMap<int32, FN2CTranslationResponse> ParsedGraphResponsesByRequestId;

    /** Raw provider requests/responses for every request in the current operation. */
    TArray<FN2CRawResponseRecord> SessionRawResponses;

    /** Whole-Blueprint requests collected until the next tick for native batch dispatch. */
    TArray<FN2CPendingNativeBatchRequest> PendingNativeBatchRequests;
    bool bNativeBatchFlushScheduled = false;

    /** Prevent duplicate retry dispatch for the same raw-history entry. */
    TSet<int32> RetryInFlightRequestIds;

    /** A requested consolidation retry waits here until all ordinary requests/retries finish. */
    int32 PendingConsolidationRetryRequestId = INDEX_NONE;
    TFunction<void(bool)> PendingConsolidationRetryCompletion;

    /** Number of request completions still outstanding. */
    int32 InFlightRequestCount = 0;

    /** Monotonic request identifier within the current operation. */
    int32 NextRequestId = 1;

    /** Tracks whether any request in the current operation failed to parse. */
    bool bSessionHadError = false;
    
    /** Initialization state */
    bool bIsInitialized = false;
};
