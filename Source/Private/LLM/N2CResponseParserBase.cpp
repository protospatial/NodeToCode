// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CResponseParserBase.h"
#include "Utils/N2CLogger.h"
#include "Serialization/JsonSerializer.h"

void UN2CResponseParserBase::Initialize()
{
    // Base initialization - can be extended by derived classes
}

bool UN2CResponseParserBase::ParseLLMResponse(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Check for empty or obviously invalid responses
    if (InJson.IsEmpty() || InJson.Len() < 10)
    {
        FN2CLogger::Get().LogError(TEXT("Empty or too short LLM response"), TEXT("ResponseParser"));
        SetParseError(OutResponse, TEXT("The model's answer is empty or too short"));
        return false;
    }

    // Parse JSON string with additional safety
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);

    bool bParseSuccess = false;
    
    // Use a try-catch to handle potential crashes in the JSON parser
    try
    {
        bParseSuccess = FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid();
    }
    catch (...)
    {
        FN2CLogger::Get().LogError(TEXT("Exception while parsing LLM JSON response"), 
            TEXT("ResponseParser"));
        SetParseError(OutResponse, TEXT("The model's answer could not be parsed as JSON"));
        return false;
    }

    if (!bParseSuccess)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse JSON response: %s"), 
                *Reader->GetErrorMessage()),
            TEXT("ResponseParser")
        );
        // The provider said it finished (stop reasons are checked before this), so an answer that ends inside an
        // open object or string was cut off some other way, for example by a proxy or a local server's context limit
        SetParseError(OutResponse, LooksCutOff(InJson)
            ? FString::Printf(TEXT("The model's answer is not valid JSON and looks cut off (it ends inside an open object or string): %s"), *Reader->GetErrorMessage())
            : FString::Printf(TEXT("The model's answer is not valid JSON: %s"), *Reader->GetErrorMessage()));
        return false;
    }

    // Validate basic structure
    if (!ValidateResponseFormat(JsonObject))
    {
        FN2CLogger::Get().LogError(TEXT("Invalid response format"), TEXT("ResponseParser"));
        SetParseError(OutResponse, TEXT("The model's answer has no 'graphs' field"));
        return false;
    }

    // Get graphs array
    const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
    if (!JsonObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing 'graphs' array in response"), TEXT("ResponseParser"));
        SetParseError(OutResponse, TEXT("The model's answer has no 'graphs' array"));
        return false;
    }

    // Process each graph
    OutResponse.Graphs.Empty();
    for (const auto& GraphValue : *GraphsArray)
    {
        TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
        if (!GraphObject.IsValid())
        {
            FN2CLogger::Get().LogWarning(TEXT("Invalid graph object in array"), TEXT("ResponseParser"));
            continue;
        }

        FN2CGraphTranslation Graph;
        ExtractGraphData(GraphObject, Graph);
        OutResponse.Graphs.Add(Graph);
    }

    if (OutResponse.Graphs.Num() == 0)
    {
        SetParseError(OutResponse, TEXT("The model's answer contains no translated graphs"));
        return false;
    }
    return true;
}

FString UN2CResponseParserBase::RemoveNewlines(const FString& Input) const
{
    return Input.Replace(TEXT("\n"), TEXT(""));
}

bool UN2CResponseParserBase::ValidateResponseFormat(const TSharedPtr<FJsonObject>& JsonObject) const
{
    if (!JsonObject->HasField(TEXT("graphs")))
    {
        FN2CLogger::Get().LogError(TEXT("Response missing 'graphs' field"), TEXT("ResponseParser"));
        return false;
    }

    return true;
}

void UN2CResponseParserBase::ExtractGraphData(
    const TSharedPtr<FJsonObject>& GraphObject,
    FN2CGraphTranslation& OutGraph)
{
    // Extract basic graph information
    OutGraph.GraphName = GraphObject->GetStringField(TEXT("graph_name"));
    OutGraph.GraphType = GraphObject->GetStringField(TEXT("graph_type"));
    OutGraph.GraphClass = GraphObject->GetStringField(TEXT("graph_class"));

    // Extract code data
    const TSharedPtr<FJsonObject> CodeObject = GraphObject->GetObjectField(TEXT("code"));
    if (CodeObject.IsValid())
    {
        ExtractCodeData(CodeObject, OutGraph.Code);
    }
    else
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Missing code data for graph: %s"), *OutGraph.GraphName),
            TEXT("ResponseParser")
        );
    }
}

