// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2COpenAIResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse OpenAI response JSON: %s"), *InJson),
            TEXT("OpenAIResponseParser")
        );
        SetParseError(OutResponse, TEXT("The provider response is not valid JSON"));
        return false;
    }

    // Check for a provider error (the HTTP handler adds the HTTP status to it)
    if (HandleCommonErrorResponse(JsonObject, TEXT("error"), OutResponse))
    {
        return false;
    }

    // Extract message content - handle both Chat Completions and Responses API formats
    FString MessageContent;

    // Check if this is a Responses API response (has "output" array instead of "choices")
    if (JsonObject->HasField(TEXT("output")))
    {
        // Responses API format
        if (!ExtractResponsesAPIContent(JsonObject, MessageContent))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to extract message content from OpenAI Responses API response"), TEXT("OpenAIResponseParser"));
            SetParseError(OutResponse, TEXT("The provider response has no message content"));
            return false;
        }
    }
    else
    {
        // Chat Completions API format
        if (!ExtractStandardMessageContent(JsonObject, TEXT("choices"), TEXT("message"), TEXT("content"), MessageContent))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to extract message content from OpenAI response"), TEXT("OpenAIResponseParser"));
            SetParseError(OutResponse, TEXT("The provider response has no message content"));
            return false;
        }
    }

    // Extract usage information if available
    const TSharedPtr<FJsonObject> UsageObject = JsonObject->GetObjectField(TEXT("usage"));
    if (UsageObject.IsValid())
    {
        int32 PromptTokens = 0;
        int32 CompletionTokens = 0;
        UsageObject->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
        UsageObject->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);

        // Responses API may use different field names
        if (PromptTokens == 0)
        {
            UsageObject->TryGetNumberField(TEXT("input_tokens"), PromptTokens);
        }
        if (CompletionTokens == 0)
        {
            UsageObject->TryGetNumberField(TEXT("output_tokens"), CompletionTokens);
        }

        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CompletionTokens;

        FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens), EN2CLogSeverity::Info);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response Message Content: %s"), *MessageContent), EN2CLogSeverity::Debug);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2COpenAIResponseParser::ExtractResponsesAPIContent(const TSharedPtr<FJsonObject>& JsonObject, FString& OutContent) const
{
    // Responses API format:
    // {
    //   "output": [
    //     {
    //       "type": "message",
    //       "role": "assistant",
    //       "content": [
    //         { "type": "output_text", "text": "..." }
    //       ]
    //     }
    //   ]
    // }

    const TArray<TSharedPtr<FJsonValue>>* OutputArray;
    if (!JsonObject->TryGetArrayField(TEXT("output"), OutputArray) || OutputArray->Num() == 0)
    {
        return false;
    }

    // Find the message output item
    for (const TSharedPtr<FJsonValue>& OutputValue : *OutputArray)
    {
        const TSharedPtr<FJsonObject>* OutputItem;
        if (!OutputValue->TryGetObject(OutputItem) || !OutputItem->IsValid())
        {
            continue;
        }

        FString ItemType;
        if (!(*OutputItem)->TryGetStringField(TEXT("type"), ItemType) || ItemType != TEXT("message"))
        {
            continue;
        }

        // Get content array
        const TArray<TSharedPtr<FJsonValue>>* ContentArray;
        if (!(*OutputItem)->TryGetArrayField(TEXT("content"), ContentArray))
        {
            continue;
        }

        // Find text content
        for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
        {
            const TSharedPtr<FJsonObject>* ContentItem;
            if (!ContentValue->TryGetObject(ContentItem) || !ContentItem->IsValid())
            {
                continue;
            }

            FString ContentType;
            (*ContentItem)->TryGetStringField(TEXT("type"), ContentType);

            // Handle both "output_text" and "text" types
            if (ContentType == TEXT("output_text") || ContentType == TEXT("text"))
            {
                if ((*ContentItem)->TryGetStringField(TEXT("text"), OutContent))
                {
                    return true;
                }
            }
        }
    }

    return false;
}
