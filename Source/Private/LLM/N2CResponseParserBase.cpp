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
        return false;
    }

    // Check for truncated JSON by looking for unbalanced braces
    int32 OpenBraces = 0;
    int32 CloseBraces = 0;
    for (const TCHAR& Char : InJson)
    {
        if (Char == '{') OpenBraces++;
        else if (Char == '}') CloseBraces++;
    }

    if (OpenBraces != CloseBraces)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Potentially truncated or malformed JSON response. Open braces: %d, Close braces: %d"),
                OpenBraces, CloseBraces),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Try primary parse first
    FN2CTranslationResponse PrimaryResponse;
    if (TryParseJson(InJson, PrimaryResponse))
    {
        OutResponse = PrimaryResponse;
        return true;
    }

    // Primary parse failed — try truncation recovery for partially-received responses
    FN2CLogger::Get().LogWarning(
        TEXT("Primary JSON parse failed — attempting truncation recovery"),
        TEXT("ResponseParser")
    );

    FN2CTranslationResponse RecoveredResponse;
    if (TryRecoverTruncatedJson(InJson, RecoveredResponse))
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Truncation recovery succeeded: recovered %d graph(s)"),
                RecoveredResponse.Graphs.Num()),
            TEXT("ResponseParser")
        );
        OutResponse = RecoveredResponse;
        return true;
    }

    // All recovery attempts failed
    FN2CLogger::Get().LogError(
        FString::Printf(TEXT("Failed to parse JSON response: all recovery attempts failed")),
        TEXT("ResponseParser")
    );
    return false;
}

