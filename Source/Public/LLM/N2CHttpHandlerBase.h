// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"
#include "Models/N2CTranslation.h"
#include "Interfaces/IHttpRequest.h"
#include "N2CHttpHandlerBase.generated.h"

/** One queued request owned by a provider HTTP handler. */
struct FN2CQueuedHttpRequest
{
    FString Endpoint;
    FString AuthToken;
    FString Payload;
    FOnLLMResponseReceived OnComplete;
    int32 RateLimitRetryCount = 0;
};

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

    /** Core request method. Requests are admitted through the provider-level queue. */
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

    /** Handle request completion after transport-level retry handling has finished. */
    virtual void OnRequestComplete(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful,
        FOnLLMResponseReceived OnComplete
    );

    /** Admit as many queued requests as the provider concurrency/cooldown state allows. */
    void PumpRequestQueue();

    /** Dispatch one admitted request. */
    void DispatchQueuedRequest(const TSharedRef<FN2CQueuedHttpRequest>& QueuedRequest);

    /** Handle one HTTP attempt and coordinate any provider-wide 429 cooldown. */
    void HandleRequestAttemptComplete(
        const TSharedRef<FN2CQueuedHttpRequest>& QueuedRequest,
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful
    );

    /** Schedule a future queue pump without blocking the editor thread. */
    void ScheduleQueuePump(float DelaySeconds);

    /** Resolve provider-supplied retry timing or calculate bounded exponential fallback backoff. */
    float CalculateRateLimitRetryDelay(
        FHttpResponsePtr Response,
        const FString& ResponseBody,
        int32 RateLimitRetryCount
    ) const;

    /** True when a 429 describes a request that can never fit the provider's per-request/token limit. */
    bool IsPermanentRateLimitFailure(const FString& ResponseBody) const;

    /** Current configuration */
    FN2CLLMConfig Config;

    /** Provider-local request admission state. */
    TArray<TSharedPtr<FN2CQueuedHttpRequest>> PendingRequestQueue;
    int32 ActiveRequestCount = 0;
    double ProviderCooldownUntilSeconds = 0.0;
    bool bQueuePumpScheduled = false;
    bool bRateLimitObserved = false;
};
