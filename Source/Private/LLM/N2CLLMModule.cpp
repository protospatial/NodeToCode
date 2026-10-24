// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Containers/Ticker.h"
#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTranslationHistory.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CBatchTranslationConsolidator.h"
#include "LLM/N2CLLMProviderRegistry.h"
#include "LLM/N2CNativeBatchProcessor.h"
#include "LLM/N2CSystemPromptManager.h"
#include "LLM/Providers/N2CAnthropicService.h"
#include "LLM/Providers/N2CDeepSeekService.h"
#include "LLM/Providers/N2CGeminiService.h"
#include "LLM/Providers/N2CLMStudioService.h"
#include "LLM/Providers/N2CMiniMaxService.h"
#include "LLM/Providers/N2COllamaService.h"
#include "LLM/Providers/N2COpenAIService.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

namespace N2CLLMModuleSessionPrivate
{
constexpr int32 NativeBatchTargetEstimatedTokens = 64 * 1024;
constexpr int32 NativeBatchEstimatedCharsPerToken = 3;
constexpr int32 NativeBatchPerRequestOverheadTokens = 256;

void MergeTranslationResponse(
    FN2CTranslationResponse& Target,
    const FN2CTranslationResponse& Source)
{
    for (const FN2CGraphTranslation& Graph : Source.Graphs)
    {
        const int32 ExistingIndex = Target.Graphs.IndexOfByPredicate(
            [&Graph](const FN2CGraphTranslation& Existing)
            {
                return Existing.GraphName.Equals(Graph.GraphName, ESearchCase::CaseSensitive) &&
                       Existing.GraphType.Equals(Graph.GraphType, ESearchCase::CaseSensitive) &&
                       Existing.GraphClass.Equals(Graph.GraphClass, ESearchCase::CaseSensitive);
            });

        if (ExistingIndex == INDEX_NONE)
        {
            Target.Graphs.Add(Graph);
        }
        else
        {
            Target.Graphs[ExistingIndex] = Graph;
        }
    }

    Target.Usage.InputTokens += Source.Usage.InputTokens;
    Target.Usage.OutputTokens += Source.Usage.OutputTokens;
}

int32 EstimateNativeBatchTokens(const FN2CPendingNativeBatchRequest& Request)
{
    // This is deliberately conservative and provider-agnostic. It is only an
    // admission heuristic; provider-side capacity remains authoritative.
    return FMath::Max(1, (Request.FormattedPayload.Len() + NativeBatchEstimatedCharsPerToken - 1) /
        NativeBatchEstimatedCharsPerToken) + NativeBatchPerRequestOverheadTokens;
}

int64 EstimateNativeBatchTokens(const TArray<FN2CPendingNativeBatchRequest>& Requests)
{
    int64 Total = 0;
    for (const FN2CPendingNativeBatchRequest& Request : Requests)
    {
        Total += EstimateNativeBatchTokens(Request);
    }
    return Total;
}

TArray<TArray<FN2CPendingNativeBatchRequest>> BuildNativeBatchChunks(
    TArray<FN2CPendingNativeBatchRequest> Requests)
{
    TArray<TArray<FN2CPendingNativeBatchRequest>> Chunks;
    TArray<FN2CPendingNativeBatchRequest> CurrentChunk;
    int64 CurrentEstimatedTokens = 0;

    for (FN2CPendingNativeBatchRequest& Request : Requests)
    {
        const int32 RequestEstimatedTokens = EstimateNativeBatchTokens(Request);
        if (!CurrentChunk.IsEmpty() &&
            CurrentEstimatedTokens + RequestEstimatedTokens > NativeBatchTargetEstimatedTokens)
        {
            Chunks.Add(MoveTemp(CurrentChunk));
            CurrentChunk.Reset();
            CurrentEstimatedTokens = 0;
        }

        CurrentEstimatedTokens += RequestEstimatedTokens;
        CurrentChunk.Add(MoveTemp(Request));
    }

    if (!CurrentChunk.IsEmpty())
    {
        Chunks.Add(MoveTemp(CurrentChunk));
    }

    return Chunks;
}

bool IsNativeBatchAdmissionCapacityFailure(const FString& Reason)
{
    const FString LowerReason = Reason.ToLower();
    return (LowerReason.Contains(TEXT("enqueued")) && LowerReason.Contains(TEXT("token"))) ||
           LowerReason.Contains(TEXT("capacity")) ||
           LowerReason.Contains(TEXT("batch is too large")) ||
           LowerReason.Contains(TEXT("batch too large")) ||
           LowerReason.Contains(TEXT("batch_size")) ||
           LowerReason.Contains(TEXT("batch size")) ||
           LowerReason.Contains(TEXT("payload too large")) ||
           LowerReason.Contains(TEXT("request too large")) ||
           LowerReason.Contains(TEXT("too many requests")) ||
           LowerReason.Contains(TEXT("http 413"));
}

bool IsNativeBatchAdmissionCapacityResponse(const FString& RawResponse)
{
    const FString LowerResponse = RawResponse.ToLower();
    return LowerResponse.Contains(TEXT("batch_error")) &&
           IsNativeBatchAdmissionCapacityFailure(LowerResponse);
}

TArray<TArray<FN2CPendingNativeBatchRequest>> SplitNativeBatchChunk(
    TArray<FN2CPendingNativeBatchRequest> Requests)
{
    TArray<TArray<FN2CPendingNativeBatchRequest>> Chunks;
    if (Requests.Num() < 2)
    {
        Chunks.Add(MoveTemp(Requests));
        return Chunks;
    }

    const int64 TotalEstimatedTokens = EstimateNativeBatchTokens(Requests);
    int64 LeftEstimatedTokens = 0;
    int64 BestDifference = -1;
    int32 SplitIndex = 1;

    for (int32 Index = 0; Index < Requests.Num() - 1; ++Index)
    {
        LeftEstimatedTokens += EstimateNativeBatchTokens(Requests[Index]);
        const int64 Delta = TotalEstimatedTokens - (LeftEstimatedTokens * 2);
        const int64 Difference = Delta < 0 ? -Delta : Delta;
        if (BestDifference < 0 || Difference < BestDifference)
        {
            BestDifference = Difference;
            SplitIndex = Index + 1;
        }
    }

    TArray<FN2CPendingNativeBatchRequest> Left;
    TArray<FN2CPendingNativeBatchRequest> Right;
    Left.Reserve(SplitIndex);
    Right.Reserve(Requests.Num() - SplitIndex);

    for (int32 Index = 0; Index < Requests.Num(); ++Index)
    {
        if (Index < SplitIndex)
        {
            Left.Add(MoveTemp(Requests[Index]));
        }
        else
        {
            Right.Add(MoveTemp(Requests[Index]));
        }
    }

    Chunks.Add(MoveTemp(Left));
    Chunks.Add(MoveTemp(Right));
    return Chunks;
}

struct FNativeBatchChunkDispatchState
{
    TArray<TArray<FN2CPendingNativeBatchRequest>> Chunks;
    int32 NextChunkIndex = 0;
};

bool QueueSmallerNativeBatchChunks(
    FNativeBatchChunkDispatchState& State,
    TArray<FN2CPendingNativeBatchRequest>& Requests,
    const FString& Reason)
{
    if (Requests.Num() < 2)
    {
        return false;
    }

    const int32 OriginalRequestCount = Requests.Num();
    const int64 OriginalEstimatedTokens = EstimateNativeBatchTokens(Requests);
    TArray<TArray<FN2CPendingNativeBatchRequest>> SmallerChunks =
        SplitNativeBatchChunk(MoveTemp(Requests));
    if (SmallerChunks.Num() <= 1)
    {
        return false;
    }

    const int32 InsertIndex = State.NextChunkIndex;
    const int32 SmallerChunkCount = SmallerChunks.Num();
    for (int32 Index = SmallerChunks.Num() - 1; Index >= 0; --Index)
    {
        State.Chunks.Insert(MoveTemp(SmallerChunks[Index]), InsertIndex);
    }

    FN2CLogger::Get().LogWarning(
        FString::Printf(
            TEXT("Native batch admission rejected for %d requests (~%lld estimated tokens); split into %d smaller chunk(s) and will retry sequentially. Provider response: %s"),
            OriginalRequestCount,
            static_cast<long long>(OriginalEstimatedTokens),
            SmallerChunkCount,
            *Reason),
        TEXT("NativeBatch"));
    return true;
}
}