bool UN2CResponseParserBase::TryParseJson(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
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
        return false;
    }

    if (!bParseSuccess)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to parse JSON response: %s"),
                *Reader->GetErrorMessage()),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Validate basic structure
    if (!ValidateResponseFormat(JsonObject))
    {
        FN2CLogger::Get().LogError(TEXT("Invalid response format"), TEXT("ResponseParser"));
        return false;
    }

    // Get graphs array
    const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
    if (!JsonObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
    {
        FN2CLogger::Get().LogError(TEXT("Missing 'graphs' array in response"), TEXT("ResponseParser"));
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

    return OutResponse.Graphs.Num() > 0;
}

int32 UN2CResponseParserBase::FindTruncationPointWithStrings(const FString& InJson) const
{
    // Advanced recovery: track brace depth AND string state to find valid truncation points
    // even when JSON is mid-string-literal at the cut boundary.
    //
    // Key insight: MiniMax truncates at byte/output limit. If it cuts mid-string,
    // we get something like: "...TargetActorLocationAndRotator" (unterminated string)
    // The last structurally complete point might be BEFORE the unterminated string started.

    int32 BraceDepth = 0;
    bool bInString = false;
    TCHAR LastChar = 0;
    int32 LastBalancedPos = INDEX_NONE;
    int32 LastSafePos = INDEX_NONE; // Position after a comma, suitable for array/object boundary

    for (int32 i = 0; i < InJson.Len(); ++i)
    {
        TCHAR c = InJson[i];

        if (!bInString)
        {
            if (c == '"')
            {
                bInString = true;
            }
            else if (c == '{')
            {
                BraceDepth++;
            }
            else if (c == '}')
            {
                BraceDepth--;
                if (BraceDepth == 0 && i > InJson.Len() / 2)
                {
                    // Check if followed by reasonable JSON terminator
                    if (i + 1 >= InJson.Len() || InJson[i+1] == ',' || InJson[i+1] == ']' || FChar::IsWhitespace(InJson[i+1]))
                    {
                        LastBalancedPos = i + 1;
                    }
                }
            }
            else if (c == ',' && BraceDepth == 1)
            {
                // At depth 1 (inside the graphs array), a comma is a safe split point
                LastSafePos = i + 1;
            }
        }
        else // bInString
        {
            if (c == '"')
            {
                // Check for escaped quote
                if (LastChar != '\\')
                {
                    bInString = false;
                }
            }
            else if (c == '\\')
            {
                // Skip next char (escaped character)
                i++;
                if (i < InJson.Len())
                    LastChar = InJson[i];
                continue;
            }
        }

        LastChar = c;
    }

    // Prefer the last balanced position if we have one
    if (LastBalancedPos != INDEX_NONE && LastBalancedPos > InJson.Len() / 2)
    {
        return LastBalancedPos;
    }

    // Fallback: use the last safe comma position if it's past 60% of content
    if (LastSafePos != INDEX_NONE && LastSafePos > InJson.Len() * 0.6)
    {
        // The comma is inside an object/array - truncate there and close the structure
        // We'll let the balanced-brace logic handle the closing
        return LastSafePos;
    }

    return INDEX_NONE;
}

bool UN2CResponseParserBase::TryRecoverTruncatedJson(
    const FString& InJson,
    FN2CTranslationResponse& OutResponse)
{
    // Recovery strategy: truncate at the last complete "graphs" array entry
    // that has both a valid graph_name and graphImplementation.
    // This handles the case where MiniMax truncates mid-JSON without
    // any final closing braces being malformed.

    // Find the last occurrence of a closing brace followed by nothing useful
    // Strategy: look for "}]" or "}]}" patterns which indicate end of graphs array
    int32 TruncationPoint = INDEX_NONE;

    // Try advanced string-aware truncation detection first
    TruncationPoint = FindTruncationPointWithStrings(InJson);

    // Search backward for last "}]" pattern in last 80% of content
    for (int32 i = InJson.Len() - 1; i >= InJson.Len() * 0.2; --i)
    {
        if (InJson[i] == '}' && InJson[i-1] == ']' && InJson[i-2] == '\"')
        {
            // Found "]}" — this is likely the end of the graphs array
            TruncationPoint = i + 1;
            break;
        }
    }

    // Fallback: find the last position where braces are balanced (tracking strings)
    if (TruncationPoint == INDEX_NONE)
    {
        int32 BraceCount = 0;
        bool bInString = false;
        TCHAR LastChar = 0;
        for (int32 i = 0; i < InJson.Len(); ++i)
        {
            TCHAR c = InJson[i];
            if (!bInString)
            {
                if (c == '"') bInString = true;
                else if (c == '{') BraceCount++;
                else if (c == '}')
                {
                    BraceCount--;
                    if (BraceCount == 0 && i > InJson.Len() * 0.6)
                    {
                        if (i + 1 >= InJson.Len() || FChar::IsWhitespace(InJson[i+1]) || InJson[i+1] == '\n' || InJson[i+1] == '\r')
                        {
                            TruncationPoint = i + 1;
                            break;
                        }
                    }
                }
            }
            else
            {
                if (c == '"' && LastChar != '\\') bInString = false;
            }
            LastChar = c;
        }
    }

    if (TruncationPoint == INDEX_NONE || TruncationPoint < InJson.Len() * 0.6)
    {
        // Not enough content to recover
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Truncation recovery: no valid truncation point found (len=%d, lastGood=%d)"),
                InJson.Len(), TruncationPoint),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Try to parse from the truncated content
    FString TruncatedJson = InJson.Left(TruncationPoint);

    // Verify the truncated JSON has balanced braces AND balanced string quotes
    int32 Open = 0, Close = 0;
    int32 StringQuotes = 0;
    bool bInString = false;
    TCHAR LastChar = 0;
    for (const TCHAR& c : TruncatedJson)
    {
        if (!bInString)
        {
            if (c == '"')
            {
                StringQuotes++;
                bInString = true;
            }
            else if (c == '{') Open++;
            else if (c == '}') Close++;
        }
        else // in string
        {
            if (c == '"' && LastChar != '\\')
            {
                StringQuotes++;
                bInString = false;
            }
            else if (c == '\\')
            {
                // Skip next char (escaped)
            }
        }
        LastChar = c;
    }

    // Check for unbalanced structure
    if (Open != Close)
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Truncation recovery: truncated content still unbalanced (%d/%d)"),
                Open, Close),
            TEXT("ResponseParser")
        );
        return false;
    }

    // Check for unclosed string (odd quote count means we're inside a string)
    if (StringQuotes % 2 != 0)
    {
        // Try to fix by appending a closing quote and brace
        FString FixedJson = TruncatedJson;
        FixedJson.Append(TEXT("\"}}"));
        TruncatedJson = FixedJson;
        FN2CLogger::Get().LogWarning(
            TEXT("Truncation recovery: fixing unterminated string by appending closing quotes and braces"),
            TEXT("ResponseParser")
        );
    }

    // Try to parse the truncated JSON
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TruncatedJson);

    bool bParseSuccess = false;
    try
    {
        bParseSuccess = FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid();
    }
    catch (...)
    {
        return false;
    }

    if (!bParseSuccess || !JsonObject.IsValid())
    {
        return false;
    }

    if (!ValidateResponseFormat(JsonObject))
    {
        return false;
    }

    // Get graphs array
    const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
    if (!JsonObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
    {
        return false;
    }

    // Process each graph
    OutResponse.Graphs.Empty();
    int32 RecoveredCount = 0;
    for (const auto& GraphValue : *GraphsArray)
    {
        TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
        if (!GraphObject.IsValid())
        {
            continue;
        }

        FN2CGraphTranslation Graph;
        ExtractGraphData(GraphObject, Graph);

        // Only count graphs with actual implementation
        if (!Graph.GraphName.IsEmpty())
        {
            OutResponse.Graphs.Add(Graph);
            RecoveredCount++;
        }
    }

    if (RecoveredCount > 0)
    {
        FN2CLogger::Get().LogWarning(
            FString::Printf(TEXT("Truncation recovery: saved %d graph(s) from %d chars truncated to %d"),
                RecoveredCount, InJson.Len(), TruncationPoint),
            TEXT("ResponseParser")
        );
        return true;
    }

    return false;
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
    FString& OutErrorMessage)
{
    const TSharedPtr<FJsonObject> ErrorObject = JsonObject->GetObjectField(ErrorFieldName);
    if (!ErrorObject.IsValid())
    {
        OutErrorMessage = FString::Printf(TEXT("Unknown error in %s field"), *ErrorFieldName);
        return true;
    }

    FString ErrorType, ErrorMessage;
    ErrorObject->TryGetStringField(TEXT("type"), ErrorType);
    ErrorObject->TryGetStringField(TEXT("message"), ErrorMessage);

    if (ErrorType.Contains(TEXT("rate_limit")))
    {
        OutErrorMessage = TEXT("API rate limit exceeded");
    }
    else if (ErrorType.Contains(TEXT("invalid_request")) || ErrorType.Contains(TEXT("invalid_request_error")))
    {
        OutErrorMessage = FString::Printf(TEXT("Invalid request: %s"), *ErrorMessage);
    }
    else if (ErrorType.Contains(TEXT("authentication")))
    {
        OutErrorMessage = TEXT("API authentication failed");
    }
    else
    {
        OutErrorMessage = FString::Printf(TEXT("API error: %s - %s"), *ErrorType, *ErrorMessage);
    }

    return true;
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
        MessageObject = ItemObject->GetObjectField(MessageObjName);
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
