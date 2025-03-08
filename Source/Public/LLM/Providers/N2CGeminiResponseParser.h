// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CGeminiResponseParser.generated.h"

/**
 * @class UN2CGeminiResponseParser
 * @brief Parser for Gemini Chat Completion API responses
 */
UCLASS()
class NODETOCODE_API UN2CGeminiResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse Gemini-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    /** Extract message content from Gemini's unique response format */
    bool ExtractGeminiMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);
};
