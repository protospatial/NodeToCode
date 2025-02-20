// Copyright ProtoSpatial. All Rights Reserved.

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
        if (HandleGeminiError(JsonObject, ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("GeminiResponseParser"));
        }
        return false;
    }

    // Extract message content from Gemini format
    FString MessageContent;
    if (!ExtractMessageContent(JsonObject, MessageContent))
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

bool UN2CGeminiResponseParser::ExtractMessageContent(
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

    // Check if content is wrapped in ```json markers and remove them
    if (RawContent.StartsWith(TEXT("```json")) && RawContent.EndsWith(TEXT("```")))
    {
        // Remove the ```json prefix and ``` suffix
        OutContent = RawContent.RightChop(7); // Skip past "```json"
        OutContent = OutContent.LeftChop(3);  // Remove trailing "```"
        OutContent = OutContent.TrimStartAndEnd(); // Remove any extra whitespace
        
        FN2CLogger::Get().Log(TEXT("Stripped JSON markers from Gemini response"), EN2CLogSeverity::Debug);
    }
    else
    {
        OutContent = RawContent;
    }

    return true;
}

bool UN2CGeminiResponseParser::HandleGeminiError(
    const TSharedPtr<FJsonObject>& JsonObject,
    FString& OutErrorMessage)
{
    const TSharedPtr<FJsonObject> ErrorObject = JsonObject->GetObjectField(TEXT("error"));
    if (!ErrorObject.IsValid())
    {
        OutErrorMessage = TEXT("Unknown Gemini error");
        return true;
    }

    FString ErrorType, ErrorMessage;
    ErrorObject->TryGetStringField(TEXT("type"), ErrorType);
    ErrorObject->TryGetStringField(TEXT("message"), ErrorMessage);

    if (ErrorType.Contains(TEXT("rate_limit")))
    {
        OutErrorMessage = TEXT("Gemini API rate limit exceeded");
    }
    else if (ErrorType.Contains(TEXT("invalid_request_error")))
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid request: %s"), *ErrorMessage);
    }
    else if (ErrorType.Contains(TEXT("authentication")))
    {
        OutErrorMessage = TEXT("Gemini API authentication failed");
    }
    else
    {
        OutErrorMessage = FString::Printf(TEXT("Gemini API error: %s - %s"), *ErrorType, *ErrorMessage);
    }

    return true;
}