void UN2CResponseParserBase::ExtractCodeData(
    const TSharedPtr<FJsonObject>& CodeObject,
    FN2CGeneratedCode& OutCode)
{
    OutCode.GraphDeclaration = CodeObject->GetStringField(TEXT("graphDeclaration"));
    OutCode.GraphImplementation = CodeObject->GetStringField(TEXT("graphImplementation"));
    OutCode.ImplementationNotes = CodeObject->GetStringField(TEXT("implementationNotes"));
}

bool UN2CResponseParserBase::HandleCommonErrorResponse(
    const TSharedPtr<FJsonObject>& JsonObject,
    const FString& ErrorFieldName,
    FN2CTranslationResponse& OutResponse)
{
    if (!JsonObject.IsValid())
    {
        return false;
    }

    // The OpenAI Responses API sends "error": null on success; an empty string is no error either
    const TSharedPtr<FJsonValue> ErrorField = JsonObject->TryGetField(ErrorFieldName);
    if (!ErrorField.IsValid() || ErrorField->IsNull()
        || (ErrorField->Type == EJson::String && ErrorField->AsString().IsEmpty()))
    {
        return false;
    }

    TSharedPtr<FJsonObject> Root = JsonObject;
    if (ErrorFieldName != TEXT("error"))
    {
        // FromErrorJson reads "error": present the custom field under that name
        Root = MakeShared<FJsonObject>(*JsonObject);
        Root->SetField(TEXT("error"), ErrorField);
    }

    OutResponse.Error = FN2CTranslationError::FromErrorJson(Root);
    FN2CLogger::Get().LogError(OutResponse.Error.ToDisplayString(), TEXT("ResponseParser"));
    return true;
}

void UN2CResponseParserBase::SetParseError(FN2CTranslationResponse& OutResponse, const FString& Message)
{
    if (OutResponse.Error.IsError())
    {
        return;
    }
    OutResponse.Error.Kind = EN2CTranslationErrorKind::Parse;
    OutResponse.Error.Message = Message;
    OutResponse.Error.bRetryable = false;
}

bool UN2CResponseParserBase::ExtractStandardMessageContent(
    const TSharedPtr<FJsonObject>& JsonObject,
    const FString& ArrayFieldName,
    const FString& MessageObjName,
    const FString& ContentFieldName,
    FString& OutContent)
{
    // Get array field (choices, candidates, etc.)
    const TArray<TSharedPtr<FJsonValue>>* ItemsArray;
    if (!JsonObject->TryGetArrayField(ArrayFieldName, ItemsArray) || ItemsArray->Num() == 0)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Missing or empty '%s' array in response"), *ArrayFieldName),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Get first item
    const TSharedPtr<FJsonObject> ItemObject = (*ItemsArray)[0]->AsObject();
    if (!ItemObject.IsValid())
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Invalid object in '%s' array"), *ArrayFieldName),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Get message object if needed
    TSharedPtr<FJsonObject> MessageObject;
    if (MessageObjName.IsEmpty())
    {
        // Use the item object directly
        MessageObject = ItemObject;
    }
    else
    {
        // Get the message object from the item
        const TSharedPtr<FJsonObject>* MessagePtr = nullptr;
        if (ItemObject->TryGetObjectField(MessageObjName, MessagePtr) && MessagePtr)
        {
            MessageObject = *MessagePtr;
        }
        if (!MessageObject.IsValid())
        {
            FN2CLogger::Get().LogError(
                FString::Printf(TEXT("Missing '%s' object in response"), *MessageObjName),
                TEXT("ResponseParser")
            );
            return false;
        }
    }

    // Get content string
    FString RawContent;
    if (!MessageObject->TryGetStringField(ContentFieldName, RawContent))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Missing '%s' field in response"), *ContentFieldName),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Process content for JSON markers
    if (ProcessJsonContentWithMarkers(RawContent))
    {
        FN2CLogger::Get().Log(TEXT("Stripped JSON markers from response"), EN2CLogSeverity::Debug);
    }
    
    OutContent = RawContent;
    return true;
}

