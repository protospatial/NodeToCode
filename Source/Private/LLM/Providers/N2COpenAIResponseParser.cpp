// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

namespace N2COpenAIParserPrivate
{
    const TCHAR* SettingHint = TEXT("Project Settings > Node to Code > LLM Services > OpenAI > Max Output Tokens (0 = Auto)");
}

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
            FString::Printf(TEXT("Failed to parse OpenAI response JSON (%d characters)"), InJson.Len()),
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

    // Extract usage information if available (also on a truncated answer, which is billed)
    const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr && UsagePtr->IsValid())
    {
        const TSharedPtr<FJsonObject>& UsageObject = *UsagePtr;
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

    // Extract message content - handle both Chat Completions and Responses API formats
    FString MessageContent;

    // Check if this is a Responses API response (has "output" array instead of "choices")
    if (JsonObject->HasField(TEXT("output")))
    {
        // Responses API format: status and incomplete_details first, then a refusal, then the text
        if (!ExtractResponsesAPIContent(JsonObject, MessageContent, OutResponse))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to extract message content from OpenAI Responses API response"), TEXT("OpenAIResponseParser"));
            SetParseError(OutResponse, TEXT("The provider response has no message content"));
            return false;
        }
    }
    else
    {
        // Chat Completions API format: finish_reason and refusal first, then the content
        if (!ExtractChatCompletionContent(JsonObject, N2COpenAIParserPrivate::SettingHint, MessageContent, OutResponse))
        {
            FN2CLogger::Get().LogError(TEXT("Failed to extract message content from OpenAI response"), TEXT("OpenAIResponseParser"));
            return false;
        }
    }

    FN2CLogger::Get().LogLLMContent(TEXT("LLM Response Message Content"), TEXT("LLM response message content"), MessageContent);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2COpenAIResponseParser::ExtractResponsesAPIContent(const TSharedPtr<FJsonObject>& JsonObject, FString& OutContent,
    FN2CTranslationResponse& OutResponse) const
{
    // Responses API format:
    // {
    //   "status": "completed" | "incomplete" | ...,
    //   "incomplete_details": null | { "reason": "max_output_tokens" | "content_filter" },
    //   "output": [
    //     { "type": "reasoning", ... },
    //     {
    //       "type": "message",
    //       "role": "assistant",
    //       "content": [
    //         { "type": "output_text", "text": "..." } | { "type": "refusal", "refusal": "..." }
    //       ]
    //     }
    //   ]
    // }

    FString Status;
    JsonObject->TryGetStringField(TEXT("status"), Status);
    if (Status == TEXT("incomplete"))
    {
        FString Reason;
        const TSharedPtr<FJsonObject>* Details = nullptr;
        if (JsonObject->TryGetObjectField(TEXT("incomplete_details"), Details) && Details && Details->IsValid())
        {
            (*Details)->TryGetStringField(TEXT("reason"), Reason);
        }
        if (Reason == TEXT("content_filter"))
        {
            SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Blocked, Reason,
                TEXT("The provider's content filter stopped the answer (status \"incomplete\", reason \"content_filter\")"));
        }
        else
        {
            // max_output_tokens, or a reason this parser does not know: the answer is incomplete either way
            SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Truncated, Reason.IsEmpty() ? Status : Reason,
                MakeTruncatedMessage(FString::Printf(TEXT("status \"incomplete\", reason \"%s\""), Reason.IsEmpty() ? TEXT("none") : *Reason),
                    N2COpenAIParserPrivate::SettingHint));
        }
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* OutputArray;
    if (!JsonObject->TryGetArrayField(TEXT("output"), OutputArray) || OutputArray->Num() == 0)
    {
        return false;
    }

    // Find the message output item; join its text parts
    FString Joined;
    bool bFoundText = false;
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

        for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
        {
            const TSharedPtr<FJsonObject>* ContentItem;
            if (!ContentValue->TryGetObject(ContentItem) || !ContentItem->IsValid())
            {
                continue;
            }

            FString ContentType;
            (*ContentItem)->TryGetStringField(TEXT("type"), ContentType);

            if (ContentType == TEXT("refusal"))
            {
                FString Refusal;
                (*ContentItem)->TryGetStringField(TEXT("refusal"), Refusal);
                SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Refused, TEXT("refusal"),
                    FString::Printf(TEXT("The model refused the request: %s"), *Refusal.TrimStartAndEnd()));
                return false;
            }

            // Handle both "output_text" and "text" types
            FString Text;
            if ((ContentType == TEXT("output_text") || ContentType == TEXT("text"))
                && (*ContentItem)->TryGetStringField(TEXT("text"), Text))
            {
                Joined += Text;
                bFoundText = true;
            }
        }
    }

    if (!bFoundText)
    {
        return false;
    }
    OutContent = Joined;
    return true;
}
