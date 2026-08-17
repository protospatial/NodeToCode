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
        return false;
    }

    // Check for OpenAI error response
    FString ErrorMessage;
    if (JsonObject->HasField(TEXT("error")))
    {
        if (HandleCommonErrorResponse(JsonObject, TEXT("error"), ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("OpenAIResponseParser"));
        }
        return false;
    }

    // Extract message content from OpenAI format
    FString MessageContent;
    if (!ExtractStandardMessageContent(JsonObject, TEXT("choices"), TEXT("message"), TEXT("content"), MessageContent))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from OpenAI response"), TEXT("OpenAIResponseParser"));
        return false;
    }

    // Extract usage information if available
    const TSharedPtr<FJsonObject> UsageObject = JsonObject->GetObjectField(TEXT("usage"));
    if (UsageObject.IsValid())
    {
        int32 PromptTokens = 0;
        int32 CompletionTokens = 0;
        UsageObject->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
        UsageObject->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
        
        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CompletionTokens;

        FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens), EN2CLogSeverity::Info);
    }

    // Reasoning-capable OpenAI-compatible servers can consume the entire completion budget in a
    // separate reasoning_content field and then return content="" with finish_reason="length".
    // That is not recoverable translation JSON, so report the actual failure instead of forwarding
    // an empty string to the generic parser and producing the misleading "too short" error.
    FString FinishReason;
    int32 ReasoningContentLength = 0;
    const TArray<TSharedPtr<FJsonValue>>& Choices = JsonObject->GetArrayField(TEXT("choices"));
    if (!Choices.IsEmpty())
    {
        const TSharedPtr<FJsonObject> ChoiceObject = Choices[0]->AsObject();
        if (ChoiceObject.IsValid())
        {
            ChoiceObject->TryGetStringField(TEXT("finish_reason"), FinishReason);

            const TSharedPtr<FJsonObject> MessageObject = ChoiceObject->GetObjectField(TEXT("message"));
            if (MessageObject.IsValid())
            {
                FString ReasoningContent;
                if (MessageObject->TryGetStringField(TEXT("reasoning_content"), ReasoningContent))
                {
                    ReasoningContentLength = ReasoningContent.Len();
                }
            }
        }
    }

    if (MessageContent.TrimStartAndEnd().IsEmpty())
    {
        if (FinishReason.Equals(TEXT("length"), ESearchCase::IgnoreCase))
        {
            FN2CLogger::Get().LogError(
                FString::Printf(
                    TEXT("LLM exhausted its completion token budget before producing final content (reasoning_content: %d chars). Increase the provider Max Output Tokens setting."),
                    ReasoningContentLength),
                TEXT("OpenAIResponseParser"));
            return false;
        }

        if (ReasoningContentLength > 0)
        {
            FN2CLogger::Get().LogError(
                FString::Printf(
                    TEXT("LLM returned reasoning content (%d chars) but no final response content"),
                    ReasoningContentLength),
                TEXT("OpenAIResponseParser"));
            return false;
        }
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response Message Content: %s"), *MessageContent), EN2CLogSeverity::Debug);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}