UN2CLLMModule* UN2CLLMModule::Get()
{
    static UN2CLLMModule* Instance = nullptr;
    if (!Instance)
    {
        Instance = NewObject<UN2CLLMModule>();
        Instance->AddToRoot();
        Instance->CurrentStatus = EN2CSystemStatus::Idle;
        Instance->LatestTranslationPath = TEXT("");
    }
    return Instance;
}

bool UN2CLLMModule::Initialize()
{
    CurrentStatus = EN2CSystemStatus::Initializing;
    bIsInitialized = false;
    ResetRequestSession();
    CurrentStatus = EN2CSystemStatus::Initializing;

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("Failed to load plugin settings"), TEXT("LLMModule"));
        return false;
    }

    Config.Provider = Settings->Provider;
    Config.ApiKey = Settings->GetActiveApiKey();
    Config.Model = Settings->GetActiveModel();

    InitializeProviderRegistry();

    if (!InitializeComponents() || !CreateServiceForProvider(Config.Provider))
    {
        CurrentStatus = EN2CSystemStatus::Error;
        return false;
    }

    bIsInitialized = true;
    CurrentStatus = EN2CSystemStatus::Idle;
    FN2CLogger::Get().Log(TEXT("LLM Module initialized successfully"), EN2CLogSeverity::Info, TEXT("LLMModule"));
    return true;
}

void UN2CLLMModule::ResetRequestSession()
{
    InFlightRequestCount = 0;
    NextRequestId = 1;
    bSessionHadError = false;

    SessionRawResponses.Reset();
    ParsedGraphResponsesByRequestId.Reset();
    PendingNativeBatchRequests.Reset();
    RetryInFlightRequestIds.Reset();

    SessionTranslationResponse.Graphs.Reset();
    SessionTranslationResponse.Usage.InputTokens = 0;
    SessionTranslationResponse.Usage.OutputTokens = 0;

    bNativeBatchFlushScheduled = false;
    PendingConsolidationRetryRequestId = INDEX_NONE;
    PendingConsolidationRetryCompletion = TFunction<void(bool)>();

    CurrentBatchRootPath.Empty();
    CurrentStatus = EN2CSystemStatus::Idle;
}

void UN2CLLMModule::AppendSessionResponse(const FN2CTranslationResponse& Response)
{
    N2CLLMModuleSessionPrivate::MergeTranslationResponse(SessionTranslationResponse, Response);
}

FN2CTranslationResponse UN2CLLMModule::BuildGraphAggregateResponse() const
{
    FN2CTranslationResponse Aggregate;
    Aggregate.Usage.InputTokens = 0;
    Aggregate.Usage.OutputTokens = 0;

    TArray<int32> RequestIds;
    ParsedGraphResponsesByRequestId.GetKeys(RequestIds);
    RequestIds.Sort();

    for (const int32 RequestId : RequestIds)
    {
        if (const FN2CTranslationResponse* Response = ParsedGraphResponsesByRequestId.Find(RequestId))
        {
            N2CLLMModuleSessionPrivate::MergeTranslationResponse(Aggregate, *Response);
        }
    }

    return Aggregate;
}

void UN2CLLMModule::RebuildSessionResponseFromGraphRequests()
{
    SessionTranslationResponse = BuildGraphAggregateResponse();
}

bool UN2CLLMModule::TryQueueNativeBatchRequest(
    const FString& JsonInput,
    const FString& SystemPrompt,
    const FOnLLMResponseReceived& OnComplete)
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (CurrentBatchRootPath.IsEmpty() ||
        !FN2CNativeBatchProcessor::SupportsProvider(Config.Provider) ||
        !Settings ||
        !Settings->IsBatchProcessingEnabledForProvider(Config.Provider))
    {
        return false;
    }

    UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(ActiveService.GetObject());
    if (!BaseService || !BaseService->IsInitialized())
    {
        return false;
    }

    const FString FormattedPayload = BaseService->BuildFormattedRequestPayload(JsonInput, SystemPrompt);
    if (FormattedPayload.IsEmpty())
    {
        return false;
    }

    FN2CPendingNativeBatchRequest Pending;
    Pending.RequestId = NextRequestId++;
    Pending.RequestLabel = FString::Printf(TEXT("Request %d"), Pending.RequestId);
    Pending.FormattedPayload = FormattedPayload;
    Pending.OnComplete = OnComplete;
    PendingNativeBatchRequests.Add(MoveTemp(Pending));

    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    if (!bNativeBatchFlushScheduled)
    {
        bNativeBatchFlushScheduled = true;
        TWeakObjectPtr<UN2CLLMModule> WeakThis(this);
        FTSTicker::GetCoreTicker().AddTicker(
            TEXT("NodeToCode.NativeBatchFlush"),
            0.0f,
            [WeakThis](float)
            {
                if (UN2CLLMModule* StrongThis = WeakThis.Get())
                {
                    StrongThis->FlushNativeBatchRequests();
                }
                return false;
            });
    }

    return true;
}

