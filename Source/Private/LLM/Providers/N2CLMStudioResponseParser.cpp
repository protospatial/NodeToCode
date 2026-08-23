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
        return false;
    }

    // Check for LM Studio error response (OpenAI-compatible format)
    FString ErrorMessage;
    if (JsonObject->HasField(TEXT("error")))
    {
        if (HandleCommonErrorResponse(JsonObject, TEXT("error"), ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("LMStudioResponseParser"));
        }
        return false;
    }

    // Extract message content from OpenAI-compatible format
    FString MessageContent;
    if (!ExtractStandardMessageContent(JsonObject, TEXT("choices"), TEXT("message"), TEXT("content"), MessageContent))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from LM Studio response"), TEXT("LMStudioResponseParser"));
        return false;
    }

    // Some reasoning models served through LM Studio return the final structured output
    // in reasoning_content while leaving content empty.
    if (MessageContent.TrimStartAndEnd().IsEmpty())
    {
        const TArray<TSharedPtr<FJsonValue>>* ChoicesArray = nullptr;
        if (JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) && ChoicesArray && ChoicesArray->Num() > 0)
        {
            const TSharedPtr<FJsonObject> ChoiceObject = (*ChoicesArray)[0]->AsObject();
            if (ChoiceObject.IsValid())
            {
                const TSharedPtr<FJsonObject> MessageObject = ChoiceObject->GetObjectField(TEXT("message"));
                if (MessageObject.IsValid())
                {
                    FString ReasoningContent;
                    if (MessageObject->TryGetStringField(TEXT("reasoning_content"), ReasoningContent) &&
                        !ReasoningContent.TrimStartAndEnd().IsEmpty())
                    {
                        MessageContent = ReasoningContent;
                        FN2CLogger::Get().Log(
                            TEXT("Using LM Studio reasoning_content because message content was empty"),
                            EN2CLogSeverity::Info,
                            TEXT("LMStudioResponseParser")
                        );
                    }
                }
            }
        }
    }

    // Extract usage information (OpenAI-compatible format)
    const TSharedPtr<FJsonObject> UsageObject = JsonObject->GetObjectField(TEXT("usage"));
    if (UsageObject.IsValid())
    {
        int32 PromptTokens = 0;
        int32 CompletionTokens = 0;
        UsageObject->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
        UsageObject->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
        
        OutResponse.Usage.InputTokens = PromptTokens;
        OutResponse.Usage.OutputTokens = CompletionTokens;

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("LM Studio Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens), 
            EN2CLogSeverity::Info,
            TEXT("LMStudioResponseParser")
        );
    }

    // Log additional LM Studio-specific stats if available
    const TSharedPtr<FJsonObject> StatsObject = JsonObject->GetObjectField(TEXT("stats"));
    if (StatsObject.IsValid())
    {
        float TokensPerSecond = 0.0f;
        float TimeToFirstToken = 0.0f;
        float GenerationTime = 0.0f;
        
        StatsObject->TryGetNumberField(TEXT("tokens_per_second"), TokensPerSecond);
        StatsObject->TryGetNumberField(TEXT("time_to_first_token"), TimeToFirstToken);
        StatsObject->TryGetNumberField(TEXT("generation_time"), GenerationTime);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("LM Studio Performance - Tokens/sec: %.2f, TTFT: %.3fs, Gen Time: %.3fs"), 
                TokensPerSecond, TimeToFirstToken, GenerationTime), 
            EN2CLogSeverity::Debug,
            TEXT("LMStudioResponseParser")
        );
    }

    // Log model info if available
    const TSharedPtr<FJsonObject> ModelInfoObject = JsonObject->HasTypedField<EJson::Object>(TEXT("model_info"))
        ? JsonObject->GetObjectField(TEXT("model_info"))
        : nullptr;
    if (ModelInfoObject.IsValid())
    {
        FString Architecture, Quantization, Format;
        int32 ContextLength = 0;
        
        ModelInfoObject->TryGetStringField(TEXT("arch"), Architecture);
        ModelInfoObject->TryGetStringField(TEXT("quant"), Quantization);
        ModelInfoObject->TryGetStringField(TEXT("format"), Format);
        ModelInfoObject->TryGetNumberField(TEXT("context_length"), ContextLength);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("LM Studio Model Info - Arch: %s, Quant: %s, Format: %s, Context: %d"), 
                *Architecture, *Quantization, *Format, ContextLength), 
            EN2CLogSeverity::Debug,
            TEXT("LMStudioResponseParser")
        );
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