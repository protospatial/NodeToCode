// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CLMStudioResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2CLMStudioResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse LM Studio response JSON: %s"), *InJson),
            TEXT("LMStudioResponseParser")
        );
        SetParseError(OutResponse, TEXT("The provider response is not valid JSON"));
        return false;
    }

    // Check for a provider error (the HTTP handler adds the HTTP status to it)
    if (HandleCommonErrorResponse(JsonObject, TEXT("error"), OutResponse))
    {
        return false;
    }

    // Extract usage information (OpenAI-compatible format); also on a truncated answer
    const TSharedPtr<FJsonObject>* UsagePtr = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("usage"), UsagePtr) && UsagePtr && UsagePtr->IsValid())
    {
        int32 PromptTokens = 0;
        int32 CompletionTokens = 0;
        (*UsagePtr)->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
        (*UsagePtr)->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
        
        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CompletionTokens;

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("LM Studio Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens), 
            EN2CLogSeverity::Info,
            TEXT("LMStudioResponseParser")
        );
    }

    // Log additional LM Studio-specific stats if available
    const TSharedPtr<FJsonObject>* StatsPtr = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("stats"), StatsPtr) && StatsPtr && StatsPtr->IsValid())
    {
        float TokensPerSecond = 0.0f;
        float TimeToFirstToken = 0.0f;
        float GenerationTime = 0.0f;
        
        (*StatsPtr)->TryGetNumberField(TEXT("tokens_per_second"), TokensPerSecond);
        (*StatsPtr)->TryGetNumberField(TEXT("time_to_first_token"), TimeToFirstToken);
        (*StatsPtr)->TryGetNumberField(TEXT("generation_time"), GenerationTime);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("LM Studio Performance - Tokens/sec: %.2f, TTFT: %.3fs, Gen Time: %.3fs"), 
                TokensPerSecond, TimeToFirstToken, GenerationTime), 
            EN2CLogSeverity::Debug,
            TEXT("LMStudioResponseParser")
        );
    }

    // Log model info if available (typed guard from PR #20, cryptosebek)
    const TSharedPtr<FJsonObject>* ModelInfoPtr = nullptr;
    if (JsonObject->TryGetObjectField(TEXT("model_info"), ModelInfoPtr) && ModelInfoPtr && ModelInfoPtr->IsValid())
    {
        FString Architecture, Quantization, Format;
        int32 ContextLength = 0;
        
        (*ModelInfoPtr)->TryGetStringField(TEXT("arch"), Architecture);
        (*ModelInfoPtr)->TryGetStringField(TEXT("quant"), Quantization);
        (*ModelInfoPtr)->TryGetStringField(TEXT("format"), Format);
        (*ModelInfoPtr)->TryGetNumberField(TEXT("context_length"), ContextLength);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("LM Studio Model Info - Arch: %s, Quant: %s, Format: %s, Context: %d"), 
                *Architecture, *Quantization, *Format, ContextLength), 
            EN2CLogSeverity::Debug,
            TEXT("LMStudioResponseParser")
        );
    }

    // Read choices[0].message once: finish_reason first, then the content (null or missing content is handled).
    // A reasoning model that ran out of budget while thinking leaves content empty next to reasoning_content and
    // finish_reason "length", which is reported as Truncated; reasoning text is logged, never parsed as the answer.
    FString MessageContent;
    if (!ExtractChatCompletionContent(JsonObject,
        TEXT("Project Settings > Node to Code > LLM Services > LM Studio > Max Output Tokens (0 = Auto sends none), ")
        TEXT("or the model's loaded context length on the server"), MessageContent, OutResponse))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from LM Studio response"), TEXT("LMStudioResponseParser"));
        return false;
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("LM Studio Response Message Content: %s"), *MessageContent), 
        EN2CLogSeverity::Debug,
        TEXT("LMStudioResponseParser")
    );

    // Parse the extracted content as our expected JSON format
    // The base class will handle parsing the structured N2C JSON response
    return Super::ParseLLMResponse(MessageContent, OutResponse);
}