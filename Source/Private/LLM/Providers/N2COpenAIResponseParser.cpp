// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool NormalizeEscapedCodeFormatting(FString& InOutCode)
{
    if (!InOutCode.Contains(TEXT("\\n")) && !InOutCode.Contains(TEXT("\\r")))
    {
        return false;
    }

    FString Normalized;
    Normalized.Reserve(InOutCode.Len());

    bool bInDoubleQuotedString = false;
    bool bInSingleQuotedChar = false;
    bool bEscapedWithinLiteral = false;
    int32 ActualLineBreaks = 0;
    int32 EscapedLineBreaks = 0;

    for (int32 Index = 0; Index < InOutCode.Len(); ++Index)
    {
        const TCHAR Char = InOutCode[Index];
        if (Char == '\n' || Char == '\r')
        {
            ++ActualLineBreaks;
        }

        if (bInDoubleQuotedString || bInSingleQuotedChar)
        {
            Normalized.AppendChar(Char);

            if (bEscapedWithinLiteral)
            {
                bEscapedWithinLiteral = false;
                continue;
            }
            if (Char == '\\')
            {
                bEscapedWithinLiteral = true;
                continue;
            }
            if (bInDoubleQuotedString && Char == '"')
            {
                bInDoubleQuotedString = false;
            }
            else if (bInSingleQuotedChar && Char == '\'')
            {
                bInSingleQuotedChar = false;
            }
            continue;
        }

        if (Char == '"')
        {
            bInDoubleQuotedString = true;
            Normalized.AppendChar(Char);
            continue;
        }
        if (Char == '\'')
        {
            bInSingleQuotedChar = true;
            Normalized.AppendChar(Char);
            continue;
        }

        if (Char == '\\' && Index + 1 < InOutCode.Len())
        {
            const TCHAR Next = InOutCode[Index + 1];
            if (Next == 'r')
            {
                ++EscapedLineBreaks;
                if (Index + 3 < InOutCode.Len() &&
                    InOutCode[Index + 2] == '\\' &&
                    InOutCode[Index + 3] == 'n')
                {
                    Normalized.AppendChar('\n');
                    Index += 3;
                }
                else
                {
                    Normalized.AppendChar('\r');
                    ++Index;
                }
                continue;
            }
            if (Next == 'n')
            {
                ++EscapedLineBreaks;
                Normalized.AppendChar('\n');
                ++Index;
                continue;
            }
            if (Next == 't')
            {
                Normalized.AppendChar('\t');
                ++Index;
                continue;
            }
        }

        Normalized.AppendChar(Char);
    }

    // A single escaped line break in otherwise multiline code may be intentional documentation.
    // Multiple structural escapes, or any escape in otherwise single-line code, indicate the
    // double-escaped response shape seen from some OpenAI-compatible structured-output servers.
    if (EscapedLineBreaks == 0 || (EscapedLineBreaks == 1 && ActualLineBreaks > 0))
    {
        return false;
    }

    InOutCode = MoveTemp(Normalized);
    return true;
}

void NormalizeEscapedPlainTextFormatting(FString& InOutText)
{
    InOutText.ReplaceInline(TEXT("\\r\\n"), TEXT("\n"), ESearchCase::CaseSensitive);
    InOutText.ReplaceInline(TEXT("\\n"), TEXT("\n"), ESearchCase::CaseSensitive);
    InOutText.ReplaceInline(TEXT("\\r"), TEXT("\r"), ESearchCase::CaseSensitive);
    InOutText.ReplaceInline(TEXT("\\t"), TEXT("\t"), ESearchCase::CaseSensitive);
}

int32 NormalizeOverEscapedGeneratedCode(FN2CTranslationResponse& Response)
{
    int32 NormalizedGraphs = 0;
    for (FN2CGraphTranslation& Graph : Response.Graphs)
    {
        const bool bDeclarationNormalized = NormalizeEscapedCodeFormatting(Graph.Code.GraphDeclaration);
        const bool bImplementationNormalized = NormalizeEscapedCodeFormatting(Graph.Code.GraphImplementation);
        if (!bDeclarationNormalized && !bImplementationNormalized)
        {
            continue;
        }

        NormalizeEscapedPlainTextFormatting(Graph.Code.ImplementationNotes);
        ++NormalizedGraphs;
    }
    return NormalizedGraphs;
}
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

    // Parse the extracted content as our expected JSON format.
    if (!Super::ParseLLMResponse(MessageContent, OutResponse))
    {
        return false;
    }

    const int32 NormalizedGraphs = NormalizeOverEscapedGeneratedCode(OutResponse);
    if (NormalizedGraphs > 0)
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(
                TEXT("Normalized over-escaped line formatting in %d OpenAI-compatible graph response(s)"),
                NormalizedGraphs),
            TEXT("OpenAIResponseParser"));
    }

    return true;
}