void UN2CLLMModule::FlushNativeBatchRequests()
{
    bNativeBatchFlushScheduled = false;

    TArray<FN2CPendingNativeBatchRequest> Pending = MoveTemp(PendingNativeBatchRequests);
    PendingNativeBatchRequests.Reset();

    if (Pending.IsEmpty())
    {
        return;
    }

    if (Pending.Num() < 2 || !FN2CNativeBatchProcessor::SupportsProvider(Config.Provider))
    {
        DispatchPendingBatchIndividually(MoveTemp(Pending), TEXT("Not enough requests for provider-native batching"));
        return;
    }

    const int32 TotalRequestCount = Pending.Num();
    const int64 TotalEstimatedTokens = N2CLLMModuleSessionPrivate::EstimateNativeBatchTokens(Pending);
    TSharedRef<N2CLLMModuleSessionPrivate::FNativeBatchChunkDispatchState> State =
        MakeShared<N2CLLMModuleSessionPrivate::FNativeBatchChunkDispatchState>();
    State->Chunks = N2CLLMModuleSessionPrivate::BuildNativeBatchChunks(MoveTemp(Pending));

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Prepared %d provider-native batch chunk(s) from %d requests (~%lld estimated input tokens; target <= %d per chunk)"),
            State->Chunks.Num(),
            TotalRequestCount,
            static_cast<long long>(TotalEstimatedTokens),
            N2CLLMModuleSessionPrivate::NativeBatchTargetEstimatedTokens),
        EN2CLogSeverity::Info,
        TEXT("NativeBatch"));

    TWeakObjectPtr<UN2CLLMModule> WeakThis(this);
    TSharedRef<TFunction<void()>> DispatchNext = MakeShared<TFunction<void()>>();
    TWeakPtr<TFunction<void()>> WeakDispatchNext = DispatchNext;

    *DispatchNext = [WeakThis, State, WeakDispatchNext]() mutable
    {
        UN2CLLMModule* StrongThis = WeakThis.Get();
        TSharedPtr<TFunction<void()>> Dispatch = WeakDispatchNext.Pin();
        if (!StrongThis || !Dispatch.IsValid())
        {
            return;
        }

        while (State->NextChunkIndex < State->Chunks.Num())
        {
            TArray<FN2CPendingNativeBatchRequest> Chunk =
                MoveTemp(State->Chunks[State->NextChunkIndex++]);
            if (Chunk.IsEmpty())
            {
                continue;
            }

            if (Chunk.Num() < 2)
            {
                StrongThis->DispatchPendingBatchIndividually(
                    MoveTemp(Chunk),
                    TEXT("Admission chunk contains only one request"));
                continue;
            }

            TSharedRef<TArray<FN2CPendingNativeBatchRequest>> PendingRef =
                MakeShared<TArray<FN2CPendingNativeBatchRequest>>(MoveTemp(Chunk));
            TSharedRef<FString> DeferredAdmissionFailure = MakeShared<FString>();

            TArray<FN2CNativeBatchRequest> NativeRequests;
            NativeRequests.Reserve(PendingRef->Num());
            for (const FN2CPendingNativeBatchRequest& Item : *PendingRef)
            {
                FN2CNativeBatchRequest Native;
                Native.RequestId = Item.RequestId;
                Native.RequestLabel = Item.RequestLabel;
                Native.FormattedPayload = Item.FormattedPayload;
                NativeRequests.Add(MoveTemp(Native));
            }

            const int64 ChunkEstimatedTokens =
                N2CLLMModuleSessionPrivate::EstimateNativeBatchTokens(*PendingRef);
            FN2CLogger::Get().Log(
                FString::Printf(
                    TEXT("Submitting provider-native batch chunk with %d requests (~%lld estimated input tokens); %d chunk(s) remain queued"),
                    PendingRef->Num(),
                    static_cast<long long>(ChunkEstimatedTokens),
                    State->Chunks.Num() - State->NextChunkIndex),
                EN2CLogSeverity::Info,
                TEXT("NativeBatch"));

            TSharedRef<FN2CNativeBatchProcessor> Processor = FN2CNativeBatchProcessor::Create(
                StrongThis->Config,
                MoveTemp(NativeRequests),
                [WeakThis, PendingRef, DeferredAdmissionFailure](const FN2CNativeBatchResult& Result)
                {
                    UN2CLLMModule* ItemOwner = WeakThis.Get();
                    if (!ItemOwner)
                    {
                        return;
                    }

                    if (N2CLLMModuleSessionPrivate::IsNativeBatchAdmissionCapacityResponse(Result.RawResponse))
                    {
                        if (DeferredAdmissionFailure->IsEmpty())
                        {
                            *DeferredAdmissionFailure = Result.RawResponse;
                        }
                        return;
                    }

                    if (!DeferredAdmissionFailure->IsEmpty())
                    {
                        return;
                    }

                    const FN2CPendingNativeBatchRequest* PendingItem = PendingRef->FindByPredicate(
                        [&Result](const FN2CPendingNativeBatchRequest& Candidate)
                        {
                            return Candidate.RequestId == Result.RequestId;
                        });

                    if (!PendingItem)
                    {
                        return;
                    }

                    ItemOwner->HandleCompletedBatchItem(
                        Result.RequestId,
                        Result.RequestLabel,
                        Result.FormattedPayload,
                        Result.RawResponse,
                        PendingItem->OnComplete);
                },
                [WeakThis, PendingRef, DeferredAdmissionFailure, State, Dispatch]() mutable
                {
                    UN2CLLMModule* BatchOwner = WeakThis.Get();
                    if (!BatchOwner)
                    {
                        return;
                    }

                    if (!DeferredAdmissionFailure->IsEmpty())
                    {
                        if (N2CLLMModuleSessionPrivate::QueueSmallerNativeBatchChunks(
                                *State,
                                *PendingRef,
                                *DeferredAdmissionFailure))
                        {
                            (*Dispatch)();
                            return;
                        }

                        TArray<FN2CPendingNativeBatchRequest> FallbackRequests = MoveTemp(*PendingRef);
                        BatchOwner->DispatchPendingBatchIndividually(
                            MoveTemp(FallbackRequests),
                            *DeferredAdmissionFailure);
                        (*Dispatch)();
                        return;
                    }

                    FN2CLogger::Get().Log(
                        TEXT("Provider-native batch chunk result collection completed"),
                        EN2CLogSeverity::Info,
                        TEXT("NativeBatch"));
                    (*Dispatch)();
                },
                [WeakThis, PendingRef, State, Dispatch](const FString& Reason) mutable
                {
                    UN2CLLMModule* BatchOwner = WeakThis.Get();
                    if (!BatchOwner)
                    {
                        return;
                    }

                    if (N2CLLMModuleSessionPrivate::IsNativeBatchAdmissionCapacityFailure(Reason) &&
                        N2CLLMModuleSessionPrivate::QueueSmallerNativeBatchChunks(
                            *State,
                            *PendingRef,
                            Reason))
                    {
                        (*Dispatch)();
                        return;
                    }

                    TArray<FN2CPendingNativeBatchRequest> FallbackRequests = MoveTemp(*PendingRef);
                    BatchOwner->DispatchPendingBatchIndividually(MoveTemp(FallbackRequests), Reason);
                    (*Dispatch)();
                });

            Processor->Start();
            return;
        }

        FN2CLogger::Get().Log(
            TEXT("Provider-native batch chunk sequence completed"),
            EN2CLogSeverity::Info,
            TEXT("NativeBatch"));
    };

    (*DispatchNext)();
}

void UN2CLLMModule::DispatchPendingBatchIndividually(
    TArray<FN2CPendingNativeBatchRequest> Requests,
    const FString& Reason)
{
    FN2CLogger::Get().LogWarning(
        FString::Printf(TEXT("Using ordinary parallel requests instead of native batch: %s"), *Reason),
        TEXT("NativeBatch"));

    UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(ActiveService.GetObject());
    if (!BaseService || !BaseService->IsInitialized())
    {
        const FString ErrorResponse = TEXT("{\"error\":{\"type\":\"batch_fallback_error\",\"message\":\"Active provider service is unavailable\"}}");
        for (const FN2CPendingNativeBatchRequest& Item : Requests)
        {
            HandleCompletedBatchItem(
                Item.RequestId,
                Item.RequestLabel,
                Item.FormattedPayload,
                ErrorResponse,
                Item.OnComplete);
        }
        return;
    }

    for (const FN2CPendingNativeBatchRequest& Item : Requests)
    {
        BaseService->ResendFormattedRequest(
            Item.FormattedPayload,
            FOnLLMResponseReceived::CreateLambda(
                [this, Item](const FString& Response)
                {
                    HandleCompletedBatchItem(
                        Item.RequestId,
                        Item.RequestLabel,
                        Item.FormattedPayload,
                        Response,
                        Item.OnComplete);
                }));
    }
}

