// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMModule.h"

#include "Containers/Ticker.h"
#include "Core/N2CNodeTranslator.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CBatchTranslationConsolidator.h"
#include "LLM/N2CLLMProviderRegistry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/N2CLogger.h"

namespace N2CRetryServicePrivate
{
void AppendText(FString& Target, const FString& Text)
{
    if (Text.IsEmpty())
    {
        return;
    }
    if (!Target.IsEmpty())
    {
        Target += TEXT("\n\n");
    }
    Target += Text;
}

FString ExtractMessageContent(const TSharedPtr<FJsonObject>& Message)
{
    if (!Message.IsValid())
    {
        return FString();
    }

    FString DirectContent;
    if (Message->TryGetStringField(TEXT("content"), DirectContent))
    {
        return DirectContent;
    }

    FString Result;
    const TArray<TSharedPtr<FJsonValue>>* ContentValues = nullptr;
    if (Message->TryGetArrayField(TEXT("content"), ContentValues) && ContentValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *ContentValues)
        {
            if (!Value.IsValid())
            {
                continue;
            }

            const TSharedPtr<FJsonObject> Part = Value->AsObject();
            if (!Part.IsValid())
            {
                continue;
            }

            FString Text;
            if (Part->TryGetStringField(TEXT("text"), Text) ||
                Part->TryGetStringField(TEXT("content"), Text))
            {
                AppendText(Result, Text);
            }
        }
    }
    return Result;
}

FString ExtractPartsText(const TSharedPtr<FJsonObject>& Object)
{
    if (!Object.IsValid())
    {
        return FString();
    }

    FString Result;
    const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
    if (Object->TryGetArrayField(TEXT("parts"), Parts) && Parts)
    {
        for (const TSharedPtr<FJsonValue>& Value : *Parts)
        {
            const TSharedPtr<FJsonObject> Part = Value.IsValid() ? Value->AsObject() : nullptr;
            if (!Part.IsValid())
            {
                continue;
            }

            FString Text;
            if (Part->TryGetStringField(TEXT("text"), Text))
            {
                AppendText(Result, Text);
            }
        }
    }
    return Result;
}

bool ExtractPreparedMessagesFromWireRequest(
    const FN2CRawResponseRecord& Record,
    FString& OutUserMessage,
    FString& OutSystemMessage)
{
    OutUserMessage = Record.SourceRequestPayload;
    OutSystemMessage = Record.SourceSystemPrompt;
    if (!OutUserMessage.IsEmpty())
    {
        return true;
    }

    if (Record.RawRequest.IsEmpty())
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Record.RawRequest);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    if (Record.Provider == EN2CLLMProvider::Gemini)
    {
        const TSharedPtr<FJsonObject>* SystemInstruction = nullptr;
        if (Root->TryGetObjectField(TEXT("systemInstruction"), SystemInstruction) &&
            SystemInstruction && SystemInstruction->IsValid())
        {
            OutSystemMessage = ExtractPartsText(*SystemInstruction);
        }

        const TArray<TSharedPtr<FJsonValue>>* Contents = nullptr;
        if (Root->TryGetArrayField(TEXT("contents"), Contents) && Contents)
        {
            for (const TSharedPtr<FJsonValue>& Value : *Contents)
            {
                const TSharedPtr<FJsonObject> Message = Value.IsValid() ? Value->AsObject() : nullptr;
                if (!Message.IsValid())
                {
                    continue;
                }

                FString Role;
                Message->TryGetStringField(TEXT("role"), Role);
                if (Role.IsEmpty() || Role.Equals(TEXT("user"), ESearchCase::IgnoreCase))
                {
                    AppendText(OutUserMessage, ExtractPartsText(Message));
                }
            }
        }
        return !OutUserMessage.IsEmpty();
    }

    if (Record.Provider == EN2CLLMProvider::Anthropic)
    {
        Root->TryGetStringField(TEXT("system"), OutSystemMessage);
    }

    const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
    if (!Root->TryGetArrayField(TEXT("messages"), Messages) || !Messages)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Messages)
    {
        const TSharedPtr<FJsonObject> Message = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Message.IsValid())
        {
            continue;
        }

        FString Role;
        Message->TryGetStringField(TEXT("role"), Role);
        const FString Content = ExtractMessageContent(Message);
        if (Role.Equals(TEXT("system"), ESearchCase::IgnoreCase))
        {
            AppendText(OutSystemMessage, Content);
        }
        else if (Role.Equals(TEXT("user"), ESearchCase::IgnoreCase))
        {
            AppendText(OutUserMessage, Content);
        }
    }

    return !OutUserMessage.IsEmpty();
}
}

