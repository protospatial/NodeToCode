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
        if (HandleAnthropicError(JsonObject, ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("AnthropicResponseParser"));
        }
        return false;
    }

    // Extract message content from Anthropic format
    FString MessageContent;
    if (!ExtractMessageContent(JsonObject, MessageContent))
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

bool UN2CAnthropicResponseParser::ExtractMessageContent(
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

        // Check if content is wrapped in ```json markers and remove them
        if (RawContent.StartsWith(TEXT("```json")) && RawContent.EndsWith(TEXT("```")))
        {
            // Remove the ```json prefix and ``` suffix
            OutContent = RawContent.RightChop(7); // Skip past "```json"
            OutContent = OutContent.LeftChop(3);  // Remove trailing "```"
            OutContent = OutContent.TrimStartAndEnd(); // Remove any extra whitespace
        
            FN2CLogger::Get().Log(TEXT("Stripped JSON markers from Anthropic response"), EN2CLogSeverity::Debug);
        }
        else
        {
            OutContent = RawContent;
        }
        
        return true;
    }

    return false;
}

bool UN2CAnthropicResponseParser::HandleAnthropicError(
    const TSharedPtr<FJsonObject>& JsonObject,
    FString& OutErrorMessage)
{
    const TSharedPtr<FJsonObject> ErrorObject = JsonObject->GetObjectField(TEXT("error"));
    if (!ErrorObject.IsValid())
    {
        OutErrorMessage = TEXT("Unknown Anthropic error");
        return true;
    }

    FString ErrorType, ErrorMessage;
    ErrorObject->TryGetStringField(TEXT("type"), ErrorType);
    ErrorObject->TryGetStringField(TEXT("message"), ErrorMessage);

    if (ErrorType.Contains(TEXT("rate_limit")))
    {
        OutErrorMessage = TEXT("Anthropic API rate limit exceeded");
    }
    else if (ErrorType.Contains(TEXT("invalid_request")))
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid request: %s"), *ErrorMessage);
    }
    else if (ErrorType.Contains(TEXT("authentication")))
    {
        OutErrorMessage = TEXT("Anthropic API authentication failed");
    }
    else
    {
        OutErrorMessage = FString::Printf(TEXT("Anthropic API error: %s - %s"), *ErrorType, *ErrorMessage);
    }

    return true;
}
