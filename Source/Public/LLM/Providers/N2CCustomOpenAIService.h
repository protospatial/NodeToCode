// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2CCustomOpenAIService.generated.h"

UCLASS()
class NODETOCODE_API UN2CCustomOpenAIService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    virtual bool Initialize(const FN2CLLMConfig& InConfig) override;
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::Custom; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return FString(); }

private:
    int32 MaxOutputTokens = 32768;
};