bool UN2CResponseParserBase::ProcessJsonContentWithMarkers(FString& Content)
{
    // Check if content is wrapped in ```json markers and remove them
    if (Content.StartsWith(TEXT("```json")) && Content.EndsWith(TEXT("```")))
    {
        // Remove the ```json prefix and ``` suffix
        Content = Content.RightChop(7); // Skip past "```json"
        Content = Content.LeftChop(3);  // Remove trailing "```"
        Content = Content.TrimStartAndEnd(); // Remove any extra whitespace
        return true;
    }
    return false;
}

void UN2CResponseParserBase::SetStopReasonError(FN2CTranslationResponse& OutResponse, EN2CTranslationErrorKind Kind,
    const FString& ProviderReason, const FString& Message)
{
    if (OutResponse.Error.IsError())
    {
        return;
    }
    OutResponse.Error.Kind = Kind == EN2CTranslationErrorKind::None ? EN2CTranslationErrorKind::Unknown : Kind;
    OutResponse.Error.Message = Message;
    OutResponse.Error.ProviderErrorType = ProviderReason;
    OutResponse.Error.bRetryable = FN2CTranslationError::IsRetryableKind(OutResponse.Error.Kind);
    FN2CLogger::Get().LogError(OutResponse.Error.ToDisplayString(), TEXT("ResponseParser"));
}

FString UN2CResponseParserBase::MakeTruncatedMessage(const FString& ReasonText, const FString& SettingHint)
{
    return FString::Printf(
        TEXT("The answer stopped at the output token limit (%s) before the translation was complete. Raise %s; ")
        TEXT("thinking tokens count against it on reasoning models."),
        *ReasonText, *SettingHint);
}

EN2CTranslationErrorKind UN2CResponseParserBase::KindFromFinishReason(const FString& FinishReason)
{
    if (FinishReason == TEXT("length"))
    {
        return EN2CTranslationErrorKind::Truncated;
    }
    if (FinishReason == TEXT("content_filter"))
    {
        return EN2CTranslationErrorKind::Blocked;
    }
    // DeepSeek (api-docs.deepseek.com/api/create-chat-completion)
    if (FinishReason == TEXT("insufficient_system_resource"))
    {
        return EN2CTranslationErrorKind::Overloaded;
    }
    if (FinishReason == TEXT("aborted"))
    {
        return EN2CTranslationErrorKind::Server;
    }
    return EN2CTranslationErrorKind::None;
}

void UN2CResponseParserBase::LogReasoningFields(const TSharedPtr<FJsonObject>& MessageObject, const FString& Category)
{
    if (!MessageObject.IsValid())
    {
        return;
    }
    // reasoning_content: DeepSeek, llama.cpp, LM Studio; reasoning: vLLM, LM Studio gpt-oss, OpenRouter; thinking: Ollama
    for (const TCHAR* Field : { TEXT("reasoning_content"), TEXT("reasoning"), TEXT("thinking") })
    {
        FString Reasoning;
        if (MessageObject->TryGetStringField(Field, Reasoning) && !Reasoning.IsEmpty())
        {
            FN2CLogger::Get().Log(FString::Printf(TEXT("The model returned %d characters of %s (logged only, not used as the answer)"),
                Reasoning.Len(), Field), EN2CLogSeverity::Debug, Category);
        }
    }
}

