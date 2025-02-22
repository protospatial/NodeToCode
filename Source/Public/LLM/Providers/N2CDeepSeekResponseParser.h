// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CDeepSeekResponseParser.generated.h"

/**
 * @class UN2CDeepSeekResponseParser
 * @brief Parser for DeepSeek API responses
 */
UCLASS()
class NODETOCODE_API UN2CDeepSeekResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse DeepSeek-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    /** Extract message content from DeepSeek response format */
    bool ExtractMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);

    /** Handle DeepSeek-specific error responses */
    bool HandleDeepSeekError(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutErrorMessage);
};