void UN2CLLMModule::HandleCompletedBatchItem(
    int32 RequestId,
    const FString& RequestLabel,
    const FString& RawRequest,
    const FString& Response,
    const FOnLLMResponseReceived& OnComplete)
{
    FN2CTranslationResponse TranslationResponse;
    TranslationResponse.Usage.InputTokens = 0;
    TranslationResponse.Usage.OutputTokens = 0;

    bool bParsedSuccessfully = false;
    FString ResolvedLabel = RequestLabel;

    TScriptInterface<IN2CLLMService> Service = GetActiveService();
    UN2CResponseParserBase* Parser = Service.GetInterface() ? Service->GetResponseParser() : nullptr;
    if (Parser && Parser->ParseLLMResponse(Response, TranslationResponse))
    {
        bParsedSuccessfully = true;
        if (!TranslationResponse.Graphs.IsEmpty() && !TranslationResponse.Graphs[0].GraphName.IsEmpty())
        {
            ResolvedLabel = TranslationResponse.Graphs[0].GraphName;
        }

        ParsedGraphResponsesByRequestId.Add(RequestId, TranslationResponse);
        RebuildSessionResponseFromGraphRequests();

        const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
        if (SaveTranslationToDisk(TranslationResponse, Blueprint))
        {
            FN2CLogger::Get().Log(TEXT("Successfully saved translation to disk"), EN2CLogSeverity::Info);
        }

        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, true);
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Successfully parsed native-batch response for: %s"), *ResolvedLabel),
            EN2CLogSeverity::Info,
            TEXT("NativeBatch"));
    }
    else
    {
        ParsedGraphResponsesByRequestId.Remove(RequestId);
        RebuildSessionResponseFromGraphRequests();
        SaveRawResponseToDisk(Response);
        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse native-batch response for: %s"), *ResolvedLabel),
            TEXT("NativeBatch"));
    }

    FN2CRawResponseRecord Record;
    Record.RequestId = RequestId;
    Record.RequestLabel = ResolvedLabel;
    Record.Provider = Config.Provider;
    Record.Model = Config.Model;
    Record.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
    Record.RawRequest = RawRequest;
    Record.FormattedResponse = FormatRawResponseForDisplay(Response);
    Record.bParsedSuccessfully = bParsedSuccessfully;

    const int32 ExistingIndex = SessionRawResponses.IndexOfByPredicate(
        [RequestId](const FN2CRawResponseRecord& Existing)
        {
            return Existing.RequestId == RequestId;
        });
    if (ExistingIndex == INDEX_NONE)
    {
        SessionRawResponses.Add(MoveTemp(Record));
    }
    else
    {
        SessionRawResponses[ExistingIndex] = MoveTemp(Record);
    }

    PersistRequestHistory();

    // Preserve existing callback ordering: the whole-Blueprint caller can start consolidation
    // after its last logical graph result while this logical request still counts as in flight.
    OnComplete.ExecuteIfBound(Response);
    FinishRequest(bParsedSuccessfully);
}

void UN2CLLMModule::FinishRequest(bool bSuccess)
{
    InFlightRequestCount = FMath::Max(0, InFlightRequestCount - 1);

    // Error state follows the current replaceable history rather than being permanently sticky.
    // A successful resend can therefore recover a previously failed request.
    bSessionHadError = SessionRawResponses.ContainsByPredicate(
        [](const FN2CRawResponseRecord& Record)
        {
            return !Record.bParsedSuccessfully;
        });

    if (InFlightRequestCount > 0)
    {
        CurrentStatus = EN2CSystemStatus::Processing;
    }
    else
    {
        CurrentStatus = bSessionHadError ? EN2CSystemStatus::Error : EN2CSystemStatus::Idle;
    }

    StartQueuedConsolidationIfReady();
}

void UN2CLLMModule::StartQueuedConsolidationIfReady()
{
    if (InFlightRequestCount != 0 || PendingConsolidationRetryRequestId == INDEX_NONE)
    {
        return;
    }

    const int32 RequestId = PendingConsolidationRetryRequestId;
    PendingConsolidationRetryRequestId = INDEX_NONE;
    TFunction<void(bool)> Completion = MoveTemp(PendingConsolidationRetryCompletion);
    PendingConsolidationRetryCompletion = TFunction<void(bool)>();

    FN2CLogger::Get().Log(
        TEXT("All in-progress requests completed; starting queued final consolidation with refreshed graph responses"),
        EN2CLogSeverity::Info,
        TEXT("BatchConsolidation"));

    StartFinalConsolidation(RequestId, MoveTemp(Completion));
}

FString UN2CLLMModule::FormatRawResponseForDisplay(const FString& RawResponse) const
{
    if (RawResponse.IsEmpty())
    {
        return RawResponse;
    }

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawResponse);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return RawResponse;
    }

    FString FormattedResponse;
    TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&FormattedResponse);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return FormattedResponse;
}

void UN2CLLMModule::ProcessN2CJson(
    const FString& JsonInput,
    const FOnLLMResponseReceived& OnComplete)
{
    if (!bIsInitialized)
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("LLM Module not initialized"), TEXT("LLMModule"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"Module not initialized\"}"));
        return;
    }

    TScriptInterface<IN2CLLMService> Service = GetActiveService();
    if (!Service.GetInterface())
    {
        CurrentStatus = EN2CSystemStatus::Error;
        FN2CLogger::Get().LogError(TEXT("No active LLM service"), TEXT("LLMModule"));
        OnComplete.ExecuteIfBound(TEXT("{\"error\": \"No active service\"}"));
        return;
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const FString SystemPrompt = PromptManager->GetLanguageSpecificPrompt(
        TEXT("CodeGen"),
        Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp);

    if (TryQueueNativeBatchRequest(JsonInput, SystemPrompt, OnComplete))
    {
        return;
    }

    const int32 RequestId = NextRequestId++;
    const EN2CLLMProvider RequestProvider = Config.Provider;
    const FString RequestModel = Config.Model;
    const TSharedRef<FString> CapturedRawRequest = MakeShared<FString>();

    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    ActiveService->SendRequest(
        JsonInput,
        SystemPrompt,
        FOnLLMResponseReceived::CreateLambda(
            [this, OnComplete, RequestId, RequestProvider, RequestModel, CapturedRawRequest](const FString& Response)
            {
                FN2CTranslationResponse TranslationResponse;
                TranslationResponse.Usage.InputTokens = 0;
                TranslationResponse.Usage.OutputTokens = 0;
                bool bParsedSuccessfully = false;
                FString RequestLabel = FString::Printf(TEXT("Request %d"), RequestId);

                TScriptInterface<IN2CLLMService> ParserService = GetActiveService();
                UN2CResponseParserBase* Parser = ParserService.GetInterface()
                    ? ParserService->GetResponseParser()
                    : nullptr;

                if (Parser && Parser->ParseLLMResponse(Response, TranslationResponse))
                {
                    bParsedSuccessfully = true;
                    if (!TranslationResponse.Graphs.IsEmpty() && !TranslationResponse.Graphs[0].GraphName.IsEmpty())
                    {
                        RequestLabel = TranslationResponse.Graphs[0].GraphName;
                    }

                    ParsedGraphResponsesByRequestId.Add(RequestId, TranslationResponse);
                    RebuildSessionResponseFromGraphRequests();

                    const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
                    if (SaveTranslationToDisk(TranslationResponse, Blueprint))
                    {
                        FN2CLogger::Get().Log(TEXT("Successfully saved translation to disk"), EN2CLogSeverity::Info);
                    }

                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, true);
                    FN2CLogger::Get().Log(TEXT("Successfully parsed LLM response"), EN2CLogSeverity::Info);
                }
                else
                {
                    ParsedGraphResponsesByRequestId.Remove(RequestId);
                    RebuildSessionResponseFromGraphRequests();
                    FN2CLogger::Get().LogError(TEXT("Failed to parse LLM response"));
                    SaveRawResponseToDisk(Response);
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
                }

                FN2CRawResponseRecord RawRecord;
                RawRecord.RequestId = RequestId;
                RawRecord.RequestLabel = RequestLabel;
                RawRecord.Provider = RequestProvider;
                RawRecord.Model = RequestModel;
                RawRecord.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
                RawRecord.RawRequest = *CapturedRawRequest;
                RawRecord.FormattedResponse = FormatRawResponseForDisplay(Response);
                RawRecord.bParsedSuccessfully = bParsedSuccessfully;
                SessionRawResponses.Add(MoveTemp(RawRecord));
                PersistRequestHistory();

                OnComplete.ExecuteIfBound(Response);
                FinishRequest(bParsedSuccessfully);
            }));

    if (UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(ActiveService.GetObject()))
    {
        *CapturedRawRequest = BaseService->GetLastFormattedRequestPayload();
    }
}

