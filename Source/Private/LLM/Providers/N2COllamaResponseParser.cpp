// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COllamaResponseParser.h"

#include "Core/N2CSettings.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2COllamaResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Failed to parse Ollama response JSON"), TEXT("OllamaResponseParser"));
        return false;
    }

    // Check for Ollama error response
    FString ErrorMessage;
    if (JsonObject->HasField(TEXT("error")))
    {
        if (HandleOllamaError(JsonObject, ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("OllamaResponseParser"));
        }
        return false;
    }

    // Extract message content from Ollama format
    FString MessageContent;
    if (!ExtractMessageContent(JsonObject, MessageContent))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from Ollama response"), TEXT("OllamaResponseParser"));
        return false;
    }
    
    // Extract usage information
    if (JsonObject.IsValid())
    {
        int32 PromptTokens = 0;
        int32 CompletionTokens = 0;
        JsonObject->TryGetNumberField(TEXT("prompt_eval_count"), PromptTokens);
        JsonObject->TryGetNumberField(TEXT("eval_count"), CompletionTokens);
        
        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CompletionTokens;

        FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens), EN2CLogSeverity::Info);
    }

    if (MessageContent.StartsWith(TEXT("<think>")))
    {
        // Remove thinking tags and their content
        while (true)
        {
            int32 ThinkStart = MessageContent.Find(TEXT("<think>"));
            if (ThinkStart == INDEX_NONE) break;

            int32 ThinkEnd = MessageContent.Find(TEXT("</think>"));
            if (ThinkEnd == INDEX_NONE) break;

            // Remove the thinking section including tags
            MessageContent.RemoveAt(ThinkStart, (ThinkEnd + 8) - ThinkStart);
        }
    }
    
    // Trim any extra whitespace that might have been left
    MessageContent = MessageContent.TrimStartAndEnd();
    
    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response Message Content: %s"), *MessageContent), EN2CLogSeverity::Debug);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2COllamaResponseParser::ExtractMessageContent(
    const TSharedPtr<FJsonObject>& JsonObject,
    FString& OutContent)
{
    // Get message object from response
    const TSharedPtr<FJsonObject> MessageObject = JsonObject->GetObjectField(TEXT("message"));
    if (!MessageObject.IsValid())
    {
        return false;
    }

    // Get content from message
    return MessageObject->TryGetStringField(TEXT("content"), OutContent);
}

bool UN2COllamaResponseParser::HandleOllamaError(
    const TSharedPtr<FJsonObject>& JsonObject,
    FString& OutErrorMessage)
{
    FString ErrorMessage;
    if (!JsonObject->TryGetStringField(TEXT("error"), ErrorMessage))
    {
        OutErrorMessage = TEXT("Unknown Ollama error");
        return true;
    }

    OutErrorMessage = FString::Printf(TEXT("Ollama API error: %s"), *ErrorMessage);
    return true;
}
