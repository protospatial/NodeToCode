// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2CDeepSeekResponseParser.generated.h"

/**
 * @class UN2CDeepSeekResponseParser
 * @brief Parser for DeepSeek API responses
 */
UCLASS()
class NODETOCODE_API UN2CDeepSeekResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse DeepSeek-specific JSON response */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    // Using base class implementations for error handling and content extraction
};