bool UN2CLLMModule::ResendRawRequest(
    int32 RequestId,
    TFunction<void(bool)> OnComplete)
{
    const int32 ExistingIndex = SessionRawResponses.IndexOfByPredicate(
        [RequestId](const FN2CRawResponseRecord& Record)
        {
            return Record.RequestId == RequestId;
        });

    if (ExistingIndex == INDEX_NONE)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Cannot resend unknown raw request #%d"), RequestId),
            TEXT("RawRequestRetry"));
        return false;
    }

    if (RetryInFlightRequestIds.Contains(RequestId) ||
        PendingConsolidationRetryRequestId == RequestId)
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Request #%d is already being resent or queued"), RequestId),
            TEXT("RawRequestRetry"));
        return false;
    }

    const FN2CRawResponseRecord OriginalRecord = SessionRawResponses[ExistingIndex];

    if (OriginalRecord.Provider != Config.Provider ||
        !OriginalRecord.Model.Equals(Config.Model, ESearchCase::CaseSensitive))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(
                TEXT("Cannot resend request #%d because the active provider/model no longer matches it"),
                RequestId),
            TEXT("RawRequestRetry"));
        return false;
    }

    if (OriginalRecord.bFinalConsolidation)
    {
        if (InFlightRequestCount > 0)
        {
            if (PendingConsolidationRetryRequestId != INDEX_NONE)
            {
                FN2CLogger::Get().LogWarning(
                    TEXT("A final consolidation retry is already queued"),
                    TEXT("BatchConsolidation"));
                return false;
            }

            PendingConsolidationRetryRequestId = RequestId;
            PendingConsolidationRetryCompletion = MoveTemp(OnComplete);
            FN2CLogger::Get().Log(
                FString::Printf(
                    TEXT("Queued final consolidation resend until %d in-progress request(s) complete"),
                    InFlightRequestCount),
                EN2CLogSeverity::Info,
                TEXT("BatchConsolidation"));
            return true;
        }

        return StartFinalConsolidation(RequestId, MoveTemp(OnComplete));
    }

    if (OriginalRecord.RawRequest.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Cannot resend request #%d because its captured request body is empty"), RequestId),
            TEXT("RawRequestRetry"));
        return false;
    }

    UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(ActiveService.GetObject());
    if (!BaseService || !BaseService->IsInitialized())
    {
        FN2CLogger::Get().LogError(
            TEXT("Cannot resend raw request because the active LLM service is unavailable"),
            TEXT("RawRequestRetry"));
        return false;
    }

    RetryInFlightRequestIds.Add(RequestId);
    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Replaying request #%d in place using %s / %s"),
            RequestId,
            *UEnum::GetValueAsString(OriginalRecord.Provider),
            *OriginalRecord.Model),
        EN2CLogSeverity::Info,
        TEXT("RawRequestRetry"));

    BaseService->ResendFormattedRequest(
        OriginalRecord.RawRequest,
        FOnLLMResponseReceived::CreateLambda(
            [this, OriginalRecord, RequestId, OnComplete = MoveTemp(OnComplete)](const FString& Response) mutable
            {
                bool bParsedSuccessfully = false;
                FN2CTranslationResponse RetriedResponse;
                RetriedResponse.Usage.InputTokens = 0;
                RetriedResponse.Usage.OutputTokens = 0;
                FString RequestLabel = OriginalRecord.RequestLabel;

                TScriptInterface<IN2CLLMService> Service = GetActiveService();
                UN2CResponseParserBase* Parser = Service.GetInterface() ? Service->GetResponseParser() : nullptr;
                if (Parser && Parser->ParseLLMResponse(Response, RetriedResponse))
                {
                    bParsedSuccessfully = true;
                    if (!RetriedResponse.Graphs.IsEmpty() && !RetriedResponse.Graphs[0].GraphName.IsEmpty())
                    {
                        RequestLabel = RetriedResponse.Graphs[0].GraphName;
                    }
                    ParsedGraphResponsesByRequestId.Add(RequestId, RetriedResponse);
                }
                else
                {
                    ParsedGraphResponsesByRequestId.Remove(RequestId);
                    SaveRawResponseToDisk(Response);
                }

                RebuildSessionResponseFromGraphRequests();

                // Rewrite the existing batch manifest with the newest graph-phase state. C++ batch
                // code files remain untouched until the user runs/refreshed final consolidation.
                if (!LatestTranslationPath.IsEmpty())
                {
                    const FString PreviousBatchRoot = CurrentBatchRootPath;
                    CurrentBatchRootPath = LatestTranslationPath;
                    const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
                    SaveTranslationToDisk(SessionTranslationResponse, Blueprint);
                    CurrentBatchRootPath = PreviousBatchRoot;
                }

                const int32 RecordIndex = SessionRawResponses.IndexOfByPredicate(
                    [RequestId](const FN2CRawResponseRecord& Record)
                    {
                        return Record.RequestId == RequestId;
                    });
                if (RecordIndex != INDEX_NONE)
                {
                    FN2CRawResponseRecord& Record = SessionRawResponses[RecordIndex];
                    Record.RequestLabel = RequestLabel;
                    Record.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
                    Record.RawRequest = OriginalRecord.RawRequest;
                    Record.FormattedResponse = FormatRawResponseForDisplay(Response);
                    Record.bParsedSuccessfully = bParsedSuccessfully;
                    Record.RetriedFromRequestId = 0;
                    Record.bFinalConsolidation = false;
                }

                PersistRequestHistory();
                OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, bParsedSuccessfully);

                if (bParsedSuccessfully)
                {
                    FN2CLogger::Get().Log(
                        FString::Printf(TEXT("Successfully replaced and re-parsed request #%d"), RequestId),
                        EN2CLogSeverity::Info,
                        TEXT("RawRequestRetry"));
                }
                else
                {
                    FN2CLogger::Get().LogError(
                        FString::Printf(TEXT("Replacement response for request #%d failed to parse"), RequestId),
                        TEXT("RawRequestRetry"));
                }

                RetryInFlightRequestIds.Remove(RequestId);
                FinishRequest(bParsedSuccessfully);
                if (OnComplete)
                {
                    OnComplete(bParsedSuccessfully);
                }
            }));

    return true;
}

