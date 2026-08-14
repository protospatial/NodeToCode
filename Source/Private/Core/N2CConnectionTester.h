// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"

class UN2CCustomProviderSettings;
class UN2CSettings;

class FN2CConnectionTester
{
public:
    static void TestProvider(EN2CLLMProvider Provider, const UN2CSettings& Settings);
    static void TestCustomProvider(const FString& ProviderName, const UN2CCustomProviderSettings& Settings);

private:
    static FString NormalizeBaseUrl(const FString& Endpoint, const FString& KnownSuffix = FString());
    static void SendGetRequest(
        const FString& ProviderName,
        const FString& Url,
        const TMap<FString, FString>& Headers);
    static void ShowResult(const FString& Message, bool bSuccess);
};
