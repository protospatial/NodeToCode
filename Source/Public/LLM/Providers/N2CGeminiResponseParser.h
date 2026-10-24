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
    /**
     * promptFeedback.blockReason gives Blocked; the first candidate's finishReason MAX_TOKENS (or CONTINUATION) gives
     * Truncated, SAFETY, RECITATION, BLOCKLIST, PROHIBITED_CONTENT, SPII and the other filter reasons give Blocked.
     * Returns true when it recorded an error.
     */
    bool CheckBlockAndFinishReason(const TSharedPtr<FJsonObject>& JsonObject, FN2CTranslationResponse& OutResponse);

    /** Joins the non-thought text parts of the first candidate; false when there are none */
    bool ExtractGeminiMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);
};
