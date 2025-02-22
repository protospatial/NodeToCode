// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CHttpHandlerBase.h"
#include "LLM/N2CResponseParserBase.h"
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


private:
    /** Initialize components */
    bool InitializeComponents();

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

    /** Current system status */
    UPROPERTY()
    EN2CSystemStatus CurrentStatus;
    
    /** Initialization state */
    bool bIsInitialized;
};
