// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.generated.h"

/** Delegate for receiving LLM responses */
DECLARE_DELEGATE_OneParam(FOnLLMResponseReceived, const FString& /* Response */);

/** Delegate for receiving parsed translation responses */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTranslationResponseReceived, const FN2CTranslationResponse&, Response, bool, bSuccess);

/** Per-request delegate for one parsed translation (native; the request's owner gets only its own result) */
DECLARE_DELEGATE_TwoParams(FOnN2CTranslationParsed, const FN2CTranslationResponse& /* Response */, bool /* bSuccess */);

/** Delegate for when a translation request is sent */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTranslationRequestSent);

/** Available LLM providers */
UENUM(BlueprintType)
enum class EN2CLLMProvider : uint8
{
    OpenAI      UMETA(DisplayName = "OpenAI"),
    Anthropic   UMETA(DisplayName = "Anthropic"),
    Gemini      UMETA(DisplayName = "Gemini"),
    Ollama      UMETA(DisplayName = "Ollama"),
    DeepSeek    UMETA(DisplayName = "DeepSeek"),
    LMStudio    UMETA(DisplayName = "LM Studio")
};

/** Status of the Node to Code system */
UENUM(BlueprintType)
enum class EN2CSystemStatus : uint8
{
    Idle        UMETA(DisplayName = "Idle"),
    Processing  UMETA(DisplayName = "Processing Request"),
    Error       UMETA(DisplayName = "Error"),
    Initializing UMETA(DisplayName = "Initializing")
};

/**
 * @struct FN2CLLMConfig
 * @brief Configuration settings for LLM integration
 */
USTRUCT(BlueprintType)
struct FN2CLLMConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    FString ApiEndpoint;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    FString ApiKey;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    float TimeoutSeconds = 3600.0f;

    /**
     * Seconds the HTTP request may go without receiving any data before it fails with a Timeout error (the HTTP
     * activity timeout). Requests are not streamed, so it must exceed the longest expected generation.
     * UN2CSettings::ResolveRequestModel fills it from the HTTP Activity Timeout setting.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    float ActivityTimeoutSeconds = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    bool bUseSystemPrompts = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    FString Model;

    /**
     * Thinking control for this request, for providers that switch thinking per request (DeepSeek: "none",
     * "low", "high" or "max"; see FN2CModelMetadata::ReasoningEffort). Empty: the registry entry for Model
     * decides. UN2CSettings::ResolveRequestModel fills it from the selected model.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    FString ReasoningEffort;

    /**
     * Output token budget for this request. 0 is Auto: cloud providers get the model's registry maximum (held to
     * UN2CLLMPayloadBuilder::NonStreamingAutoCap while requests are not streamed), reduced so
     * prompt plus output fits its context window, and local servers (LM Studio, vLLM) get no field so the server
     * uses the context left after the prompt. A value above 0 is sent as-is, capped at the registry maximum when
     * the model is registered. UN2CSettings::ResolveRequestModel fills it from the provider's Max Output Tokens
     * setting; Ollama ignores it (its own Max Tokens setting, num_predict, applies).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LLM Integration")
    int32 MaxOutputTokens = 0;
};
