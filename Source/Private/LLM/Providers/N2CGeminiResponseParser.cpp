// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CGeminiResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

namespace N2CGeminiParserPrivate
{
    const TCHAR* SettingHint = TEXT("Project Settings > Node to Code > LLM Services > Gemini > Max Output Tokens (0 = Auto)");

    /** finishReason values (ai.google.dev/api/generate-content#FinishReason) that mean a filter stopped the answer */
    bool IsBlockingFinishReason(const FString& Reason)
    {
        static const TCHAR* Blocking[] = {
            TEXT("SAFETY"), TEXT("RECITATION"), TEXT("LANGUAGE"), TEXT("BLOCKLIST"), TEXT("PROHIBITED_CONTENT"),
            TEXT("SPII"), TEXT("IMAGE_SAFETY"), TEXT("IMAGE_PROHIBITED_CONTENT"), TEXT("IMAGE_RECITATION"),
            TEXT("ESCALATION"), TEXT("PUP_LIMITED_DISABLED") };
        for (const TCHAR* Name : Blocking)
        {
            if (Reason == Name)
            {
                return true;
            }
        }
        return false;
    }
}

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
        SetParseError(OutResponse, TEXT("The provider response is not valid JSON"));
        return false;
    }

    // Check for a provider error (the HTTP handler adds the HTTP status to it)
    if (HandleCommonErrorResponse(JsonObject, TEXT("error"), OutResponse))
    {
        return false;
    }

    const TSharedPtr<FJsonObject>& ResponseObject = JsonObject;

    // Extract usage information if available (also on a truncated answer, which is billed)
    const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
    if (ResponseObject->TryGetObjectField(TEXT("usageMetadata"), UsagePtr) && UsagePtr && UsagePtr->IsValid())
    {
        // Thinking is billed as output but counted apart from the answer in thoughtsTokenCount; promptTokenCount
        // already includes cachedContentTokenCount (ai.google.dev/api/generate-content#UsageMetadata)
        int32 PromptTokens = 0;
        int32 CandidateTokens = 0;
        int32 ThoughtTokens = 0;
        int32 CachedTokens = 0;
        (*UsagePtr)->TryGetNumberField(TEXT("promptTokenCount"), PromptTokens);
        (*UsagePtr)->TryGetNumberField(TEXT("candidatesTokenCount"), CandidateTokens);
        (*UsagePtr)->TryGetNumberField(TEXT("thoughtsTokenCount"), ThoughtTokens);
        (*UsagePtr)->TryGetNumberField(TEXT("cachedContentTokenCount"), CachedTokens);

        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CandidateTokens + ThoughtTokens;
        OutResponse.Usage.ReasoningTokens = ThoughtTokens;
        OutResponse.Usage.CacheReadInputTokens = CachedTokens;
        LogUsage(OutResponse.Usage, TEXT("GeminiResponseParser"));
    }

    // A blocked prompt or a candidate's finishReason decides before the content does
    if (CheckBlockAndFinishReason(ResponseObject, OutResponse))
    {
        return false;
    }

    // Extract message content from Gemini format - this is a special case
    // because Gemini's format is different from other providers
    FString MessageContent;
    if (!ExtractGeminiMessageContent(ResponseObject, MessageContent))
    {
        FString FinishReason;
        const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
        const TSharedPtr<FJsonObject>* Candidate = nullptr;
        if (ResponseObject->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates->Num() > 0
            && (*Candidates)[0].IsValid() && (*Candidates)[0]->TryGetObject(Candidate) && Candidate)
        {
            (*Candidate)->TryGetStringField(TEXT("finishReason"), FinishReason);
        }
        SetParseError(OutResponse, FString::Printf(TEXT("The provider response has no answer text (finishReason \"%s\")"),
            FinishReason.IsEmpty() ? TEXT("none") : *FinishReason));
        FN2CLogger::Get().LogError(OutResponse.Error.Message, TEXT("GeminiResponseParser"));
        return false;
    }

    FN2CLogger::Get().LogLLMContent(TEXT("LLM Response Message Content"), TEXT("LLM response message content"), MessageContent);

    // Parse the extracted content as our expected JSON format
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}

