// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CGeminiResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2CGeminiResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse Gemini response JSON: %s"), *InJson),
            TEXT("GeminiResponseParser")
        );
        return false;
    }

    // Check for Gemini error response
    FString ErrorMessage;
    if (JsonObject->HasField(TEXT("error")))
    {
        if (HandleCommonErrorResponse(JsonObject, TEXT("error"), ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("GeminiResponseParser"));
        }
        return false;
    }

    // Extract message content from Gemini format - this is a special case
    // because Gemini's format is different from other providers
    FString MessageContent;
    if (!ExtractGeminiMessageContent(JsonObject, MessageContent))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from Gemini response"), TEXT("GeminiResponseParser"));
        return false;
    }

    // Extract usage information if available
    const TSharedPtr<FJsonObject> UsageMetadata = JsonObject->GetObjectField(TEXT("usageMetadata"));
    if (UsageMetadata.IsValid())
    {
        int32 PromptTokens = 0;
        int32 CompletionTokens = 0;
        UsageMetadata->TryGetNumberField(TEXT("promptTokenCount"), PromptTokens);
        UsageMetadata->TryGetNumberField(TEXT("candidatesTokenCount"), CompletionTokens);
        
        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CompletionTokens;

        FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens), EN2CLogSeverity::Info);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("LLM Response Message Content: %s"), *MessageContent), EN2CLogSeverity::Debug);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2CGeminiResponseParser::ExtractGeminiMessageContent(
    const TSharedPtr<FJsonObject>& JsonObject,
    FString& OutContent)
{
    // Get candidates array
    const TArray<TSharedPtr<FJsonValue>>* CandidatesArray;
    if (!JsonObject->TryGetArrayField(TEXT("candidates"), CandidatesArray) || CandidatesArray->Num() == 0)
    {
        return false;
    }

    // Get first candidate
    const TSharedPtr<FJsonObject> CandidateObject = (*CandidatesArray)[0]->AsObject();
    if (!CandidateObject.IsValid())
    {
        return false;
    }

    // Get content object
    const TSharedPtr<FJsonObject> ContentObject = CandidateObject->GetObjectField(TEXT("content"));
    if (!ContentObject.IsValid())
    {
        return false;
    }

    // Get parts array
    const TArray<TSharedPtr<FJsonValue>>* PartsArray;
    if (!ContentObject->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0)
    {
        return false;
    }

    // Get first part
    const TSharedPtr<FJsonObject> PartObject = (*PartsArray)[0]->AsObject();
    if (!PartObject.IsValid())
    {
        return false;
    }

    // Get text content
    FString RawContent;
    if (!PartObject->TryGetStringField(TEXT("text"), RawContent))
    {
        return false;
    }

    // Process content for JSON markers
    ProcessJsonContentWithMarkers(RawContent);
    OutContent = RawContent;
    return true;
}
