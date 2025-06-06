// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CLMStudioResponseParser.generated.h"

/**
 * @class UN2CLMStudioResponseParser
 * @brief Parser for LM Studio Chat Completion API responses
 * 
 * LM Studio uses OpenAI-compatible response format with additional fields
 * like stats (tokens_per_second, time_to_first_token) and model_info.
 */
UCLASS()
class NODETOCODE_API UN2CLMStudioResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse LM Studio-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    // Using base class implementations for error handling and content extraction
};