// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CMiniMaxResponseParser.generated.h"

/**
 * @class UN2CMiniMaxResponseParser
 * @brief Parser for MiniMax Chat Completion API responses
 *
 * MiniMax uses OpenAI-compatible response format.
 */
UCLASS()
class NODETOCODE_API UN2CMiniMaxResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse MiniMax-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    // Using base class implementations for error handling and content extraction
};