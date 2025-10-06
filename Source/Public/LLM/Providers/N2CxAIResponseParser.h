// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CxAIResponseParser.generated.h"

/**
 * @class UN2CxAIResponseParser
 * @brief Parser for xAI Chat Completion API responses
 */
UCLASS()
class NODETOCODE_API UN2CxAIResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse xAI-specific JSON response (OpenAI-compatible schema) */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;
};
