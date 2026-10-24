// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CTranslationHistory.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CTranslationHistoryPrivate
{
constexpr int32 HistoryVersion = 2;
const TCHAR* HistoryFileName = TEXT("N2C_RequestHistory.json");

TSharedPtr<FJsonObject> RecordToJson(const FN2CRawResponseRecord& Record)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("request_id"), Record.RequestId);
    Object->SetStringField(TEXT("request_label"), Record.RequestLabel);
    Object->SetNumberField(TEXT("provider_value"), static_cast<uint8>(Record.Provider));
    Object->SetStringField(TEXT("provider"), UEnum::GetValueAsString(Record.Provider));
    Object->SetStringField(TEXT("custom_provider_name"), Record.CustomProviderName);
    Object->SetStringField(TEXT("model"), Record.Model);
    Object->SetStringField(TEXT("timestamp"), Record.Timestamp);
    Object->SetStringField(TEXT("raw_request"), Record.RawRequest);
    Object->SetStringField(TEXT("source_request_payload"), Record.SourceRequestPayload);
    Object->SetStringField(TEXT("source_system_prompt"), Record.SourceSystemPrompt);
    Object->SetStringField(TEXT("formatted_response"), Record.FormattedResponse);
    Object->SetBoolField(TEXT("parsed_successfully"), Record.bParsedSuccessfully);
    Object->SetNumberField(TEXT("retried_from_request_id"), Record.RetriedFromRequestId);
    Object->SetBoolField(TEXT("final_consolidation"), Record.bFinalConsolidation);
    return Object;
}

bool JsonToRecord(const TSharedPtr<FJsonObject>& Object, FN2CRawResponseRecord& OutRecord)
{
    if (!Object.IsValid())
    {
        return false;
    }

    double NumberValue = 0.0;
    Object->TryGetNumberField(TEXT("request_id"), NumberValue);
    OutRecord.RequestId = static_cast<int32>(NumberValue);
    Object->TryGetStringField(TEXT("request_label"), OutRecord.RequestLabel);
    Object->TryGetStringField(TEXT("custom_provider_name"), OutRecord.CustomProviderName);
    Object->TryGetStringField(TEXT("model"), OutRecord.Model);
    Object->TryGetStringField(TEXT("timestamp"), OutRecord.Timestamp);
    Object->TryGetStringField(TEXT("raw_request"), OutRecord.RawRequest);
    Object->TryGetStringField(TEXT("source_request_payload"), OutRecord.SourceRequestPayload);
    Object->TryGetStringField(TEXT("source_system_prompt"), OutRecord.SourceSystemPrompt);
    Object->TryGetStringField(TEXT("formatted_response"), OutRecord.FormattedResponse);
    Object->TryGetBoolField(TEXT("parsed_successfully"), OutRecord.bParsedSuccessfully);
    Object->TryGetBoolField(TEXT("final_consolidation"), OutRecord.bFinalConsolidation);

    NumberValue = 0.0;
    if (Object->TryGetNumberField(TEXT("retried_from_request_id"), NumberValue))
    {
        OutRecord.RetriedFromRequestId = static_cast<int32>(NumberValue);
    }

    NumberValue = static_cast<double>(static_cast<uint8>(EN2CLLMProvider::Anthropic));
    if (Object->TryGetNumberField(TEXT("provider_value"), NumberValue))
    {
        const int32 ProviderValue = FMath::Clamp(
            static_cast<int32>(NumberValue),
            static_cast<int32>(EN2CLLMProvider::OpenAI),
            static_cast<int32>(EN2CLLMProvider::Custom));
        OutRecord.Provider = static_cast<EN2CLLMProvider>(ProviderValue);
    }

    return OutRecord.RequestId > 0;
}
}

FString FN2CTranslationHistory::GetHistoryFilePath(const FString& BatchDirectory)
{
    return FPaths::Combine(BatchDirectory, N2CTranslationHistoryPrivate::HistoryFileName);
}

bool FN2CTranslationHistory::SaveRequestHistory(const FString& BatchDirectory, const TArray<FN2CRawResponseRecord>& Records)
{
    if (BatchDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*BatchDirectory))
    {
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> RequestValues;
    RequestValues.Reserve(Records.Num());
    for (const FN2CRawResponseRecord& Record : Records)
    {
        RequestValues.Add(MakeShared<FJsonValueObject>(N2CTranslationHistoryPrivate::RecordToJson(Record)));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), N2CTranslationHistoryPrivate::HistoryVersion);
    Root->SetArrayField(TEXT("requests"), RequestValues);

    FString Json;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
    {
        return false;
    }
    return FFileHelper::SaveStringToFile(Json, *GetHistoryFilePath(BatchDirectory));
}

bool FN2CTranslationHistory::LoadRequestHistory(const FString& BatchDirectory, TArray<FN2CRawResponseRecord>& OutRecords)
{
    OutRecords.Reset();
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *GetHistoryFilePath(BatchDirectory)))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* RequestValues = nullptr;
    if (!Root->TryGetArrayField(TEXT("requests"), RequestValues) || !RequestValues)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *RequestValues)
    {
        FN2CRawResponseRecord Record;
        if (Value.IsValid() && N2CTranslationHistoryPrivate::JsonToRecord(Value->AsObject(), Record))
        {
            OutRecords.Add(MoveTemp(Record));
        }
    }
    OutRecords.Sort([](const FN2CRawResponseRecord& A, const FN2CRawResponseRecord& B) { return A.RequestId < B.RequestId; });
    return true;
}

void FN2CTranslationHistory::EnumerateBatches(const FString& BaseDirectory, TArray<FN2CTranslationBatchInfo>& OutBatches)
{
    OutBatches.Reset();
    if (!IFileManager::Get().DirectoryExists(*BaseDirectory))
    {
        return;
    }

    TArray<FString> DirectoryNames;
    IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(BaseDirectory, TEXT("*")), false, true);
    for (const FString& DirectoryName : DirectoryNames)
    {
        FN2CTranslationBatchInfo Info;
        Info.DirectoryPath = FPaths::Combine(BaseDirectory, DirectoryName);
        Info.DisplayName = DirectoryName;
        Info.Timestamp = IFileManager::Get().GetTimeStamp(*Info.DirectoryPath);
        Info.bHasRequestHistory = IFileManager::Get().FileExists(*GetHistoryFilePath(Info.DirectoryPath));
        OutBatches.Add(MoveTemp(Info));
    }

    OutBatches.Sort([](const FN2CTranslationBatchInfo& A, const FN2CTranslationBatchInfo& B) { return A.Timestamp > B.Timestamp; });
}
