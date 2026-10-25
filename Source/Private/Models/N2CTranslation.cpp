// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Models/N2CTranslation.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CTranslationErrorPrivate
{
    /** Longest provider text kept from a non-JSON error body */
    constexpr int32 MaxReadableBodyChars = 300;

    FString SerializeCondensed(const TSharedRef<FJsonObject>& Object)
    {
        FString Out;
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
        FJsonSerializer::Serialize(Object, Writer);
        return Out;
    }

    /** Collapses runs of whitespace (including newlines) to one space and trims */
    FString CollapseWhitespace(const FString& In)
    {
        FString Out;
        Out.Reserve(In.Len());
        bool bLastWasSpace = true;
        for (const TCHAR Char : In)
        {
            if (FChar::IsWhitespace(Char))
            {
                if (!bLastWasSpace)
                {
                    Out.AppendChar(TEXT(' '));
                    bLastWasSpace = true;
                }
                continue;
            }
            Out.AppendChar(Char);
            bLastWasSpace = false;
        }
        return Out.TrimEnd();
    }

    /** Drops <...> tags (an HTML error page reduced to its text) */
    FString StripTags(const FString& In)
    {
        FString Out;
        Out.Reserve(In.Len());
        bool bInTag = false;
        for (const TCHAR Char : In)
        {
            if (Char == TEXT('<'))
            {
                bInTag = true;
                Out.AppendChar(TEXT(' '));
            }
            else if (Char == TEXT('>'))
            {
                bInTag = false;
            }
            else if (!bInTag)
            {
                Out.AppendChar(Char);
            }
        }
        return Out;
    }

    /** A short readable text for a non-JSON error body: an HTML page's <title> (or its text), or plain text */
    FString ReadableBodyText(const FString& Body)
    {
        FString Text;
        const bool bIsHtml = Body.Contains(TEXT("<html"), ESearchCase::IgnoreCase)
            || Body.Contains(TEXT("<!doctype"), ESearchCase::IgnoreCase);
        if (bIsHtml)
        {
            const int32 TitleStart = Body.Find(TEXT("<title>"), ESearchCase::IgnoreCase);
            const int32 TitleEnd = TitleStart == INDEX_NONE ? INDEX_NONE
                : Body.Find(TEXT("</title>"), ESearchCase::IgnoreCase, ESearchDir::FromStart, TitleStart);
            if (TitleStart != INDEX_NONE && TitleEnd != INDEX_NONE)
            {
                const int32 ContentStart = TitleStart + 7;
                Text = CollapseWhitespace(Body.Mid(ContentStart, TitleEnd - ContentStart));
            }
            if (Text.IsEmpty())
            {
                Text = CollapseWhitespace(StripTags(Body));
            }
        }
        else
        {
            Text = CollapseWhitespace(Body);
        }

        if (Text.Len() > MaxReadableBodyChars)
        {
            Text = Text.Left(MaxReadableBodyChars) + TEXT("...");
        }
        return Text;
    }

    bool ContainsAny(const FString& Haystack, std::initializer_list<const TCHAR*> Needles)
    {
        for (const TCHAR* Needle : Needles)
        {
            if (Haystack.Contains(Needle, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
        return false;
    }
}

FString FN2CTranslationError::KindToString(EN2CTranslationErrorKind InKind)
{
    return StaticEnum<EN2CTranslationErrorKind>()->GetNameStringByValue(static_cast<int64>(InKind));
}

EN2CTranslationErrorKind FN2CTranslationError::KindFromString(const FString& Name)
{
    const int64 Value = StaticEnum<EN2CTranslationErrorKind>()->GetValueByNameString(Name);
    return Value == INDEX_NONE ? EN2CTranslationErrorKind::Unknown : static_cast<EN2CTranslationErrorKind>(Value);
}

bool FN2CTranslationError::IsRetryableKind(EN2CTranslationErrorKind InKind)
{
    switch (InKind)
    {
    case EN2CTranslationErrorKind::RateLimited:
    case EN2CTranslationErrorKind::Overloaded:
    case EN2CTranslationErrorKind::Server:
    case EN2CTranslationErrorKind::Network:
    case EN2CTranslationErrorKind::Timeout:
        return true;
    default:
        return false;
    }
}

EN2CTranslationErrorKind FN2CTranslationError::KindFromHttpStatus(int32 Status)
{
    if (Status <= 0)
    {
        return EN2CTranslationErrorKind::None;
    }
    switch (Status)
    {
    case 401:
    case 403:
        return EN2CTranslationErrorKind::Auth;
    case 402:
        return EN2CTranslationErrorKind::Billing;
    case 408:
        return EN2CTranslationErrorKind::Network;
    case 429:
        return EN2CTranslationErrorKind::RateLimited;
    case 503:
    case 529:
        return EN2CTranslationErrorKind::Overloaded;
    case 502:
    case 504:
    case 520:
    case 521:
    case 522:
    case 523:
    case 524:
        return EN2CTranslationErrorKind::Network;
    default:
        break;
    }
    if (Status >= 400 && Status < 500)
    {
        return EN2CTranslationErrorKind::InvalidRequest;
    }
    if (Status >= 500 && Status < 600)
    {
        return EN2CTranslationErrorKind::Server;
    }
    return EN2CTranslationErrorKind::Unknown;
}

EN2CTranslationErrorKind FN2CTranslationError::KindFromProviderType(const FString& ProviderType, const FString& InMessage)
{
    using N2CTranslationErrorPrivate::ContainsAny;

    // Billing first: Anthropic reports a low credit balance as an invalid_request_error
    if (ContainsAny(ProviderType, { TEXT("insufficient_quota"), TEXT("billing"), TEXT("insufficient_balance") })
        || ContainsAny(InMessage, { TEXT("insufficient balance"), TEXT("credit balance is too low") }))
    {
        return EN2CTranslationErrorKind::Billing;
    }
    if (ContainsAny(ProviderType, { TEXT("rate_limit"), TEXT("RESOURCE_EXHAUSTED"), TEXT("too_many_requests") }))
    {
        return EN2CTranslationErrorKind::RateLimited;
    }
    if (ContainsAny(ProviderType, { TEXT("overloaded"), TEXT("UNAVAILABLE") }))
    {
        return EN2CTranslationErrorKind::Overloaded;
    }
    // Gemini reports a bad key as 400 INVALID_ARGUMENT with "API key not valid" in the message
    if (ContainsAny(ProviderType, { TEXT("authentication"), TEXT("permission"), TEXT("invalid_api_key"),
        TEXT("UNAUTHENTICATED"), TEXT("unauthorized"), TEXT("API_KEY_INVALID") })
        || ContainsAny(InMessage, { TEXT("API key not valid"), TEXT("Incorrect API key"), TEXT("invalid x-api-key") }))
    {
        return EN2CTranslationErrorKind::Auth;
    }
    if (ContainsAny(ProviderType, { TEXT("invalid_request"), TEXT("INVALID_ARGUMENT"), TEXT("not_found"),
        TEXT("FAILED_PRECONDITION"), TEXT("request_too_large"), TEXT("context_length") }))
    {
        return EN2CTranslationErrorKind::InvalidRequest;
    }
    if (ContainsAny(ProviderType, { TEXT("DEADLINE_EXCEEDED"), TEXT("timeout") }))
    {
        return EN2CTranslationErrorKind::Network;
    }
    if (ContainsAny(ProviderType, { TEXT("api_error"), TEXT("server_error"), TEXT("INTERNAL") }))
    {
        return EN2CTranslationErrorKind::Server;
    }
    return EN2CTranslationErrorKind::Unknown;
}

FString FN2CTranslationError::ToDisplayString() const
{
    const FString Who = Provider.IsEmpty() ? FString(TEXT("Request")) : FString::Printf(TEXT("%s request"), *Provider);
    const FString Detail = bRetryable
        ? FString::Printf(TEXT("%s, retryable"), *KindToString(Kind))
        : KindToString(Kind);
    const FString Head = HttpStatus > 0
        ? FString::Printf(TEXT("%s failed with HTTP %d (%s)"), *Who, HttpStatus, *Detail)
        : FString::Printf(TEXT("%s failed (%s)"), *Who, *Detail);
    return Message.IsEmpty() ? Head : FString::Printf(TEXT("%s: %s"), *Head, *Message);
}

TSharedRef<FJsonObject> FN2CTranslationError::ToJsonObject() const
{
    TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
    // "error" first: consumers recognise NodeToCode error JSON by a top-level "error"
    Object->SetStringField(TEXT("error"), ToDisplayString());
    Object->SetStringField(TEXT("error_kind"), KindToString(Kind));
    Object->SetStringField(TEXT("message"), Message);
    if (HttpStatus > 0)
    {
        Object->SetNumberField(TEXT("http_status"), HttpStatus);
    }
    Object->SetBoolField(TEXT("retryable"), bRetryable);
    if (!Provider.IsEmpty())
    {
        Object->SetStringField(TEXT("provider"), Provider);
    }
    if (!Model.IsEmpty())
    {
        Object->SetStringField(TEXT("model"), Model);
    }
    if (!ProviderErrorType.IsEmpty())
    {
        Object->SetStringField(TEXT("provider_error_type"), ProviderErrorType);
    }
    if (!RequestId.IsEmpty())
    {
        Object->SetStringField(TEXT("request_id"), RequestId);
    }
    return Object;
}

FString FN2CTranslationError::ToJsonString() const
{
    return N2CTranslationErrorPrivate::SerializeCondensed(ToJsonObject());
}

FN2CTranslationError FN2CTranslationError::Make(const FString& InMessage, EN2CTranslationErrorKind InKind, int32 InHttpStatus)
{
    FN2CTranslationError Error;
    Error.Kind = InKind == EN2CTranslationErrorKind::None ? EN2CTranslationErrorKind::Unknown : InKind;
    Error.Message = InMessage;
    Error.HttpStatus = InHttpStatus;
    Error.bRetryable = IsRetryableKind(Error.Kind);
    return Error;
}

FString FN2CTranslationError::MakeErrorJson(const FString& InMessage, EN2CTranslationErrorKind InKind, int32 InHttpStatus)
{
    return Make(InMessage, InKind, InHttpStatus).ToJsonString();
}

FString FN2CTranslationError::WrapHttpErrorBody(int32 InHttpStatus, const FString& Body)
{
    using namespace N2CTranslationErrorPrivate;

    const FString Trimmed = Body.TrimStartAndEnd();

    // A JSON error body (an object, or Gemini's occasional one-element array) is kept as the provider sent it
    TSharedPtr<FJsonObject> ProviderObject;
    if (Trimmed.StartsWith(TEXT("{")))
    {
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
        FJsonSerializer::Deserialize(Reader, ProviderObject);
    }
    else if (Trimmed.StartsWith(TEXT("[")))
    {
        TArray<TSharedPtr<FJsonValue>> Array;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
        if (FJsonSerializer::Deserialize(Reader, Array) && Array.Num() > 0 && Array[0].IsValid()
            && Array[0]->Type == EJson::Object)
        {
            ProviderObject = Array[0]->AsObject();
        }
    }

    if (ProviderObject.IsValid())
    {
        const TSharedPtr<FJsonValue> ErrorField = ProviderObject->TryGetField(TEXT("error"));
        if (!ErrorField.IsValid() || ErrorField->IsNull())
        {
            // JSON without an error field (some proxies): its message, or the whole body, becomes the error
            FString Text;
            if (!ProviderObject->TryGetStringField(TEXT("message"), Text) || Text.IsEmpty())
            {
                Text = ReadableBodyText(Trimmed);
            }
            ProviderObject->SetStringField(TEXT("error"), Text.IsEmpty() ? FString::Printf(TEXT("HTTP %d"), InHttpStatus) : Text);
        }
        ProviderObject->SetNumberField(TEXT("http_status"), InHttpStatus);
        return SerializeCondensed(ProviderObject.ToSharedRef());
    }

    // Anything else (an HTML proxy page, LM Studio or Ollama plain text) is reduced to readable text and escaped
    const FString Text = ReadableBodyText(Trimmed);
    TSharedRef<FJsonObject> Wrapped = MakeShared<FJsonObject>();
    Wrapped->SetStringField(TEXT("error"), Text.IsEmpty() ? FString::Printf(TEXT("HTTP %d with an empty body"), InHttpStatus) : Text);
    Wrapped->SetNumberField(TEXT("http_status"), InHttpStatus);
    return SerializeCondensed(Wrapped);
}

FN2CTranslationError FN2CTranslationError::FromErrorJson(const TSharedPtr<FJsonObject>& Root)
{
    FN2CTranslationError Error;
    if (!Root.IsValid())
    {
        Error.Kind = EN2CTranslationErrorKind::Unknown;
        Error.Message = TEXT("Empty error response");
        return Error;
    }

    int32 Status = 0;
    if (Root->TryGetNumberField(TEXT("http_status"), Status))
    {
        Error.HttpStatus = Status;
    }

    // Text used to classify: the provider's type, status and code strings
    FString ClassifyText;
    const TSharedPtr<FJsonValue> ErrorField = Root->TryGetField(TEXT("error"));
    if (ErrorField.IsValid() && ErrorField->Type == EJson::Object)
    {
        const TSharedPtr<FJsonObject> ErrorObject = ErrorField->AsObject();
        FString Type, StatusName, Code;
        ErrorObject->TryGetStringField(TEXT("message"), Error.Message);
        ErrorObject->TryGetStringField(TEXT("type"), Type);
        ErrorObject->TryGetStringField(TEXT("status"), StatusName);   // Gemini: RESOURCE_EXHAUSTED, UNAVAILABLE
        ErrorObject->TryGetStringField(TEXT("code"), Code);           // OpenAI: invalid_api_key, insufficient_quota

        int32 NumericCode = 0;
        if (Error.HttpStatus == 0 && ErrorObject->TryGetNumberField(TEXT("code"), NumericCode)
            && NumericCode >= 400 && NumericCode < 600)
        {
            Error.HttpStatus = NumericCode;  // Gemini repeats the HTTP status as error.code
        }

        Error.ProviderErrorType = !Type.IsEmpty() ? Type : (!StatusName.IsEmpty() ? StatusName : Code);
        ClassifyText = FString::Join(TArray<FString>{ Type, StatusName, Code }, TEXT(" "));
    }
    else if (ErrorField.IsValid() && ErrorField->Type == EJson::String)
    {
        Error.Message = ErrorField->AsString();
    }

    // NodeToCode's own error JSON carries the raw message next to the display string in "error"
    FString RawMessage;
    if (Root->TryGetStringField(TEXT("message"), RawMessage) && !RawMessage.IsEmpty()
        && !(ErrorField.IsValid() && ErrorField->Type == EJson::Object))
    {
        Error.Message = RawMessage;
    }
    if (Error.Message.IsEmpty())
    {
        Error.Message = TEXT("The provider returned an error without a message");
    }

    Root->TryGetStringField(TEXT("request_id"), Error.RequestId);
    Root->TryGetStringField(TEXT("provider"), Error.Provider);
    Root->TryGetStringField(TEXT("model"), Error.Model);
    FString StoredType;
    if (Error.ProviderErrorType.IsEmpty() && Root->TryGetStringField(TEXT("provider_error_type"), StoredType))
    {
        Error.ProviderErrorType = StoredType;
        ClassifyText = StoredType;
    }

    // Kind: NodeToCode's own label, else the provider's type, else the HTTP status
    FString KindName;
    if (Root->TryGetStringField(TEXT("error_kind"), KindName))
    {
        Error.Kind = KindFromString(KindName);
    }
    if (Error.Kind == EN2CTranslationErrorKind::None || Error.Kind == EN2CTranslationErrorKind::Unknown)
    {
        Error.Kind = KindFromProviderType(ClassifyText, Error.Message);
    }
    if (Error.Kind == EN2CTranslationErrorKind::Unknown)
    {
        const EN2CTranslationErrorKind FromStatus = KindFromHttpStatus(Error.HttpStatus);
        Error.Kind = FromStatus == EN2CTranslationErrorKind::None ? EN2CTranslationErrorKind::Unknown : FromStatus;
    }

    bool bStoredRetryable = false;
    Error.bRetryable = Root->TryGetBoolField(TEXT("retryable"), bStoredRetryable) ? bStoredRetryable : IsRetryableKind(Error.Kind);
    return Error;
}
