// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CTranslation.h"
#include "Dom/JsonObject.h"
#include "N2CResponseParserBase.generated.h"

/**
 * @class UN2CResponseParserBase
 * @brief Base class for parsing LLM responses into translation structs
 */
UCLASS(Abstract)
class NODETOCODE_API UN2CResponseParserBase : public UObject
{
    GENERATED_BODY()

public:
    /** Initialize with configuration */
    virtual void Initialize();

    /** Parse JSON response into translation structs */
    UFUNCTION(BlueprintCallable, Category = "LLM Integration")
    virtual bool ParseLLMResponse(
        const FString& InJson,
        FN2CTranslationResponse& OutResponse
    );

public:
    /**
     * Error handling shared by the provider parsers. Returns true when the response root carries an error (a non-null
     * object or a non-empty string in ErrorFieldName). It then fills OutResponse.Error (kind, HTTP status, the
     * provider's message, retryable) from the error and the "http_status" the HTTP handler adds, and logs it.
     */
    bool HandleCommonErrorResponse(
        const TSharedPtr<FJsonObject>& JsonObject,
        const FString& ErrorFieldName,
        FN2CTranslationResponse& OutResponse);

    /** Records a parse failure on OutResponse (kind Parse) unless an error is already recorded, and logs it */
    static void SetParseError(FN2CTranslationResponse& OutResponse, const FString& Message);

    /**
     * Records why the provider stopped (Truncated, Refused, Blocked, ...) unless an error is already recorded, and logs
     * it. ProviderReason is the provider's own stop or finish reason (max_tokens, length, SAFETY) and becomes the
     * error's provider_error_type.
     */
    static void SetStopReasonError(FN2CTranslationResponse& OutResponse, EN2CTranslationErrorKind Kind,
        const FString& ProviderReason, const FString& Message);

    /**
     * Message for an answer cut off at the output limit: names the provider's reason (for example
     * stop_reason "max_tokens") and the setting that raises the limit.
     */
    static FString MakeTruncatedMessage(const FString& ReasonText, const FString& SettingHint);

    /**
     * Kind for an OpenAI-compatible finish_reason (OpenAI Chat Completions, DeepSeek, LM Studio, vLLM): "length" is
     * Truncated, "content_filter" Blocked, DeepSeek's "insufficient_system_resource" Overloaded and "aborted" Server;
     * None for "stop", "tool_calls" and anything else.
     */
    static EN2CTranslationErrorKind KindFromFinishReason(const FString& FinishReason);

    /**
     * Reads choices[0].message of an OpenAI-compatible Chat Completions body once. Checks finish_reason and the
     * "refusal" field before the content, so a cut-off, filtered or refused answer is reported as Truncated, Blocked
     * or Refused (the Truncated message names SettingHint). A null, missing or blank content is a Parse error that
     * names the finish reason. Reasoning fields (reasoning_content, reasoning) are logged, never used as the answer.
     * Returns false with OutResponse.Error set when there is no answer to parse.
     */
    bool ExtractChatCompletionContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        const FString& SettingHint,
        FString& OutContent,
        FN2CTranslationResponse& OutResponse);

    /** Common content extraction from standard message formats */
    bool ExtractStandardMessageContent(
        const TSharedPtr<FJsonObject>& JsonObject,
        const FString& ArrayFieldName,
        const FString& MessageObjName,
        const FString& ContentFieldName,
        FString& OutContent);

    /**
     * True when Json ends inside an open object, array or string literal (braces inside strings are skipped), so the
     * text looks cut off rather than malformed. Used only to word the parse error.
     */
    static bool LooksCutOff(const FString& Json);

protected:
    /** Remove newlines from string */
    FString RemoveNewlines(const FString& Input) const;

    /** Validate response format */
    bool ValidateResponseFormat(const TSharedPtr<FJsonObject>& JsonObject) const;

    /** Extract graph data from JSON */
    void ExtractGraphData(
        const TSharedPtr<FJsonObject>& GraphObject,
        FN2CGraphTranslation& OutGraph
    );

    /** Extract code data from JSON */
    void ExtractCodeData(
        const TSharedPtr<FJsonObject>& CodeObject,
        FN2CGeneratedCode& OutCode
    );

    /** Process JSON content with code block markers */
    bool ProcessJsonContentWithMarkers(FString& Content);

    /** Logs how much reasoning text a message carries (reasoning_content, reasoning, thinking); never parses it */
    static void LogReasoningFields(const TSharedPtr<FJsonObject>& MessageObject, const FString& Category);
};
