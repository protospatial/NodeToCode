// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CAnthropicResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

namespace N2CAnthropicParserPrivate
{
    const TCHAR* SettingHint = TEXT("Project Settings > Node to Code > LLM Services > Anthropic > Max Output Tokens (0 = Auto)");
}

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
        SetParseError(OutResponse, TEXT("The provider response is not valid JSON"));
        return false;
    }

    // Check for a provider error (the HTTP handler adds the HTTP status to it)
    if (HandleCommonErrorResponse(JsonObject, TEXT("error"), OutResponse))
    {
        return false;
    }

    // Extract usage information if available (also on a truncated or refused answer, which is billed)
    const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr && UsagePtr->IsValid())
    {
        // input_tokens counts only the input after the last cache breakpoint; the cached part is billed separately
        // as cache_read_input_tokens and cache_creation_input_tokens (docs: build-with-claude/prompt-caching)
        int32 InputTokens = 0;
        int32 OutputTokens = 0;
        int32 CacheRead = 0;
        int32 CacheWrite = 0;
        (*UsagePtr)->TryGetNumberField(TEXT("input_tokens"), InputTokens);
        (*UsagePtr)->TryGetNumberField(TEXT("output_tokens"), OutputTokens);
        (*UsagePtr)->TryGetNumberField(TEXT("cache_read_input_tokens"), CacheRead);
        (*UsagePtr)->TryGetNumberField(TEXT("cache_creation_input_tokens"), CacheWrite);

        OutResponse.Usage.InputTokens = InputTokens + CacheRead + CacheWrite;
        OutResponse.Usage.OutputTokens = OutputTokens;
        OutResponse.Usage.CacheReadInputTokens = CacheRead;
        OutResponse.Usage.CacheWriteInputTokens = CacheWrite;
        LogUsage(OutResponse.Usage, TEXT("AnthropicResponseParser"));
    }

    // The stop reason decides before the content does (docs: build-with-claude/handling-stop-reasons)
    if (CheckStopReason(JsonObject, OutResponse))
    {
        return false;
    }

    // Extract message content from Anthropic format - this is a special case
    // because Anthropic's format is different from other providers
    FString MessageContent;
    if (!ExtractAnthropicMessageContent(JsonObject, MessageContent))
    {
        // Current models think by default and return thinking blocks (empty text unless display is "summarized")
        // before the answer, so a response can hold thinking blocks and no text block
        FString StopReason;
        JsonObject->TryGetStringField(TEXT("stop_reason"), StopReason);
        TArray<FString> BlockTypes;
        const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
        if (JsonObject->TryGetArrayField(TEXT("content"), ContentArray))
        {
            for (const TSharedPtr<FJsonValue>& Block : *ContentArray)
            {
                const TSharedPtr<FJsonObject>* BlockObject = nullptr;
                FString Type;
                if (Block.IsValid() && Block->TryGetObject(BlockObject) && BlockObject && (*BlockObject)->TryGetStringField(TEXT("type"), Type))
                {
                    BlockTypes.Add(Type);
                }
            }
        }
        SetParseError(OutResponse, FString::Printf(TEXT("The provider response has no text answer (stop_reason \"%s\", content blocks: %s)"),
            StopReason.IsEmpty() ? TEXT("none") : *StopReason,
            BlockTypes.Num() > 0 ? *FString::Join(BlockTypes, TEXT(", ")) : TEXT("none")));
        FN2CLogger::Get().LogError(OutResponse.Error.Message, TEXT("AnthropicResponseParser"));
        return false;
    }

    FN2CLogger::Get().LogLLMContent(TEXT("LLM Response Message Content"), TEXT("LLM response message content"), MessageContent);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2CAnthropicResponseParser::CheckStopReason(const TSharedPtr<FJsonObject>& JsonObject, FN2CTranslationResponse& OutResponse)
{
    FString StopReason;
    if (!JsonObject->TryGetStringField(TEXT("stop_reason"), StopReason))
    {
        return false;
    }

    if (StopReason == TEXT("max_tokens"))
    {
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Truncated, StopReason,
            MakeTruncatedMessage(TEXT("stop_reason \"max_tokens\""), N2CAnthropicParserPrivate::SettingHint));
        return true;
    }
    if (StopReason == TEXT("model_context_window_exceeded"))
    {
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Truncated, StopReason,
            TEXT("The answer filled the model's context window (stop_reason \"model_context_window_exceeded\") before the ")
            TEXT("translation was complete. Translate fewer nodes (lower the translation depth) or remove reference files."));
        return true;
    }
    if (StopReason == TEXT("refusal"))
    {
        // stop_details: {"type": "refusal", "category": "cyber" | ... | null, "explanation": "..." | null}
        FString Category, Explanation;
        const TSharedPtr<FJsonObject>* Details = nullptr;
        if (JsonObject->TryGetObjectField(TEXT("stop_details"), Details) && Details && Details->IsValid())
        {
            (*Details)->TryGetStringField(TEXT("category"), Category);
            (*Details)->TryGetStringField(TEXT("explanation"), Explanation);
        }
        FString Message = TEXT("The model declined the request (stop_reason \"refusal\"");
        if (!Category.IsEmpty())
        {
            Message += FString::Printf(TEXT(", category \"%s\""), *Category);
        }
        Message += TEXT(")");
        if (!Explanation.IsEmpty())
        {
            Message += TEXT(": ") + Explanation;
        }
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Refused, StopReason, Message);
        return true;
    }
    return false;
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

    // Join every text block; thinking and other blocks are skipped (citations can split the answer into several)
    FString Joined;
    bool bFoundText = false;
    for (const auto& ContentValue : *ContentArray)
    {
        const TSharedPtr<FJsonObject>* ContentObject = nullptr;
        if (!ContentValue.IsValid() || !ContentValue->TryGetObject(ContentObject) || !ContentObject || !ContentObject->IsValid())
        {
            continue;
        }

        FString Type;
        if (!(*ContentObject)->TryGetStringField(TEXT("type"), Type) || Type != TEXT("text"))
        {
            continue;
        }

        FString Text;
        if ((*ContentObject)->TryGetStringField(TEXT("text"), Text))
        {
            Joined += Text;
            bFoundText = true;
        }
    }

    if (!bFoundText || Joined.TrimStartAndEnd().IsEmpty())
    {
        return false;
    }

    // Fences and text around the JSON are removed by ParseLLMResponse
    OutContent = Joined.TrimStartAndEnd();
    return true;
}