bool UN2CLLMModule::StartFinalConsolidation(
    int32 ExistingRequestId,
    TFunction<void(bool)> OnComplete)
{
    if (!ActiveService.GetInterface())
    {
        FN2CLogger::Get().LogError(
            TEXT("Cannot run final Blueprint consolidation because no active LLM service is available"),
            TEXT("BatchConsolidation"));
        return false;
    }

    UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(ActiveService.GetObject());
    if (!BaseService || !BaseService->IsInitialized())
    {
        FN2CLogger::Get().LogError(
            TEXT("Cannot format final Blueprint consolidation because the active service is unavailable"),
            TEXT("BatchConsolidation"));
        return false;
    }

    const FN2CTranslationResponse GraphAggregate = BuildGraphAggregateResponse();
    if (GraphAggregate.Graphs.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            TEXT("Cannot run final Blueprint consolidation because there are no successful graph translations"),
            TEXT("BatchConsolidation"));
        return false;
    }

    const FString BatchRootPath = !CurrentBatchRootPath.IsEmpty()
        ? CurrentBatchRootPath
        : LatestTranslationPath;
    if (BatchRootPath.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            TEXT("Cannot run final Blueprint consolidation because the translation batch path is unavailable"),
            TEXT("BatchConsolidation"));
        return false;
    }

    const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
    FString ConsolidationPayload;
    if (!FN2CBatchTranslationConsolidator::BuildRequestPayload(
            GraphAggregate,
            Blueprint,
            ConsolidationPayload))
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to build final Blueprint consolidation request"),
            TEXT("BatchConsolidation"));
        return false;
    }

    const FString ConsolidationPrompt = FN2CBatchTranslationConsolidator::GetSystemPrompt();
    const FString FormattedRequest = BaseService->BuildFormattedRequestPayload(
        ConsolidationPayload,
        ConsolidationPrompt);
    if (FormattedRequest.IsEmpty())
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to format final Blueprint consolidation request"),
            TEXT("BatchConsolidation"));
        return false;
    }

    const bool bReplacingExisting = ExistingRequestId != INDEX_NONE;
    const int32 RequestId = bReplacingExisting ? ExistingRequestId : NextRequestId++;
    const EN2CLLMProvider RequestProvider = Config.Provider;
    const FString RequestModel = Config.Model;
    const int32 PriorInputTokens = GraphAggregate.Usage.InputTokens;
    const int32 PriorOutputTokens = GraphAggregate.Usage.OutputTokens;

    // While consolidation is running, expose graph-phase state rather than any stale prior final response.
    SessionTranslationResponse = GraphAggregate;

    if (bReplacingExisting)
    {
        RetryInFlightRequestIds.Add(RequestId);
    }

    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Starting %sfinal Blueprint consolidation request with %d current parsed graph translation(s)"),
            bReplacingExisting ? TEXT("replacement ") : TEXT(""),
            GraphAggregate.Graphs.Num()),
        EN2CLogSeverity::Info,
        TEXT("BatchConsolidation"));

    BaseService->ResendFormattedRequest(
        FormattedRequest,
        FOnLLMResponseReceived::CreateLambda(
            [this,
             BatchRootPath,
             PriorInputTokens,
             PriorOutputTokens,
             RequestId,
             RequestProvider,
             RequestModel,
             FormattedRequest,
             bReplacingExisting,
             OnComplete = MoveTemp(OnComplete)](const FString& Response) mutable
            {
                bool bSuccess = false;
                FN2CTranslationResponse FinalResponse;
                FinalResponse.Usage.InputTokens = 0;
                FinalResponse.Usage.OutputTokens = 0;
                FString ValidationError;

                TScriptInterface<IN2CLLMService> Service = GetActiveService();
                UN2CResponseParserBase* Parser = Service.GetInterface() ? Service->GetResponseParser() : nullptr;

                if (!Parser)
                {
                    ValidationError = TEXT("No response parser available for final Blueprint consolidation");
                }
                else if (!Parser->ParseLLMResponse(Response, FinalResponse))
                {
                    ValidationError = TEXT("Failed to parse final Blueprint consolidation response");
                }
                else if (!FN2CBatchTranslationConsolidator::ValidateFinalResponse(FinalResponse, ValidationError))
                {
                    // ValidationError populated by structural validator.
                }
                else
                {
                    FinalResponse.Usage.InputTokens += PriorInputTokens;
                    FinalResponse.Usage.OutputTokens += PriorOutputTokens;
                    SessionTranslationResponse = FinalResponse;

                    const FString PreviousBatchRoot = CurrentBatchRootPath;
                    CurrentBatchRootPath = BatchRootPath;
                    const FN2CBlueprint& CurrentBlueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
                    const bool bManifestSaved = SaveTranslationToDisk(SessionTranslationResponse, CurrentBlueprint);
                    const bool bCodeSaved = FN2CBatchTranslationConsolidator::SaveCppFiles(
                        SessionTranslationResponse,
                        BatchRootPath);
                    CurrentBatchRootPath = PreviousBatchRoot;

                    bSuccess = bManifestSaved && bCodeSaved;
                    if (!bManifestSaved)
                    {
                        ValidationError = TEXT("Failed to save final consolidated translation manifest");
                    }
                    else if (!bCodeSaved)
                    {
                        ValidationError = TEXT("Failed to save final consolidated C++ pair");
                    }
                }

                FN2CRawResponseRecord Record;
                Record.RequestId = RequestId;
                Record.RequestLabel = TEXT("Final Consolidation");
                Record.Provider = RequestProvider;
                Record.Model = RequestModel;
                Record.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
                Record.RawRequest = FormattedRequest;
                Record.FormattedResponse = FormatRawResponseForDisplay(Response);
                Record.bParsedSuccessfully = bSuccess;
                Record.RetriedFromRequestId = 0;
                Record.bFinalConsolidation = true;

                const int32 ExistingIndex = SessionRawResponses.IndexOfByPredicate(
                    [RequestId](const FN2CRawResponseRecord& Existing)
                    {
                        return Existing.RequestId == RequestId;
                    });
                if (ExistingIndex == INDEX_NONE)
                {
                    SessionRawResponses.Add(MoveTemp(Record));
                }
                else
                {
                    SessionRawResponses[ExistingIndex] = MoveTemp(Record);
                }

                PersistRequestHistory();

                if (bSuccess)
                {
                    FN2CLogger::Get().Log(
                        TEXT("Final Blueprint consolidation completed successfully"),
                        EN2CLogSeverity::Info,
                        TEXT("BatchConsolidation"));
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, true);
                }
                else
                {
                    // Keep the current graph aggregate authoritative after a failed replacement.
                    SessionTranslationResponse = BuildGraphAggregateResponse();
                    FN2CLogger::Get().LogError(
                        FString::Printf(TEXT("Final Blueprint consolidation failed: %s"), *ValidationError),
                        TEXT("BatchConsolidation"));
                    SaveRawResponseToDisk(Response);
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
                }

                if (!bReplacingExisting)
                {
                    CurrentBatchRootPath.Empty();
                    FN2CLogger::Get().Log(TEXT("Batch translation ended"), EN2CLogSeverity::Info);
                }
                else
                {
                    RetryInFlightRequestIds.Remove(RequestId);
                }

                FinishRequest(bSuccess);
                if (OnComplete)
                {
                    OnComplete(bSuccess);
                }
            }));

    return true;
}

bool UN2CLLMModule::InitializeComponents()
{
    PromptManager = NewObject<UN2CSystemPromptManager>(this);
    if (!PromptManager)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create prompt manager"), TEXT("LLMModule"));
        return false;
    }
    PromptManager->Initialize(Config);
    return true;
}

void UN2CLLMModule::OpenTranslationFolder(bool& Success)
{
    FString PathToOpen = LatestTranslationPath;
    if (PathToOpen.IsEmpty())
    {
        FN2CLogger::Get().LogWarning(TEXT("No translation path available, opening the base path"));
        PathToOpen = GetTranslationBasePath();
    }

    if (!FPaths::DirectoryExists(PathToOpen))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Translation directory does not exist: %s. Opening the base path"), *PathToOpen));
        PathToOpen = GetTranslationBasePath();
    }

#if PLATFORM_WINDOWS || PLATFORM_MAC
    FPlatformProcess::ExploreFolder(*PathToOpen);
    Success = true;
#else
    Success = false;
#endif
}

void UN2CLLMModule::BeginBatchTranslation(const FString& BlueprintName)
{
    const FString BlueprintNameToUse = BlueprintName.IsEmpty() ? TEXT("UnknownBlueprint") : BlueprintName;
    CurrentBatchRootPath = GenerateTranslationRootPath(BlueprintNameToUse);
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Batch translation started, root path: %s"), *CurrentBatchRootPath),
        EN2CLogSeverity::Info);
}

