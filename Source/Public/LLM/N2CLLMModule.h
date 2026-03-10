// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CHttpHandlerBase.h"
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

    /** Process N2C JSON through LLM */
    void ProcessN2CJson(
        const FString& JsonInput,
        const FOnLLMResponseReceived& OnComplete
    );

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
    
    /** Get the appropriate file extension for the target language */
    FString GetFileExtensionForLanguage(EN2CCodeLanguage Language) const;
    
    /** Create directory if it doesn't exist */
    bool EnsureDirectoryExists(const FString& DirectoryPath) const;
    
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
    TObjectPtr<class UN2CSystemPromptManager> PromptManager;

    /** HTTP handler */
    UPROPERTY()
    TObjectPtr<class UN2CHttpHandlerBase> HttpHandler;

    /** Response parser */
    UPROPERTY()
    TObjectPtr<class UN2CResponseParserBase> ResponseParser;

    /** Active LLM service */
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