TScriptInterface<IN2CLLMService> UN2CLLMModule::CreateTransientService(
    const FN2CLLMConfig& ServiceConfig)
{
    TScriptInterface<IN2CLLMService> Service =
        UN2CLLMProviderRegistry::Get()->CreateProviderDirect(ServiceConfig.Provider, this);
    if (!Service.GetInterface())
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to create selected retry provider service"),
            TEXT("RawRequestRetry"));
        return TScriptInterface<IN2CLLMService>();
    }

    if (!Service->Initialize(ServiceConfig))
    {
        FN2CLogger::Get().LogError(
            TEXT("Failed to initialize selected retry provider service"),
            TEXT("RawRequestRetry"));
        return TScriptInterface<IN2CLLMService>();
    }

    if (UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(Service.GetObject()))
    {
        RetryServiceObjects.AddUnique(BaseService);
    }
    return Service;
}

void UN2CLLMModule::ReleaseTransientService(UObject* ServiceObject)
{
    RetryServiceObjects.RemoveAll(
        [ServiceObject](const TObjectPtr<UN2CBaseLLMService>& Item)
        {
            return Item.Get() == ServiceObject;
        });
}

bool UN2CLLMModule::ResendRawRequest(
    int32 RequestId,
    const FN2CLLMConfig& RetryConfig,
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
    if (RetryConfig.Model.TrimStartAndEnd().IsEmpty())
    {
        FN2CLogger::Get().LogError(TEXT("Cannot resend with an empty model identifier"), TEXT("RawRequestRetry"));
        return false;
    }

    if (OriginalRecord.bFinalConsolidation)
    {
        RetryInFlightRequestIds.Add(RequestId);

        if (InFlightRequestCount > 0)
        {
            FN2CLogger::Get().Log(
                FString::Printf(
                    TEXT("Queued final consolidation resend through %s / %s until %d in-progress request(s) complete"),
                    *UEnum::GetValueAsString(RetryConfig.Provider),
                    *RetryConfig.Model,
                    InFlightRequestCount),
                EN2CLogSeverity::Info,
                TEXT("BatchConsolidation"));

            TWeakObjectPtr<UN2CLLMModule> WeakThis(this);
            FTSTicker::GetCoreTicker().AddTicker(
                TEXT("NodeToCode.SelectedConsolidationRetry"),
                0.25f,
                [WeakThis, RequestId, RetryConfig, OnComplete = MoveTemp(OnComplete)](float) mutable
                {
                    UN2CLLMModule* StrongThis = WeakThis.Get();
                    if (!StrongThis)
                    {
                        if (OnComplete)
                        {
                            OnComplete(false);
                        }
                        return false;
                    }

                    if (StrongThis->InFlightRequestCount > 0)
                    {
                        return true;
                    }

                    StrongThis->StartFinalConsolidationWithConfig(
                        RequestId,
                        RetryConfig,
                        MoveTemp(OnComplete));
                    return false;
                });
            return true;
        }

        return StartFinalConsolidationWithConfig(RequestId, RetryConfig, MoveTemp(OnComplete));
    }

    FString PreparedUserMessage;
    FString PreparedSystemMessage;
    if (!N2CRetryServicePrivate::ExtractPreparedMessagesFromWireRequest(
            OriginalRecord,
            PreparedUserMessage,
            PreparedSystemMessage))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(
                TEXT("Cannot reformat request #%d for another provider because its semantic messages could not be recovered"),
                RequestId),
            TEXT("RawRequestRetry"));
        return false;
    }

    TScriptInterface<IN2CLLMService> RetryService = CreateTransientService(RetryConfig);
    UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(RetryService.GetObject());
    if (!RetryService.GetInterface() || !BaseService)
    {
        ReleaseTransientService(RetryService.GetObject());
        return false;
    }

    const FString FormattedRequest = BaseService->BuildFormattedRequestPayloadFromPreparedMessages(
        PreparedUserMessage,
        PreparedSystemMessage);
    if (FormattedRequest.IsEmpty())
    {
        ReleaseTransientService(RetryService.GetObject());
        return false;
    }

    RetryInFlightRequestIds.Add(RequestId);
    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Replacing request #%d using %s / %s"),
            RequestId,
            *UEnum::GetValueAsString(RetryConfig.Provider),
            *RetryConfig.Model),
        EN2CLogSeverity::Info,
        TEXT("RawRequestRetry"));

    TWeakObjectPtr<UN2CBaseLLMService> WeakRetryService(BaseService);
    BaseService->ResendFormattedRequest(
        FormattedRequest,
        FOnLLMResponseReceived::CreateLambda(
            [this,
             OriginalRecord,
             RequestId,
             RetryConfig,
             PreparedUserMessage,
             PreparedSystemMessage,
             FormattedRequest,
             WeakRetryService,
             OnComplete = MoveTemp(OnComplete)](const FString& Response) mutable
            {
                bool bParsedSuccessfully = false;
                FN2CTranslationResponse RetriedResponse;
                RetriedResponse.Usage.InputTokens = 0;
                RetriedResponse.Usage.OutputTokens = 0;
                FString RequestLabel = OriginalRecord.RequestLabel;

                UN2CBaseLLMService* RetryServiceObject = WeakRetryService.Get();
                UN2CResponseParserBase* Parser = RetryServiceObject
                    ? RetryServiceObject->GetResponseParser()
                    : nullptr;
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
                    Record.Provider = RetryConfig.Provider;
                    Record.CustomProviderName = RetryConfig.CustomProviderName;
                    Record.Model = RetryConfig.Model;
                    Record.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
                    Record.RawRequest = FormattedRequest;
                    Record.SourceRequestPayload = PreparedUserMessage;
                    Record.SourceSystemPrompt = PreparedSystemMessage;
                    Record.FormattedResponse = FormatRawResponseForDisplay(Response);
                    Record.bParsedSuccessfully = bParsedSuccessfully;
                    Record.RetriedFromRequestId = 0;
                    Record.bFinalConsolidation = false;
                }

                PersistRequestHistory();
                OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, bParsedSuccessfully);

                RetryInFlightRequestIds.Remove(RequestId);
                ReleaseTransientService(RetryServiceObject);
                FinishRequest(bParsedSuccessfully);

                if (OnComplete)
                {
                    OnComplete(bParsedSuccessfully);
                }
            }));

    return true;
}