void UN2CLLMModule::EndBatchTranslation()
{
    if (CurrentBatchRootPath.IsEmpty())
    {
        FN2CLogger::Get().Log(TEXT("Batch translation ended"), EN2CLogSeverity::Info);
        return;
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const EN2CCodeLanguage TargetLanguage = Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;
    const FN2CTranslationResponse GraphAggregate = BuildGraphAggregateResponse();
    SessionTranslationResponse = GraphAggregate;

    if (TargetLanguage != EN2CCodeLanguage::Cpp || GraphAggregate.Graphs.IsEmpty())
    {
        PersistRequestHistory();
        CurrentBatchRootPath.Empty();
        FN2CLogger::Get().Log(TEXT("Batch translation ended"), EN2CLogSeverity::Info);
        return;
    }

    if (!StartFinalConsolidation())
    {
        bSessionHadError = true;
        PersistRequestHistory();
        CurrentBatchRootPath.Empty();
        OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
    }
}

void UN2CLLMModule::PersistRequestHistory() const
{
    const FString HistoryRoot = !CurrentBatchRootPath.IsEmpty()
        ? CurrentBatchRootPath
        : LatestTranslationPath;

    if (HistoryRoot.IsEmpty() || !FPaths::DirectoryExists(HistoryRoot))
    {
        return;
    }

    if (!FN2CTranslationHistory::SaveRequestHistory(HistoryRoot, SessionRawResponses))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Failed to persist request history: %s"), *HistoryRoot),
            TEXT("TranslationHistory"));
    }
}

bool UN2CLLMModule::SaveTranslationToDisk(
    const FN2CTranslationResponse& Response,
    const FN2CBlueprint& Blueprint)
{
    FString BlueprintName = Blueprint.Metadata.Name;
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = TEXT("UnknownBlueprint");
    }

    const FString RootPath = !CurrentBatchRootPath.IsEmpty()
        ? CurrentBatchRootPath
        : GenerateTranslationRootPath(BlueprintName);

    if (!EnsureDirectoryExists(RootPath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to create translation directory: %s"), *RootPath));
        return false;
    }

    LatestTranslationPath = RootPath;

    const FString JsonFileName = FString::Printf(TEXT("N2C_BP_%s.json"), *FPaths::GetBaseFilename(RootPath));
    const FString JsonFilePath = FPaths::Combine(RootPath, JsonFileName);
    FN2CSerializer::SetPrettyPrint(true);
    const FString JsonContent = FN2CSerializer::ToJson(Blueprint);
    if (!FFileHelper::SaveStringToFile(JsonContent, *JsonFilePath))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save JSON file: %s"), *JsonFilePath));
        return false;
    }

    const FString MinifiedJsonFileName = FString::Printf(
        TEXT("N2C_BP_Minified_%s.json"),
        *FPaths::GetBaseFilename(RootPath));
    const FString MinifiedJsonFilePath = FPaths::Combine(RootPath, MinifiedJsonFileName);
    FN2CSerializer::SetPrettyPrint(false);
    const FString MinifiedJsonContent = FN2CSerializer::ToJson(Blueprint);
    if (!FFileHelper::SaveStringToFile(MinifiedJsonContent, *MinifiedJsonFilePath))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Failed to save minified JSON file: %s"), *MinifiedJsonFilePath));
    }

    const FN2CTranslationResponse& ManifestResponse =
        !CurrentBatchRootPath.IsEmpty() ? SessionTranslationResponse : Response;

    const FString TranslationJsonFileName = FString::Printf(
        TEXT("N2C_Translation_%s.json"),
        *FPaths::GetBaseFilename(RootPath));
    const FString TranslationJsonFilePath = FPaths::Combine(RootPath, TranslationJsonFileName);

    TSharedPtr<FJsonObject> TranslationJsonObject = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (const FN2CGraphTranslation& Graph : ManifestResponse.Graphs)
    {
        TSharedPtr<FJsonObject> GraphObject = MakeShared<FJsonObject>();
        GraphObject->SetStringField(TEXT("graph_name"), Graph.GraphName);
        GraphObject->SetStringField(TEXT("graph_type"), Graph.GraphType);
        GraphObject->SetStringField(TEXT("graph_class"), Graph.GraphClass);

        TSharedPtr<FJsonObject> CodeObject = MakeShared<FJsonObject>();
        CodeObject->SetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
        CodeObject->SetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
        CodeObject->SetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
        GraphObject->SetObjectField(TEXT("code"), CodeObject);
        GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObject));
    }
    TranslationJsonObject->SetArrayField(TEXT("graphs"), GraphsArray);

    if (ManifestResponse.Usage.InputTokens > 0 || ManifestResponse.Usage.OutputTokens > 0)
    {
        TSharedPtr<FJsonObject> UsageObject = MakeShared<FJsonObject>();
        UsageObject->SetNumberField(TEXT("input_tokens"), ManifestResponse.Usage.InputTokens);
        UsageObject->SetNumberField(TEXT("output_tokens"), ManifestResponse.Usage.OutputTokens);
        TranslationJsonObject->SetObjectField(TEXT("usage"), UsageObject);
    }

    FString TranslationJsonContent;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&TranslationJsonContent);
    FJsonSerializer::Serialize(TranslationJsonObject.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(TranslationJsonContent, *TranslationJsonFilePath))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Failed to save translation JSON file: %s"), *TranslationJsonFilePath));
    }

    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    const EN2CCodeLanguage TargetLanguage = Settings ? Settings->TargetLanguage : EN2CCodeLanguage::Cpp;
    const bool bIsBatchMode = !CurrentBatchRootPath.IsEmpty();

    if (bIsBatchMode)
    {
        if (TargetLanguage != EN2CCodeLanguage::Cpp)
        {
            SaveGraphFilesWithBatchFeatures(Response, RootPath, TargetLanguage);
        }
    }
    else
    {
        SaveGraphFilesOriginal(Response, RootPath, TargetLanguage);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Translation saved to: %s"), *RootPath), EN2CLogSeverity::Info);
    return true;
}

void UN2CLLMModule::SaveRawResponseToDisk(const FString& RawResponse)
{
    if (RawResponse.IsEmpty())
    {
        return;
    }

    const FString RootPath = !CurrentBatchRootPath.IsEmpty()
        ? CurrentBatchRootPath
        : GenerateTranslationRootPath(TEXT("ParseFailure"));

    if (!EnsureDirectoryExists(RootPath))
    {
        return;
    }

    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d-%H.%M.%S.%f"));
    const FString RawFileName = FString::Printf(TEXT("RAW_RESPONSE_%s.txt"), *Timestamp);
    const FString RawFilePath = FPaths::Combine(RootPath, RawFileName);

    if (FFileHelper::SaveStringToFile(RawResponse, *RawFilePath))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Raw LLM response saved for debugging: %s"), *RawFilePath),
            TEXT("LLMModule"));
    }
    else
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to save raw response to: %s"), *RawFilePath),
            TEXT("LLMModule"));
    }
}

