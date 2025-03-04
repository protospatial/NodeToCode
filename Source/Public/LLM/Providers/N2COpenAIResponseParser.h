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
    // Using base class implementations for error handling and content extraction
};
