// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2COpenAIResponseParser.generated.h"

/**
 * @class UN2COpenAIResponseParser
 * @brief Parser for OpenAI Chat Completion API responses
 */
UCLASS()
class NODETOCODE_API UN2COpenAIResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse OpenAI-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    /** Extract message content from OpenAI response format */
    bool ExtractMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);

    /** Handle OpenAI-specific error responses */
    bool HandleOpenAIError(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutErrorMessage);
};
