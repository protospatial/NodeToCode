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

    /** Initialize module */
    bool Initialize();

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

    /** Delegate for notifying when translation response is received */
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

    /** Returns the module to Idle when Handle is cancelled while the module is still Processing */
    void ResetStatusOnCancel(const FN2CLLMRequestHandleRef& Handle);

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
    
    /** Initialization state */
    bool bIsInitialized;
};