bool UN2CLLMModule::StartFinalConsolidationWithConfig(
    int32 ExistingRequestId,
    const FN2CLLMConfig& RequestConfig,
    TFunction<void(bool)> OnComplete)
{
    auto FailStart = [this, ExistingRequestId, &OnComplete](const FString& Message)
    {
        FN2CLogger::Get().LogError(Message, TEXT("BatchConsolidation"));
        RetryInFlightRequestIds.Remove(ExistingRequestId);
        if (OnComplete)
        {
            OnComplete(false);
        }
        return false;
    };

    TScriptInterface<IN2CLLMService> RetryService = CreateTransientService(RequestConfig);
    UN2CBaseLLMService* BaseService = Cast<UN2CBaseLLMService>(RetryService.GetObject());
    if (!RetryService.GetInterface() || !BaseService)
    {
        return FailStart(TEXT("Cannot initialize selected provider for final Blueprint consolidation"));
    }

    const FN2CTranslationResponse GraphAggregate = BuildGraphAggregateResponse();
    if (GraphAggregate.Graphs.IsEmpty())
    {
        ReleaseTransientService(RetryService.GetObject());
        return FailStart(TEXT("Cannot run final Blueprint consolidation because there are no successful graph translations"));
    }

    const FString BatchRootPath = !CurrentBatchRootPath.IsEmpty()
        ? CurrentBatchRootPath
        : LatestTranslationPath;
    if (BatchRootPath.IsEmpty())
    {
        ReleaseTransientService(RetryService.GetObject());
        return FailStart(TEXT("Cannot run final Blueprint consolidation because the translation batch path is unavailable"));
    }

    const FN2CBlueprint& Blueprint = FN2CNodeTranslator::Get().GetN2CBlueprint();
    FString ConsolidationPayload;
    if (!FN2CBatchTranslationConsolidator::BuildRequestPayload(
            GraphAggregate,
            Blueprint,
            ConsolidationPayload))
    {
        ReleaseTransientService(RetryService.GetObject());
        return FailStart(TEXT("Failed to build final Blueprint consolidation request"));
    }

    const FString ConsolidationPrompt = FN2CBatchTranslationConsolidator::GetSystemPrompt();
    const FString FormattedRequest = BaseService->BuildFormattedRequestPayload(
        ConsolidationPayload,
        ConsolidationPrompt);
    if (FormattedRequest.IsEmpty())
    {
        ReleaseTransientService(RetryService.GetObject());
        return FailStart(TEXT("Failed to format final Blueprint consolidation request"));
    }

    const int32 PriorInputTokens = GraphAggregate.Usage.InputTokens;
    const int32 PriorOutputTokens = GraphAggregate.Usage.OutputTokens;
    SessionTranslationResponse = GraphAggregate;

    ++InFlightRequestCount;
    CurrentStatus = EN2CSystemStatus::Processing;
    OnTranslationRequestSent.Broadcast();

    FN2CLogger::Get().Log(
        FString::Printf(
            TEXT("Starting replacement final Blueprint consolidation with %d current parsed graph translation(s) using %s / %s"),
            GraphAggregate.Graphs.Num(),
            *UEnum::GetValueAsString(RequestConfig.Provider),
            *RequestConfig.Model),
        EN2CLogSeverity::Info,
        TEXT("BatchConsolidation"));

    TWeakObjectPtr<UN2CBaseLLMService> WeakRetryService(BaseService);
    BaseService->ResendFormattedRequest(
        FormattedRequest,
        FOnLLMResponseReceived::CreateLambda(
            [this,
             BatchRootPath,
             PriorInputTokens,
             PriorOutputTokens,
             ExistingRequestId,
             RequestConfig,
             ConsolidationPayload,
             ConsolidationPrompt,
             FormattedRequest,
             WeakRetryService,
             OnComplete = MoveTemp(OnComplete)](const FString& Response) mutable
            {
                bool bSuccess = false;
                FN2CTranslationResponse FinalResponse;
                FinalResponse.Usage.InputTokens = 0;
                FinalResponse.Usage.OutputTokens = 0;
                FString ValidationError;

                UN2CBaseLLMService* RetryServiceObject = WeakRetryService.Get();
                UN2CResponseParserBase* Parser = RetryServiceObject
                    ? RetryServiceObject->GetResponseParser()
                    : nullptr;

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
                Record.RequestId = ExistingRequestId;
                Record.RequestLabel = TEXT("Final Consolidation");
                Record.Provider = RequestConfig.Provider;
                Record.CustomProviderName = RequestConfig.CustomProviderName;
                Record.Model = RequestConfig.Model;
                Record.Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"));
                Record.RawRequest = FormattedRequest;
                Record.SourceRequestPayload = ConsolidationPayload;
                Record.SourceSystemPrompt = ConsolidationPrompt;
                Record.FormattedResponse = FormatRawResponseForDisplay(Response);
                Record.bParsedSuccessfully = bSuccess;
                Record.RetriedFromRequestId = 0;
                Record.bFinalConsolidation = true;

                const int32 RecordIndex = SessionRawResponses.IndexOfByPredicate(
                    [ExistingRequestId](const FN2CRawResponseRecord& Existing)
                    {
                        return Existing.RequestId == ExistingRequestId;
                    });
                if (RecordIndex == INDEX_NONE)
                {
                    SessionRawResponses.Add(MoveTemp(Record));
                }
                else
                {
                    SessionRawResponses[RecordIndex] = MoveTemp(Record);
                }

                PersistRequestHistory();

                if (bSuccess)
                {
                    FN2CLogger::Get().Log(
                        TEXT("Replacement final Blueprint consolidation completed successfully"),
                        EN2CLogSeverity::Info,
                        TEXT("BatchConsolidation"));
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, true);
                }
                else
                {
                    SessionTranslationResponse = BuildGraphAggregateResponse();
                    FN2CLogger::Get().LogError(
                        FString::Printf(TEXT("Final Blueprint consolidation failed: %s"), *ValidationError),
                        TEXT("BatchConsolidation"));
                    SaveRawResponseToDisk(Response);
                    OnTranslationResponseReceived.Broadcast(SessionTranslationResponse, false);
                }

                RetryInFlightRequestIds.Remove(ExistingRequestId);
                ReleaseTransientService(RetryServiceObject);
                FinishRequest(bSuccess);

                if (OnComplete)
                {
                    OnComplete(bSuccess);
                }
            }));

    return true;
}
