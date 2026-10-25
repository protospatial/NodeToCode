// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CResponseParserBase.h"
#include "Models/N2CBlueprint.h"
#include "N2CLLMModule.generated.h"

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

    /** Initialize module: reads provider, model and key from the settings and creates the service */
    bool Initialize();

    /**
     * The request configuration the settings give now: provider, API key, model with its thinking mode, output
     * budget and HTTP activity timeout. Every request starts from this snapshot, so a settings change (Project
     * Settings, the context window's quick picker) applies to the next request, including the next batch item.
     */
    static FN2CLLMConfig MakeConfigFromSettings();

    /**
     * Re-reads the settings (MakeConfigFromSettings) and, when the provider, model, key, thinking mode, output budget
     * or activity timeout differ from the current configuration, or the module is not initialized, rebuilds the
     * active service so its headers and endpoint come from the new values. Requests already in flight keep the
     * service they were sent on. ProcessN2CJson calls it before every request. Returns false if no service could be
     * created.
     */
    bool RefreshFromSettings();

    /**
     * Process N2C JSON through LLM. Returns the request's handle: Cancel() stops the HTTP request, OnComplete and the
     * OnTranslationResponseReceived broadcast, and returns the module to Idle. Callers may ignore it.
     *
     * A successful translation is saved with CapturedBlueprint (the Blueprint JsonInput was serialized from), copied
     * when the request is sent: whatever is captured while the request is in flight cannot change where or with
     * which Blueprint JSON it is saved.
     */
    FN2CLLMRequestHandleRef ProcessN2CJson(
        const FString& JsonInput,
        const FN2CBlueprint& CapturedBlueprint,
        const FOnLLMResponseReceived& OnComplete
    );

    /**
     * ProcessN2CJson with a per-request parsed result: OnParsed gets this request's FN2CTranslationResponse (with its
     * typed error on failure) at most once, never after a successful Cancel() on the returned handle. Use it instead of
     * OnTranslationResponseReceived to tell your result from another caller's (the batch orchestrator does).
     */
    FN2CLLMRequestHandleRef ProcessN2CJson(
        const FString& JsonInput,
        const FN2CBlueprint& CapturedBlueprint,
        const FOnLLMResponseReceived& OnComplete,
        const FOnN2CTranslationParsed& OnParsed
    );

    /** Process N2C JSON through LLM with specific overrides for provider, model, and language. Returns the request's handle. */
    FN2CLLMRequestHandleRef ProcessN2CJsonWithOverrides(
        const FString& JsonInput,
        const FN2CLLMConfig& RequestConfig,
        EN2CCodeLanguage RequestLanguage,
        const FOnLLMResponseReceived& OnComplete
    );

    /**
     * Completes the typed error of a failed translation: names the provider and model, and records a Parse error
     * (with the start of RawResponse) when the parser found no more specific one.
     */
    static void FillTranslationError(FN2CTranslationResponse& Response, EN2CLLMProvider Provider, const FString& Model,
        const FString& RawResponse);

    /** Get the current configuration */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    const FN2CLLMConfig& GetConfig() const { return Config; }

    /** Check if module is initialized */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    bool IsInitialized() const { return bIsInitialized; }

    /** Get the active LLM service */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    TScriptInterface<IN2CLLMService> GetActiveService() const { return ActiveService; }

    /**
     * Broadcast for every ProcessN2CJson result, whoever sent it. Kept for Blueprint listeners only: it cannot tell one
     * request from another, so C++ callers use the per-request OnParsed of ProcessN2CJson (deprecated for correlation).
     */
    UPROPERTY(BlueprintAssignable, Category = "Node to Code | LLM Module")
    FOnTranslationResponseReceived OnTranslationResponseReceived;

    /** Delegate for notifying when translation request is sent */
    UPROPERTY(BlueprintAssignable, Category = "Node to Code | LLM Module")
    FOnTranslationRequestSent OnTranslationRequestSent;

    /** Get the current system status */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    EN2CSystemStatus GetSystemStatus() const { return CurrentStatus; }

    /** Get the path to the latest translation */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Module")
    FString GetLatestTranslationPath() const { return LatestTranslationPath; }

    /** The language the latest translation's implementation files were saved as */
    EN2CCodeLanguage GetLatestTranslationLanguage() const { return LatestTranslationLanguage; }

    /** Open the latest translation folder in file explorer */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Module")
    void OpenTranslationFolder(bool& Success);

    /** Save translation files to disk */
    bool SaveTranslationToDisk(const FN2CTranslationResponse& Response, const FN2CBlueprint& Blueprint);

private:
    /** Generate file paths for translation */
    FString GenerateTranslationRootPath(const FString& BlueprintName) const;

    /** Get the base path where translations are saved */
    FString GetTranslationBasePath() const;

public:    
    /** Get the appropriate file extension for the target language */
    FString GetFileExtensionForLanguage(EN2CCodeLanguage Language) const;
    
private:
    
    /** Create directory if it doesn't exist */
    bool EnsureDirectoryExists(const FString& DirectoryPath) const;
    
    /** Initialize components */
    bool InitializeComponents();

    /** Initialize the provider registry with all available providers */
    void InitializeProviderRegistry();

    /** Create appropriate service for provider */
    bool CreateServiceForProvider(EN2CLLMProvider Provider);

    /** Broadcasts a failed OnTranslationResponseReceived carrying Error (for errors found before a request is sent) */
    void BroadcastFailure(const FN2CTranslationError& Error);

    /** A handle already completed with Response (for errors found before a request is sent) */
    static FN2CLLMRequestHandleRef CompleteWithoutRequest(const FOnLLMResponseReceived& OnComplete, const FString& Response);

    /** Ends the request's status ownership (EndRequest(Idle)) when Handle is cancelled */
    void ResetStatusOnCancel(const FN2CLLMRequestHandleRef& Handle);

    /**
     * Status ownership: each sent request owns the Processing status until it ends. BeginRequest counts it in;
     * EndRequest counts it out and sets Result only when no other request is still in flight, so one caller's result
     * cannot mark another caller's request Idle or Error.
     */
    void BeginRequest();
    void EndRequest(EN2CSystemStatus Result);

    /** Sets Status unless a request is in flight (for errors found before anything is sent) */
    void SetStatusIfIdle(EN2CSystemStatus Status);

    /** Requests sent and not yet completed or cancelled */
    int32 InFlightRequests = 0;

    /** The local provider endpoint setting the active service was created with (Ollama, LM Studio) */
    FString ServiceEndpointSetting;

    /** The endpoint setting a local provider reads when its service is created; empty for cloud providers */
    static FString GetLocalEndpointSetting(EN2CLLMProvider Provider);

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

    /** Active LLM service. A UPROPERTY so garbage collection keeps it (and its HTTP handler) alive. */
    UPROPERTY()
    TScriptInterface<class IN2CLLMService> ActiveService;

    /** Current system status */
    UPROPERTY()
    EN2CSystemStatus CurrentStatus;
    
    /** Path to the latest translation */
    UPROPERTY()
    FString LatestTranslationPath;

    /** Language of the latest saved translation */
    EN2CCodeLanguage LatestTranslationLanguage = EN2CCodeLanguage::Cpp;
    
    /** Initialization state */
    bool bIsInitialized;
};