void UN2CLLMModule::SaveGraphFilesWithBatchFeatures(
    const FN2CTranslationResponse& Response,
    const FString& RootPath,
    EN2CCodeLanguage TargetLanguage) const
{
    const bool bIsCpp = TargetLanguage == EN2CCodeLanguage::Cpp;

    auto SanitizeNameForFilesystem = [](const FString& InName)
    {
        FString Result = InName.TrimStartAndEnd();
        const TCHAR InvalidChars[] =
        {
            TEXT('<'), TEXT('>'), TEXT(':'), TEXT('"'), TEXT('/'),
            TEXT('\\'), TEXT('|'), TEXT('?'), TEXT('*')
        };
        for (TCHAR Ch : InvalidChars)
        {
            FString From;
            From.AppendChar(Ch);
            Result.ReplaceInline(*From, TEXT("_"), ESearchCase::CaseSensitive);
        }
        return Result;
    };

    for (const FN2CGraphTranslation& Graph : Response.Graphs)
    {
        if (Graph.GraphName.IsEmpty())
        {
            continue;
        }

        const FString SanitizedGraphName = SanitizeNameForFilesystem(Graph.GraphName);
        const bool bIsClassItSelf = Graph.GraphType.Equals(TEXT("ClassItSelf"), ESearchCase::IgnoreCase);
        const bool bHasGraphClass = !Graph.GraphClass.IsEmpty();

        if (bIsClassItSelf && bHasGraphClass)
        {
            const FString ClassDir = FPaths::Combine(RootPath, Graph.GraphClass);
            if (!EnsureDirectoryExists(ClassDir))
            {
                continue;
            }

            if (bIsCpp && !Graph.Code.GraphDeclaration.IsEmpty())
            {
                const FString HeaderPath = FPaths::Combine(ClassDir, Graph.GraphClass + TEXT(".h"));
                if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
                {
                    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save class header file: %s"), *HeaderPath));
                }
            }

            if (!Graph.Code.GraphImplementation.IsEmpty())
            {
                const FString ImplPath = FPaths::Combine(
                    ClassDir,
                    Graph.GraphClass + GetFileExtensionForLanguage(TargetLanguage));
                if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath))
                {
                    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save class implementation file: %s"), *ImplPath));
                }
            }

            if (!Graph.Code.ImplementationNotes.IsEmpty())
            {
                const FString NotesPath = FPaths::Combine(ClassDir, Graph.GraphClass + TEXT("_Notes.txt"));
                FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath);
            }
            continue;
        }

        const FString GraphDir = FPaths::Combine(RootPath, SanitizedGraphName);
        if (!EnsureDirectoryExists(GraphDir))
        {
            continue;
        }

        if (bIsCpp && !Graph.Code.GraphDeclaration.IsEmpty())
        {
            const FString HeaderPath = FPaths::Combine(GraphDir, SanitizedGraphName + TEXT(".h"));
            FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath);
        }
        if (!Graph.Code.GraphImplementation.IsEmpty())
        {
            const FString ImplPath = FPaths::Combine(
                GraphDir,
                SanitizedGraphName + GetFileExtensionForLanguage(TargetLanguage));
            FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath);
        }
        if (!Graph.Code.ImplementationNotes.IsEmpty())
        {
            const FString NotesPath = FPaths::Combine(GraphDir, SanitizedGraphName + TEXT("_Notes.txt"));
            FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath);
        }
    }
}

void UN2CLLMModule::SaveGraphFilesOriginal(
    const FN2CTranslationResponse& Response,
    const FString& RootPath,
    EN2CCodeLanguage TargetLanguage) const
{
    for (const FN2CGraphTranslation& Graph : Response.Graphs)
    {
        if (Graph.GraphName.IsEmpty())
        {
            continue;
        }

        const FString GraphDir = FPaths::Combine(RootPath, Graph.GraphName);
        if (!EnsureDirectoryExists(GraphDir))
        {
            continue;
        }

        if (TargetLanguage == EN2CCodeLanguage::Cpp && !Graph.Code.GraphDeclaration.IsEmpty())
        {
            const FString HeaderPath = FPaths::Combine(GraphDir, Graph.GraphName + TEXT(".h"));
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphDeclaration, *HeaderPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save header file: %s"), *HeaderPath));
            }
        }

        if (!Graph.Code.GraphImplementation.IsEmpty())
        {
            const FString ImplPath = FPaths::Combine(
                GraphDir,
                Graph.GraphName + GetFileExtensionForLanguage(TargetLanguage));
            if (!FFileHelper::SaveStringToFile(Graph.Code.GraphImplementation, *ImplPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save implementation file: %s"), *ImplPath));
            }
        }

        if (!Graph.Code.ImplementationNotes.IsEmpty())
        {
            const FString NotesPath = FPaths::Combine(GraphDir, Graph.GraphName + TEXT("_Notes.txt"));
            if (!FFileHelper::SaveStringToFile(Graph.Code.ImplementationNotes, *NotesPath))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save notes file: %s"), *NotesPath));
            }
        }
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Translation saved to: %s"), *RootPath), EN2CLogSeverity::Info);
}

FString UN2CLLMModule::GenerateTranslationRootPath(const FString& BlueprintName) const
{
    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d-%H.%M.%S"));
    return FPaths::Combine(
        GetTranslationBasePath(),
        FString::Printf(TEXT("%s_%s"), *BlueprintName, *Timestamp));
}

FString UN2CLLMModule::GetTranslationBasePath() const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (Settings && !Settings->CustomTranslationOutputDirectory.Path.IsEmpty())
    {
        return Settings->CustomTranslationOutputDirectory.Path;
    }

    return FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Translations");
}

FString UN2CLLMModule::GetFileExtensionForLanguage(EN2CCodeLanguage Language) const
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp: return TEXT(".cpp");
        case EN2CCodeLanguage::Python: return TEXT(".py");
        case EN2CCodeLanguage::JavaScript: return TEXT(".js");
        case EN2CCodeLanguage::CSharp: return TEXT(".cs");
        case EN2CCodeLanguage::Swift: return TEXT(".swift");
        case EN2CCodeLanguage::Pseudocode: return TEXT(".md");
        default: return TEXT(".txt");
    }
}

bool UN2CLLMModule::EnsureDirectoryExists(const FString& DirectoryPath) const
{
    if (FPaths::DirectoryExists(DirectoryPath))
    {
        return true;
    }

    const bool bSuccess = FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*DirectoryPath);
    if (!bSuccess)
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to create directory: %s"), *DirectoryPath));
        return false;
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Created directory: %s"), *DirectoryPath),
        EN2CLogSeverity::Info);
    return true;
}

bool UN2CLLMModule::CreateServiceForProvider(EN2CLLMProvider Provider)
{
    UN2CLLMProviderRegistry* Registry = UN2CLLMProviderRegistry::Get();
    if (!Registry->IsProviderRegistered(Provider))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Provider type not registered: %s"), *UEnum::GetValueAsString(Provider)),
            TEXT("LLMModule"));
        return false;
    }

    TScriptInterface<IN2CLLMService> ServiceInterface = Registry->CreateProvider(Provider, this);
    if (!ServiceInterface.GetInterface())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to create service for provider type: %s"), *UEnum::GetValueAsString(Provider)),
            TEXT("LLMModule"));
        return false;
    }

    if (!ServiceInterface.GetInterface()->Initialize(Config))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to initialize service"), TEXT("LLMModule"));
        return false;
    }

    ActiveService = ServiceInterface;
    return true;
}

void UN2CLLMModule::InitializeProviderRegistry()
{
    UN2CLLMProviderRegistry* Registry = UN2CLLMProviderRegistry::Get();
    Registry->RegisterProvider(EN2CLLMProvider::OpenAI, UN2COpenAIService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::Anthropic, UN2CAnthropicService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::Gemini, UN2CGeminiService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::DeepSeek, UN2CDeepSeekService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::Ollama, UN2COllamaService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::LMStudio, UN2CLMStudioService::StaticClass());
    Registry->RegisterProvider(EN2CLLMProvider::MiniMax, UN2CMiniMaxService::StaticClass());

    FN2CLogger::Get().Log(TEXT("Provider registry initialized"), EN2CLogSeverity::Info, TEXT("LLMModule"));
}
