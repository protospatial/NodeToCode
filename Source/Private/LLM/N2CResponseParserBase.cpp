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
