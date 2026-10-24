// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpResponse.h"
#include "LLM/N2CLLMTypes.h"

struct FN2CNativeBatchRequest
{
    int32 RequestId = 0;
    FString RequestLabel;
    FString FormattedPayload;
};

struct FN2CNativeBatchResult
{
    int32 RequestId = 0;
    FString RequestLabel;
    FString FormattedPayload;
    FString RawResponse;
};

class FN2CNativeBatchProcessor : public TSharedFromThis<FN2CNativeBatchProcessor>
{
public:
    using FOnItemComplete = TFunction<void(const FN2CNativeBatchResult&)>;
    using FOnBatchComplete = TFunction<void()>;
    using FOnBatchUnavailable = TFunction<void(const FString&)>;

    static bool SupportsProvider(EN2CLLMProvider Provider);

    static TSharedRef<FN2CNativeBatchProcessor> Create(
        const FN2CLLMConfig& Config,
        TArray<FN2CNativeBatchRequest> Requests,
        FOnItemComplete OnItemComplete,
        FOnBatchComplete OnBatchComplete,
        FOnBatchUnavailable OnBatchUnavailable);

    void Start();

private:
    FN2CNativeBatchProcessor(
        const FN2CLLMConfig& InConfig,
        TArray<FN2CNativeBatchRequest> InRequests,
        FOnItemComplete InOnItemComplete,
        FOnBatchComplete InOnBatchComplete,
        FOnBatchUnavailable InOnBatchUnavailable);

    void StartOpenAIBatch();
    void StartAnthropicBatch();
    void StartGeminiBatch();

    void PollOpenAIBatch(const FString& BatchId);
    void PollAnthropicBatch(const FString& BatchId);
    void PollGeminiBatch(const FString& BatchName);

    void ProcessOpenAIResults(const FString& JsonLines);
    void ProcessAnthropicResults(const FString& JsonLines);
    void ProcessGeminiResults(const TSharedPtr<class FJsonObject>& JobObject);

    void CompleteItemByCustomId(const FString& CustomId, const FString& RawResponse);
    void CompleteItemByIndex(int32 RequestIndex, const FString& RawResponse);
    void CompleteMissingItems(const FString& ErrorMessage);
    void FinishBatch();
    void FallbackBeforeStart(const FString& Reason);

    void SendHttp(
        const FString& Verb,
        const FString& Url,
        const TMap<FString, FString>& Headers,
        const FString& Body,
        const FString& ContentType,
        TFunction<void(int32, const FString&, const FHttpResponsePtr&)> OnComplete,
        int32 RetryCount = 0);

    void SchedulePoll(TFunction<void()> PollFunction, float DelaySeconds = 5.0f);

    FString MakeCustomId(int32 RequestId) const;
    const FN2CNativeBatchRequest* FindRequestByCustomId(const FString& CustomId) const;

    FN2CLLMConfig Config;
    TArray<FN2CNativeBatchRequest> Requests;
    TSet<int32> CompletedRequestIds;
    FOnItemComplete OnItemComplete;
    FOnBatchComplete OnBatchComplete;
    FOnBatchUnavailable OnBatchUnavailable;
    bool bFinished = false;
};
