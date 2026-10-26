// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CDeepSeekResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2CDeepSeekResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse DeepSeek response JSON: %s"), *InJson),
            TEXT("DeepSeekResponseParser")
        );
        SetParseError(OutResponse, TEXT("The provider response is not valid JSON"));
        return false;
    }

    // Check for a provider error (the HTTP handler adds the HTTP status to it)
    if (HandleCommonErrorResponse(JsonObject, TEXT("error"), OutResponse))
    {
        return false;
    }

    // Extract usage information if available (also on a truncated answer, which is billed)
    const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr && UsagePtr->IsValid())
    {
        // prompt_tokens includes prompt_cache_hit_tokens; completion_tokens includes reasoning_tokens
        ReadOpenAICompatibleUsage(*UsagePtr, OutResponse.Usage);
        LogUsage(OutResponse.Usage, TEXT("DeepSeekResponseParser"));
    }

    // Extract message content from DeepSeek format: finish_reason first (length, content_filter,
    // insufficient_system_resource), then the content; reasoning_content is never the answer
    FString MessageContent;
    if (!ExtractChatCompletionContent(JsonObject,
        TEXT("Project Settings > Node to Code > LLM Services > DeepSeek > Max Output Tokens (0 = Auto)"), MessageContent, OutResponse))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from DeepSeek response"), TEXT("DeepSeekResponseParser"));
        return false;
    }

    FN2CLogger::Get().LogLLMContent(TEXT("LLM Response Message Content"), TEXT("LLM response message content"), MessageContent);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}
