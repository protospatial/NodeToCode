// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CResponseParserBase.h"
#include "N2COpenAIResponseParser.generated.h"

/**
 * @class UN2COpenAIResponseParser
 * @brief Parser for OpenAI Chat Completion and Responses API responses
 */
UCLASS()
class NODETOCODE_API UN2COpenAIResponseParser : public UN2CResponseParserBase
{
    GENERATED_BODY()

public:
    /** Parse OpenAI-specific JSON response (handles both Chat Completions and Responses API formats) */
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse) override;

protected:
    // Using base class implementations for error handling and content extraction

    /**
     * Extract content from OpenAI Responses API format. status "incomplete" gives Truncated (max_output_tokens) or
     * Blocked (content_filter) and a refusal content part gives Refused, recorded on OutResponse.Error.
     */
    bool ExtractResponsesAPIContent(const TSharedPtr<FJsonObject>& JsonObject, FString& OutContent,
        FN2CTranslationResponse& OutResponse) const;
};