bool UN2CGeminiResponseParser::CheckBlockAndFinishReason(const TSharedPtr<FJsonObject>& JsonObject, FN2CTranslationResponse& OutResponse)
{
    // promptFeedback.blockReason: the prompt was blocked and no candidates are returned
    const TSharedPtr<FJsonObject>* Feedback = nullptr;
    FString BlockReason;
    if (JsonObject->TryGetObjectField(TEXT("promptFeedback"), Feedback) && Feedback && Feedback->IsValid()
        && (*Feedback)->TryGetStringField(TEXT("blockReason"), BlockReason) && !BlockReason.IsEmpty()
        && BlockReason != TEXT("BLOCK_REASON_UNSPECIFIED"))
    {
        FString BlockMessage;
        (*Feedback)->TryGetStringField(TEXT("blockReasonMessage"), BlockMessage);
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Blocked, BlockReason, FString::Printf(
            TEXT("Gemini blocked the prompt (promptFeedback.blockReason \"%s\")%s%s"), *BlockReason,
            BlockMessage.IsEmpty() ? TEXT("") : TEXT(": "), *BlockMessage));
        return true;
    }

    const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
    const TSharedPtr<FJsonObject>* Candidate = nullptr;
    if (!JsonObject->TryGetArrayField(TEXT("candidates"), Candidates) || Candidates->Num() == 0
        || !(*Candidates)[0].IsValid() || !(*Candidates)[0]->TryGetObject(Candidate) || !Candidate || !Candidate->IsValid())
    {
        return false;
    }

    FString FinishReason, FinishMessage;
    (*Candidate)->TryGetStringField(TEXT("finishReason"), FinishReason);
    (*Candidate)->TryGetStringField(TEXT("finishMessage"), FinishMessage);
    const FString Detail = FinishMessage.IsEmpty() ? FString() : FString::Printf(TEXT(": %s"), *FinishMessage);

    // MAX_TOKENS: maxOutputTokens reached (thinking counts); CONTINUATION: the per-request limit, answer unfinished
    if (FinishReason == TEXT("MAX_TOKENS") || FinishReason == TEXT("CONTINUATION"))
    {
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Truncated, FinishReason,
            MakeTruncatedMessage(FString::Printf(TEXT("finishReason \"%s\""), *FinishReason), N2CGeminiParserPrivate::SettingHint) + Detail);
        return true;
    }
    if (N2CGeminiParserPrivate::IsBlockingFinishReason(FinishReason))
    {
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Blocked, FinishReason, FString::Printf(
            TEXT("Gemini stopped the answer (finishReason \"%s\")%s"), *FinishReason, *Detail));
        return true;
    }
    return false;
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
    const TSharedPtr<FJsonObject>* CandidateObject = nullptr;
    if (!(*CandidatesArray)[0].IsValid() || !(*CandidatesArray)[0]->TryGetObject(CandidateObject) || !CandidateObject || !CandidateObject->IsValid())
    {
        return false;
    }

    // Get content object (absent when the candidate stopped before writing anything)
    const TSharedPtr<FJsonObject>* ContentObject = nullptr;
    if (!(*CandidateObject)->TryGetObjectField(TEXT("content"), ContentObject) || !ContentObject || !ContentObject->IsValid())
    {
        return false;
    }

    // Get parts array
    const TArray<TSharedPtr<FJsonValue>>* PartsArray;
    if (!(*ContentObject)->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0)
    {
        return false;
    }

    // Join every text part that is not a thought summary ("thought": true); the answer can span several parts
    FString Joined;
    bool bFoundText = false;
    for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
    {
        const TSharedPtr<FJsonObject>* PartObject = nullptr;
        if (!PartValue.IsValid() || !PartValue->TryGetObject(PartObject) || !PartObject || !PartObject->IsValid())
        {
            continue;
        }
        bool bThought = false;
        if ((*PartObject)->TryGetBoolField(TEXT("thought"), bThought) && bThought)
        {
            continue;
        }
        FString Text;
        if ((*PartObject)->TryGetStringField(TEXT("text"), Text))
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
