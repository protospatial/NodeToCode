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
    /** Extract message content from Anthropic's unique response format */
    bool ExtractAnthropicMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        FString& OutContent);
};
