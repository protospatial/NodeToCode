// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "N2CTranslation.generated.h"


/**
 * @struct FN2CTranslationUsage
 * @brief Tracks the token usage for LLM requests (input vs. output tokens).
 * Used to monitor or bill usage after a request completes.
 */
USTRUCT(BlueprintType)
struct FN2CTranslationUsage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 OutputTokens;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 InputTokens;
    
};
/**
 * @struct FN2CGeneratedCode
 * @brief Container for the generated code and implementation notes for a blueprint graph
 */
USTRUCT(BlueprintType)
struct FN2CGeneratedCode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphDeclaration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphImplementation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ImplementationNotes;
};

/**
 * @struct FN2CGraphTranslation
 * @brief Represents a single graph's translation from Blueprint to C++
 */
USTRUCT(BlueprintType)
struct FN2CGraphTranslation
{
    GENERATED_BODY()

    // Graph identification
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphClass;

    /** Code generation output */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CGeneratedCode Code;
};

/**
 * @enum EN2CTranslationErrorKind
 * @brief Why a translation failed. Names are stable: they appear as "error_kind" in MCP results and BatchSummary.json.
 */
UENUM(BlueprintType)
enum class EN2CTranslationErrorKind : uint8
{
    /** No error */
    None,
    /** The provider stopped at the output token limit */
    Truncated,
    /** The model refused the request */
    Refused,
    /** A provider safety filter blocked the request or the answer */
    Blocked,
    /** Too many requests (HTTP 429, rate_limit_error, RESOURCE_EXHAUSTED) */
    RateLimited,
    /** The provider is overloaded or unavailable (HTTP 503, 529, overloaded_error, UNAVAILABLE) */
    Overloaded,
    /** Missing, invalid or unauthorised API key (HTTP 401, 403) */
    Auth,
    /** The provider rejected the request: bad model id, bad parameter, too long (HTTP 400, 404, 413, 422) */
    InvalidRequest,
    /** Out of credit or quota (HTTP 402, insufficient_quota, Insufficient Balance) */
    Billing,
    /** Provider-side failure (HTTP 500 and other 5xx) */
    Server,
    /** No usable HTTP response: connection failure, timeout, proxy or gateway error (HTTP 502, 504, 52x) */
    Network,
    /** The response arrived but could not be read as a translation */
    Parse,
    /** An error the provider or NodeToCode reported that fits no other kind */
    Unknown,
    /**
     * No data arrived from the provider within the HTTP Activity Timeout, or the whole request ran past its total
     * timeout. Retryable. (Appended last so the values before it keep their numbers.)
     */
    Timeout
};

/**
 * @struct FN2CTranslationError
 * @brief A typed translation error: what kind, the HTTP status, the provider's own message and whether a retry may help.
 * Built by the HTTP handler (status and body) and the response parsers, carried in FN2CTranslationResponse to the UI,
 * the MCP translate result and batch items.
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTranslationError
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CTranslationErrorKind Kind = EN2CTranslationErrorKind::None;

    /** HTTP status of the provider's response; 0 when there was none (connection failure, error found before sending) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 HttpStatus = 0;

    /** The provider's own message (or NodeToCode's, for errors it found itself) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Message;

    /** The provider's error type or status code, for example rate_limit_error or RESOURCE_EXHAUSTED */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ProviderErrorType;

    /** The provider's request id when the error body carries one */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString RequestId;

    /** Provider and model the request went to, when known */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Provider;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Model;

    /** True when the same request may succeed later (rate limit, overload, network, server error) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bRetryable = false;

    bool IsError() const { return Kind != EN2CTranslationErrorKind::None; }

    /** One line for people: "Gemini request failed with HTTP 503 (Overloaded, retryable): <provider message>" */
    FString ToDisplayString() const;

    /**
     * The error as NodeToCode's error JSON: {"error": <display string>, "error_kind", "http_status", "retryable",
     * "message", plus "provider", "model", "provider_error_type", "request_id" when set}. Built with a JSON writer, so
     * any message text is escaped. MCP clients receive this as the translate tool's error text.
     */
    FString ToJsonString() const;
    TSharedRef<FJsonObject> ToJsonObject() const;

    /** A NodeToCode-made error (no provider response); Unknown when Kind is None */
    static FN2CTranslationError Make(const FString& Message, EN2CTranslationErrorKind Kind = EN2CTranslationErrorKind::Unknown, int32 HttpStatus = 0);

    /** A NodeToCode-made error (no provider response) as error JSON */
    static FString MakeErrorJson(const FString& Message, EN2CTranslationErrorKind Kind = EN2CTranslationErrorKind::Unknown, int32 HttpStatus = 0);

    /**
     * What the HTTP handler hands on for a non-2xx response. A JSON object body is kept and gains "http_status"; any
     * other body (an HTML proxy page, plain text) becomes {"error": <readable text>, "http_status": Status}. Always
     * valid JSON with a top-level "error".
     */
    static FString WrapHttpErrorBody(int32 HttpStatus, const FString& Body);

    /**
     * Reads an error from a response root that has an "error" field (an object in OpenAI, Anthropic, Gemini, DeepSeek
     * and LM Studio format, or a string as Ollama and NodeToCode send it) plus the "http_status", "error_kind" and
     * "retryable" fields NodeToCode adds. Classifies the kind from the provider's type first, then the HTTP status.
     */
    static FN2CTranslationError FromErrorJson(const TSharedPtr<FJsonObject>& Root);

    /** Kind for a provider error type, code or status string (rate_limit_error, RESOURCE_EXHAUSTED, ...); Unknown if none fits */
    static EN2CTranslationErrorKind KindFromProviderType(const FString& ProviderType, const FString& Message);

    /** Kind for an HTTP status; Unknown for statuses with no specific meaning, None for 0 */
    static EN2CTranslationErrorKind KindFromHttpStatus(int32 HttpStatus);

    static bool IsRetryableKind(EN2CTranslationErrorKind Kind);

    /** Stable name without the enum prefix, for example "RateLimited" */
    static FString KindToString(EN2CTranslationErrorKind Kind);
    static EN2CTranslationErrorKind KindFromString(const FString& Name);
};

/**
 * @struct FN2CTranslationResponse
 * @brief Container for all translated graphs from an LLM response
 */
USTRUCT(BlueprintType)
struct FN2CTranslationResponse
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CGraphTranslation> Graphs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CTranslationUsage Usage;

    /** Why the translation failed; Kind is None on success */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CTranslationError Error;
};


