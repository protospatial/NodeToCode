// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2CMiniMaxService.generated.h"

/**
 * @class UN2CMiniMaxService
 * @brief Implementation of MiniMax's OpenAI-compatible API integration
 *
 * MiniMax provides an OpenAI-compatible REST API. This service supports
 * custom endpoints and structured output via JSON schema for reliable parsing.
 */
UCLASS()
class NODETOCODE_API UN2CMiniMaxService : public UN2CBaseLLMService
{
    GENERATED_BODY()

public:
    // Override Initialize to handle MiniMax-specific setup
    virtual bool Initialize(const FN2CLLMConfig& InConfig) override;

    // Provider-specific implementations
    virtual void GetConfiguration(FString& OutEndpoint, FString& OutAuthToken, bool& OutSupportsSystemPrompts) override;
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::MiniMax; }
    virtual void GetProviderHeaders(TMap<FString, FString>& OutHeaders) const override;

protected:
    // Provider-specific implementations
    virtual FString FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const override;
    virtual UN2CResponseParserBase* CreateResponseParser() override;
    virtual FString GetDefaultEndpoint() const override { return TEXT("https://api.minimax.io"); }

private:
    /** MiniMax endpoint from settings */
    FString MiniMaxEndpoint;
};