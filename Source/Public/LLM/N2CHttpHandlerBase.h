// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"
#include "Models/N2CTranslation.h"
#include "Interfaces/IHttpRequest.h"
#include "N2CHttpHandlerBase.generated.h"

/**
 * @class UN2CHttpHandlerBase
 * @brief Base class for handling HTTP communication with LLM providers
 */
UCLASS(Abstract)
class NODETOCODE_API UN2CHttpHandlerBase : public UObject
{
    GENERATED_BODY()

public:
    /** Delegate for translation responses */
    FOnTranslationResponseReceived OnTranslationResponseReceived;

    /** Configuration for request timeouts */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    float RequestTimeout = 3600.0f;

    /** Additional headers for requests */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    TMap<FString, FString> ExtraHeaders;

    /** Initialize with configuration */
    virtual void Initialize(const FN2CLLMConfig& Config);

    /** Core request method */
    virtual void PostLLMRequest(
        const FString& Endpoint,
        const FString& AuthToken,
        const FString& Payload,
        const FOnLLMResponseReceived& OnComplete
    );

protected:
    /** Validate request parameters */
    virtual bool ValidateRequest(
        const FString& Endpoint,
        const FString& Payload
    ) const;

    /** Handle request completion */
    virtual void OnRequestComplete(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        FOnLLMResponseReceived OnComplete
    );

    /** Current configuration */
    FN2CLLMConfig Config;
};
