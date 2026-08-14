// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2CMiniMaxResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

bool UN2CMiniMaxResponseParser::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Parse JSON string
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse MiniMax response JSON: %s"), *InJson),
            TEXT("MiniMaxResponseParser")
        );
        return false;
    }

    // Check for MiniMax error response (OpenAI-compatible format)
    FString ErrorMessage;
    if (JsonObject->HasField(TEXT("error")))
    {
        if (HandleCommonErrorResponse(JsonObject, TEXT("error"), ErrorMessage))
        {
            FN2CLogger::Get().LogError(ErrorMessage, TEXT("MiniMaxResponseParser"));
        }
        return false;
    }

    // Extract message content from OpenAI-compatible format
    FString MessageContent;
    if (!ExtractStandardMessageContent(JsonObject, TEXT("choices"), TEXT("message"), TEXT("content"), MessageContent))
    {
        FN2CLogger::Get().LogError(TEXT("Failed to extract message content from MiniMax response"), TEXT("MiniMaxResponseParser"));
        return false;
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
            FString::Printf(TEXT("MiniMax Token Usage - Input: %d Output: %d"), PromptTokens, CompletionTokens),
            EN2CLogSeverity::Info,
            TEXT("MiniMaxResponseParser")
        );
    }

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("MiniMax Response Message Content: %s"), *MessageContent),
        EN2CLogSeverity::Debug,
        TEXT("MiniMaxResponseParser")
    );

    // Parse the extracted content as our expected JSON format
    // Handle code block wrappers that some LLMs return
    FString ProcessedContent = MessageContent;
    ProcessedContent = ProcessedContent.TrimStartAndEnd();

    // Remove <think>...</think> reasoning tags that reasoning models use
    if (ProcessedContent.StartsWith(TEXT("<think>")))
    {
        int32 ThinkEnd = ProcessedContent.Find(TEXT("</think>"));
        if (ThinkEnd != INDEX_NONE)
        {
            ProcessedContent = ProcessedContent.Mid(ThinkEnd + 9);
        }
    }

    // Remove markdown code block wrappers - loop to handle double-wrapping like ```json\n```...```
    bool bRemovedWrapper = true;
    while (bRemovedWrapper && ProcessedContent.Len() > 6)
    {
        bRemovedWrapper = false;
        FString Before = ProcessedContent;

        ProcessedContent = ProcessedContent.TrimStartAndEnd();

        if (ProcessedContent.StartsWith(TEXT("```json")))
        {
            ProcessedContent = ProcessedContent.Mid(7);
            bRemovedWrapper = true;
        }
        else if (ProcessedContent.StartsWith(TEXT("```")))
        {
            ProcessedContent = ProcessedContent.Mid(3);
            bRemovedWrapper = true;
        }

        if (ProcessedContent.EndsWith(TEXT("```")))
        {
            ProcessedContent = ProcessedContent.LeftChop(3);
            bRemovedWrapper = true;
        }

        ProcessedContent = ProcessedContent.TrimStartAndEnd();

        // If nothing changed this iteration, stop
        if (ProcessedContent == Before)
            break;
    }

    // Log first 200 chars to see what we're getting
    FString First200 = ProcessedContent.Left(200);
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("MiniMax Content Preview (first 200): '%s'"), *First200),
        EN2CLogSeverity::Info,
        TEXT("MiniMaxResponseParser")
    );

    // Count braces to debug
    int32 Opens = 0, Closes = 0;
    for (const TCHAR& c : ProcessedContent)
    {
        if (c == '{') Opens++;
        else if (c == '}') Closes++;
    }
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("MiniMax Content brace count: { = %d, } = %d"), Opens, Closes),
        EN2CLogSeverity::Info,
        TEXT("MiniMaxResponseParser")
    );

    // Use the same recovery flow as the base class — try parse first, then truncate recovery
    FN2CTranslationResponse PrimaryResponse;
    if (TryParseJson(ProcessedContent, PrimaryResponse))
    {
        OutResponse = PrimaryResponse;
        return true;
    }

    // Recovery for truncated responses
    FN2CLogger::Get().LogWarning(
        TEXT("MiniMax response parse failed — attempting truncation recovery"),
        TEXT("MiniMaxResponseParser")
    );

    FN2CTranslationResponse RecoveredResponse;
    if (TryRecoverTruncatedJson(ProcessedContent, RecoveredResponse))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Truncation recovery succeeded: recovered %d graph(s)"),
                RecoveredResponse.Graphs.Num()),
            TEXT("MiniMaxResponseParser")
        );
        OutResponse = RecoveredResponse;
        return true;
    }

    FN2CLogger::Get().LogError(
        TEXT("Failed to parse MiniMax response: all recovery attempts failed"),
        TEXT("MiniMaxResponseParser")
    );
    return false;
}