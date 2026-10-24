// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CAnthropicResponseParser.generated.h"

/**
 * @class UN2CAnthropicResponseParser
 * @brief Parser for Anthropic's Claude API responses
 */
UCLASS()
class NODETOCODE_API UN2CAnthropicResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse Anthropic-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    /**
     * Reads stop_reason: max_tokens and model_context_window_exceeded give Truncated, refusal gives Refused with
     * stop_details' category and explanation. Returns true when it recorded an error.
     */
    bool CheckStopReason(const TSharedPtr<FJsonObject>& JsonObject, FN2CTranslationResponse& OutResponse);

    /** Joins the text blocks of Anthropic's content array (thinking blocks are skipped); false when there are none */
    bool ExtractAnthropicMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);
};
