// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"

struct FN2CTranslationBatchInfo
{
    FString DirectoryPath;
    FString DisplayName;
    FDateTime Timestamp;
    bool bHasRequestHistory = false;
};

class FN2CTranslationHistory
{
public:
    static FString GetHistoryFilePath(const FString& BatchDirectory);
    static bool SaveRequestHistory(const FString& BatchDirectory, const TArray<FN2CRawResponseRecord>& Records);
    static bool LoadRequestHistory(const FString& BatchDirectory, TArray<FN2CRawResponseRecord>& OutRecords);
    static void EnumerateBatches(const FString& BaseDirectory, TArray<FN2CTranslationBatchInfo>& OutBatches);
};
