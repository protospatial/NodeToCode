// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2COllamaResponseParser.generated.h"

/**
 * @class UN2COllamaResponseParser
 * @brief Parser for Ollama's local LLM API responses
 */
UCLASS()
class NODETOCODE_API UN2COllamaResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse Ollama-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    /** Extract message content from Ollama's unique response format */
    bool ExtractOllamaMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);
};
