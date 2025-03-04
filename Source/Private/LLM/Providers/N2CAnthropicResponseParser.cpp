// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CAnthropicResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2CAnthropicResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse Anthropic response JSON"), TEXT("AnthropicResponseParser"));
        return false;
    }

    // Check for Anthropic error response
    FString ErrorMessage;
    if (JsonObject->HasField(TEXT("error")))
    {
        if (HandleCommonErrorResponse(JsonObject, TEXT("error"), ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("AnthropicResponseParser"));
        }
        return false;
    }

    // Extract message content from Anthropic format - this is a special case
    // because Anthropic's format is different from other providers
    FString MessageContent;
    if (!ExtractAnthropicMessageContent(JsonObject, MessageContent))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from Anthropic response"), TEXT("AnthropicResponseParser"));
        return false;
    }

    // Extract usage information if available
    const TSharedPtr<FJsonObject> UsageObject = JsonObject->GetObjectField(TEXT("usage"));
    if (UsageObject.IsValid())
    {
        int32 InputTokens = 0;
        int32 OutputTokens = 0;
        UsageObject->TryGetNumberField(TEXT("input_tokens"), InputTokens);
        UsageObject->TryGetNumberField(TEXT("output_tokens"), OutputTokens);
        
        OutResponse.Usage.InputTokens = InputTokens;
        OutResponse.Usage.OutputTokens = OutputTokens;

        FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Token Usage - Input: %d Output: %d"), InputTokens, OutputTokens), EN2CLogSeverity::Info);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response Message Content: %s"), *MessageContent), EN2CLogSeverity::Debug);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2CAnthropicResponseParser::ExtractAnthropicMessageContent(
    const TSharedPtr<FJsonObject>& JsonObject,
    FString& OutContent)
{
    // Get content array from response
    const TArray<TSharedPtr<FJsonValue>>* ContentArray;
    if (!JsonObject->TryGetArrayField(TEXT("content"), ContentArray) || ContentArray->Num() == 0)
    {
        return false;
    }

    // Find the text content block
    for (const auto& ContentValue : *ContentArray)
    {
        const TSharedPtr<FJsonObject> ContentObject = ContentValue->AsObject();
        if (!ContentObject.IsValid())
        {
            continue;
        }

        FString Type;
        if (!ContentObject->TryGetStringField(TEXT("type"), Type) || Type != TEXT("text"))
        {
            continue;
        }

        // Get content string
        FString RawContent;
        if (!ContentObject->TryGetStringField(TEXT("text"), RawContent))
        {
            return false;
        }

        // Process content for JSON markers
        ProcessJsonContentWithMarkers(RawContent);
        OutContent = RawContent;
        return true;
    }

    return false;
}