bool UN2CResponseParserBase::ExtractChatCompletionContent(
    const TSharedPtr<FJsonObject>& JsonObject,
    const FString& SettingHint,
    FString& OutContent,
    FN2CTranslationResponse& OutResponse)
{
    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    if (!JsonObject.IsValid() || !JsonObject->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        SetParseError(OutResponse, TEXT("The provider response has no choices"));
        return false;
    }
    const TSharedPtr<FJsonObject>* ChoicePtr = nullptr;
    if (!(*Choices)[0].IsValid() || !(*Choices)[0]->TryGetObject(ChoicePtr) || !ChoicePtr || !ChoicePtr->IsValid())
    {
        SetParseError(OutResponse, TEXT("The provider response's first choice is not an object"));
        return false;
    }
    const TSharedPtr<FJsonObject>& Choice = *ChoicePtr;

    // finish_reason is null while streaming and on some servers; a missing one is not an error
    FString FinishReason;
    Choice->TryGetStringField(TEXT("finish_reason"), FinishReason);

    TSharedPtr<FJsonObject> Message;
    const TSharedPtr<FJsonObject>* MessagePtr = nullptr;
    if (Choice->TryGetObjectField(TEXT("message"), MessagePtr) && MessagePtr)
    {
        Message = *MessagePtr;
    }
    LogReasoningFields(Message, TEXT("ResponseParser"));

    // Stop reason first: an answer cut at the limit or filtered is reported as such, not as a parse failure
    const EN2CTranslationErrorKind FinishKind = KindFromFinishReason(FinishReason);
    if (FinishKind == EN2CTranslationErrorKind::Truncated)
    {
        SetStopReasonError(OutResponse, FinishKind, FinishReason,
            MakeTruncatedMessage(FString::Printf(TEXT("finish_reason \"%s\""), *FinishReason), SettingHint));
        return false;
    }
    if (FinishKind == EN2CTranslationErrorKind::Blocked)
    {
        SetStopReasonError(OutResponse, FinishKind, FinishReason, FString::Printf(
            TEXT("The provider's content filter removed the answer (finish_reason \"%s\")"), *FinishReason));
        return false;
    }
    if (FinishKind != EN2CTranslationErrorKind::None)
    {
        SetStopReasonError(OutResponse, FinishKind, FinishReason, FString::Printf(
            TEXT("The provider stopped generating before the answer was complete (finish_reason \"%s\")"), *FinishReason));
        return false;
    }

    // OpenAI puts a model refusal in message.refusal (a string, null otherwise) with no content
    FString Refusal;
    if (Message.IsValid() && Message->TryGetStringField(TEXT("refusal"), Refusal) && !Refusal.TrimStartAndEnd().IsEmpty())
    {
        SetStopReasonError(OutResponse, EN2CTranslationErrorKind::Refused, TEXT("refusal"),
            FString::Printf(TEXT("The model refused the request: %s"), *Refusal.TrimStartAndEnd()));
        return false;
    }

    // content is a string, or null (tool calls, refusals, some reasoning servers), or missing
    FString RawContent;
    if (!Message.IsValid() || !Message->TryGetStringField(TEXT("content"), RawContent) || RawContent.TrimStartAndEnd().IsEmpty())
    {
        const bool bHasReasoning = Message.IsValid()
            && (Message->HasTypedField<EJson::String>(TEXT("reasoning_content")) || Message->HasTypedField<EJson::String>(TEXT("reasoning")));
        SetParseError(OutResponse, FString::Printf(TEXT("The model returned no answer text (finish_reason \"%s\")%s"),
            FinishReason.IsEmpty() ? TEXT("none") : *FinishReason,
            bHasReasoning ? TEXT("; it returned only reasoning text, which is not used as the answer") : TEXT("")));
        FN2CLogger::Get().LogError(OutResponse.Error.Message, TEXT("ResponseParser"));
        return false;
    }

    if (ProcessJsonContentWithMarkers(RawContent))
    {
        FN2CLogger::Get().Log(TEXT("Stripped JSON markers from response"), EN2CLogSeverity::Debug);
    }
    OutContent = RawContent;
    return true;
}

bool UN2CResponseParserBase::LooksCutOff(const FString& Json)
{
    int32 Depth = 0;
    bool bInString = false;
    bool bEscaped = false;
    for (const TCHAR Char : Json)
    {
        if (bInString)
        {
            if (bEscaped)
            {
                bEscaped = false;
            }
            else if (Char == TEXT('\\'))
            {
                bEscaped = true;
            }
            else if (Char == TEXT('"'))
            {
                bInString = false;
            }
            continue;
        }
        if (Char == TEXT('"'))
        {
            bInString = true;
        }
        else if (Char == TEXT('{') || Char == TEXT('['))
        {
            ++Depth;
        }
        else if (Char == TEXT('}') || Char == TEXT(']'))
        {
            --Depth;
        }
    }
    return bInString || Depth > 0;
}
