// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpFindNodesInGraphTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSerializer.h"
#include "Utils/N2CLogger.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "K2Node.h"
#include "Utils/N2CStableNames.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpFindNodesInGraphTool)

FMcpToolDefinition FN2CMcpFindNodesInGraphTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("find-nodes-in-graph"),
        TEXT("Searches for specific nodes in a Blueprint graph (the one blueprintPath plus graphName or graphGuid name, else the focused graph) by keywords or node GUIDs. ")
        TEXT("Returns matching nodes in N2C JSON format with full node and pin GUID information.")
    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("searchTerms"), TEXT("array"));
    Properties.Add(TEXT("searchType"), TEXT("string"));
    Properties.Add(TEXT("caseSensitive"), TEXT("boolean"));
    Properties.Add(TEXT("maxResults"), TEXT("number"));
    
    TArray<FString> Required;
    Required.Add(TEXT("searchTerms"));
    
    TSharedPtr<FJsonObject> Schema = BuildInputSchema(Properties, Required);
    
    // Add array items schema for searchTerms
    TSharedPtr<FJsonObject> SearchTermsArray = Schema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("searchTerms"));
    TSharedPtr<FJsonObject> ItemsSchema = MakeShareable(new FJsonObject);
    ItemsSchema->SetStringField(TEXT("type"), TEXT("string"));
    SearchTermsArray->SetObjectField(TEXT("items"), ItemsSchema);
    SearchTermsArray->SetStringField(TEXT("description"), TEXT("Array of keywords or GUIDs to search for"));
    
    // Add descriptions and constraints for other properties
    TSharedPtr<FJsonObject> SearchTypeObj = Schema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("searchType"));
    SearchTypeObj->SetStringField(TEXT("description"), TEXT("Type of search: 'keyword' or 'guid'"));
    SearchTypeObj->SetStringField(TEXT("default"), TEXT("keyword"));
    TArray<TSharedPtr<FJsonValue>> EnumValues;
    EnumValues.Add(MakeShareable(new FJsonValueString(TEXT("keyword"))));
    EnumValues.Add(MakeShareable(new FJsonValueString(TEXT("guid"))));
    SearchTypeObj->SetArrayField(TEXT("enum"), EnumValues);
    
    TSharedPtr<FJsonObject> CaseSensitiveObj = Schema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("caseSensitive"));
    CaseSensitiveObj->SetStringField(TEXT("description"), TEXT("Whether keyword search is case-sensitive"));
    CaseSensitiveObj->SetBoolField(TEXT("default"), false);
    
    TSharedPtr<FJsonObject> MaxResultsObj = Schema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("maxResults"));
    MaxResultsObj->SetStringField(TEXT("description"), TEXT("Maximum number of nodes to return"));
    MaxResultsObj->SetNumberField(TEXT("default"), 50);
    MaxResultsObj->SetNumberField(TEXT("minimum"), 1);
    MaxResultsObj->SetNumberField(TEXT("maximum"), 200);
    
    Definition.InputSchema = Schema;
    
    // Add read-only annotation
    AddReadOnlyAnnotation(Definition);
    
    // Results carry Blueprint text (node titles and pin default values): marked as untrusted data (card 0124)
    Definition.bReturnsUntrustedContent = true;

    FN2CGraphTarget::AddSchemaProperties(Definition);

    return Definition;
}

FMcpToolCallResult FN2CMcpFindNodesInGraphTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        TArray<FString> SearchTerms;
        FString SearchType;
        bool bCaseSensitive;
        int32 MaxResults;
        FString ParseError;
        
        if (!ParseArguments(Arguments, SearchTerms, SearchType, bCaseSensitive, MaxResults, ParseError))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }
        
        // The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0152); every result
        // after this says which graph was used ('target')
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ false);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }
        UBlueprint* OwningBlueprint = Target.Blueprint;
        UEdGraph* FocusedGraph = Target.Graph;
        
        // Collect all nodes from the graph
        TArray<UK2Node*> AllNodes;
        if (!FN2CEditorIntegration::Get().CollectNodesFromGraph(FocusedGraph, AllNodes))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to collect nodes from the focused graph.")));
        }
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("FindNodesInGraph: Searching through %d nodes for %d search terms (type: %s)"), 
            AllNodes.Num(), SearchTerms.Num(), *SearchType), EN2CLogSeverity::Info);
        
        // Find matching nodes
        TArray<UK2Node*> MatchingNodes;
        for (UK2Node* Node : AllNodes)
        {
            if (DoesNodeMatchSearch(Node, SearchTerms, SearchType, bCaseSensitive))
            {
                MatchingNodes.Add(Node);
                if (MatchingNodes.Num() >= MaxResults)
                {
                    break;
                }
            }
        }
        
        if (MatchingNodes.IsEmpty())
        {
            return Target.Finish(FMcpToolCallResult::CreateTextResult(TEXT("{\"nodes\":[],\"metadata\":{\"totalFound\":0}}")));
        }
        
        // Capture ALL nodes so the IDs are the ones get-focused-blueprint gives, but return only the matching ones
        FN2CCapture Capture;
        // The default events the editor placed stay visible (marked disabled), so an agent wires them
        // instead of adding a duplicate event (card 0137)
        FN2CCaptureOptions Options;
        Options.bIncludeGhostNodes = true;
        if (!FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprintWithMaps(AllNodes, Capture, Options))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to translate nodes for ID mapping.")));
        }
        
        // Build the result JSON
        TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
        
        // Add metadata
        TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
        MetadataObject->SetStringField(TEXT("blueprintName"), OwningBlueprint->GetName());
        MetadataObject->SetStringField(TEXT("graphName"), FocusedGraph->GetName());
        MetadataObject->SetNumberField(TEXT("totalFound"), MatchingNodes.Num());
        MetadataObject->SetNumberField(TEXT("totalInGraph"), AllNodes.Num());
        ResultObject->SetObjectField(TEXT("metadata"), MetadataObject);
        
        // The capture is serialized and parsed once per call, and each match is looked up by its ID
        // (imp-mcp-tools-graph-functions-12: it was serialized and parsed again for every match)
        const TMap<FString, TSharedPtr<FJsonObject>> NodeJsonById = IndexNodesById(Capture);
        TArray<TSharedPtr<FJsonValue>> NodesArray;
        for (UK2Node* Node : MatchingNodes)
        {
            const FString* NodeId = Capture.FindNodeID(Node->NodeGuid);
            const TSharedPtr<FJsonObject>* NodeJson = NodeId ? NodeJsonById.Find(*NodeId) : nullptr;
            if (!NodeJson)
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Could not find node %s in the capture"), *Node->GetName()));
                continue;
            }
            // A copy, so the index is never changed
            const TSharedPtr<FJsonObject> NodeCopy = MakeShared<FJsonObject>(**NodeJson);
            EnhanceNodeWithGuids(NodeCopy, Node->NodeGuid, Capture);
            NodesArray.Add(MakeShared<FJsonValueObject>(NodeCopy));
        }
        
        ResultObject->SetArrayField(TEXT("nodes"), NodesArray);
        
        // Serialize to JSON string
        FString ResultJsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJsonString);
        FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("find-nodes-in-graph tool found %d matching nodes"), 
            MatchingNodes.Num()), EN2CLogSeverity::Info);
        
        return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultJsonString));
    });
}

bool FN2CMcpFindNodesInGraphTool::ParseArguments(
    const TSharedPtr<FJsonObject>& Arguments,
    TArray<FString>& OutSearchTerms,
    FString& OutSearchType,
    bool& OutCaseSensitive,
    int32& OutMaxResults,
    FString& OutError)
{
    FN2CMcpArgumentParser ArgParser(Arguments);
    
    // Get search terms array
    const TArray<TSharedPtr<FJsonValue>>* SearchTermsArray;
    if (!Arguments->TryGetArrayField(TEXT("searchTerms"), SearchTermsArray) || SearchTermsArray->IsEmpty())
    {
        OutError = TEXT("searchTerms array is required and must not be empty");
        return false;
    }
    
    // Extract search terms
    for (const TSharedPtr<FJsonValue>& Value : *SearchTermsArray)
    {
        FString Term;
        if (Value->TryGetString(Term) && !Term.IsEmpty())
        {
            OutSearchTerms.Add(Term);
        }
    }
    
    if (OutSearchTerms.IsEmpty())
    {
        OutError = TEXT("searchTerms array must contain at least one non-empty string");
        return false;
    }
    
    // Get other parameters
    OutSearchType = ArgParser.GetOptionalString(TEXT("searchType"), TEXT("keyword"));
    if (OutSearchType != TEXT("keyword") && OutSearchType != TEXT("guid"))
    {
        OutError = TEXT("searchType must be either 'keyword' or 'guid'");
        return false;
    }
    
    OutCaseSensitive = ArgParser.GetOptionalBool(TEXT("caseSensitive"), false);
    OutMaxResults = ArgParser.GetOptionalInt(TEXT("maxResults"), 50);
    OutMaxResults = FMath::Clamp(OutMaxResults, 1, 200);
    
    return true;
}

bool FN2CMcpFindNodesInGraphTool::DoesNodeMatchSearch(
    UK2Node* Node,
    const TArray<FString>& SearchTerms,
    const FString& SearchType,
    bool bCaseSensitive)
{
    if (!Node)
    {
        return false;
    }
    
    if (SearchType == TEXT("guid"))
    {
        // GUID search - check if the node's GUID matches any search term
        FString NodeGuidString = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
        FString NodeGuidStringNoHyphens = Node->NodeGuid.ToString(EGuidFormats::Digits);
        
        for (const FString& Term : SearchTerms)
        {
            // Support both with and without hyphens
            if (NodeGuidString.Equals(Term, ESearchCase::IgnoreCase) || 
                NodeGuidStringNoHyphens.Equals(Term, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }
    }
    else // keyword search
    {
        // Get searchable text from the node: its title as the editor shows it and in source (English) text, its class
        // and the function or variable it names (N2CStableNames::NodeSearchText), so the names get-focused-blueprint
        // returns ("class", "member_name", "title") match in every editor language (card 0138); and its menu category
        FString SearchableText = N2CStableNames::NodeSearchText(Node) + TEXT(" ") + Node->GetMenuCategory().ToString();
        
        if (!bCaseSensitive)
        {
            SearchableText = SearchableText.ToLower();
        }
        
        // Check if any search term matches
        for (const FString& Term : SearchTerms)
        {
            FString SearchTerm = bCaseSensitive ? Term : Term.ToLower();
            if (SearchableText.Contains(SearchTerm))
            {
                return true;
            }
        }
    }
    
    return false;
}

TMap<FString, TSharedPtr<FJsonObject>> FN2CMcpFindNodesInGraphTool::IndexNodesById(const FN2CCapture& Capture)
{
    TMap<FString, TSharedPtr<FJsonObject>> Index;
    TSharedPtr<FJsonObject> FullJsonObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FN2CSerializer::ToJson(Capture.Blueprint, EN2CJsonStyle::Condensed));
    if (!FJsonSerializer::Deserialize(Reader, FullJsonObject) || !FullJsonObject.IsValid())
    {
        FN2CLogger::Get().LogWarning(TEXT("Failed to parse full blueprint JSON"));
        return Index;
    }

    const TArray<TSharedPtr<FJsonValue>>* GraphsArray = nullptr;
    if (!FullJsonObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
    {
        return Index;
    }
    for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
    {
        const TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
        if (!GraphObject.IsValid() || !GraphObject->TryGetArrayField(TEXT("nodes"), NodesArray))
        {
            continue;
        }
        for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
        {
            const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
            FString NodeId;
            if (NodeObject.IsValid() && NodeObject->TryGetStringField(TEXT("id"), NodeId))
            {
                // IDs are unique across the capture
                Index.Add(NodeId, NodeObject);
            }
        }
    }
    return Index;
}

void FN2CMcpFindNodesInGraphTool::EnhanceNodeWithGuids(
    TSharedPtr<FJsonObject> NodeObject,
    const FGuid& NodeGuid,
    const FN2CCapture& Capture)
{
    if (!NodeObject.IsValid())
    {
        return;
    }

    // Enhance node ID structure
    FString CurrentNodeId;
    if (NodeObject->TryGetStringField(TEXT("id"), CurrentNodeId))
    {
        // Create the nested ID structure
        TSharedPtr<FJsonObject> IdsObject = MakeShareable(new FJsonObject);
        IdsObject->SetStringField(TEXT("short"), CurrentNodeId);
        
        // Use the provided node GUID directly
        IdsObject->SetStringField(TEXT("guid"), NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
        
        // Replace the simple ID with the nested structure
        NodeObject->RemoveField(TEXT("id"));
        NodeObject->SetObjectField(TEXT("ids"), IdsObject);
    }
    
    // Process pins
    TArray<FString> PinArrayNames = { TEXT("input_pins"), TEXT("output_pins") };
    for (const FString& PinArrayName : PinArrayNames)
    {
        const TArray<TSharedPtr<FJsonValue>>* PinsArray;
        if (NodeObject->TryGetArrayField(PinArrayName, PinsArray))
        {
            TArray<TSharedPtr<FJsonValue>> EnhancedPins;
            
            for (const TSharedPtr<FJsonValue>& PinValue : *PinsArray)
            {
                TSharedPtr<FJsonObject> PinObject = PinValue->AsObject();
                if (!PinObject.IsValid()) continue;
                
                // Get the short pin ID
                FString ShortPinID;
                if (PinObject->TryGetStringField(TEXT("id"), ShortPinID))
                {
                    // Create the nested ID structure for pin
                    TSharedPtr<FJsonObject> PinIdsObject = MakeShareable(new FJsonObject);
                    PinIdsObject->SetStringField(TEXT("short"), ShortPinID);
                    
                    // Find and add the GUID if available
                    if (const FN2CCapturedPin* Pin = Capture.Pins.Find(ShortPinID))
                    {
                        PinIdsObject->SetStringField(TEXT("guid"), Pin->PinGuid.ToString(EGuidFormats::DigitsWithHyphens));
                    }
                    
                    // Add pin name as fallback identifier
                    FString PinName;
                    if (PinObject->TryGetStringField(TEXT("name"), PinName))
                    {
                        PinIdsObject->SetStringField(TEXT("name"), PinName);
                    }
                    
                    // Replace the simple ID with the nested structure
                    PinObject->RemoveField(TEXT("id"));
                    PinObject->SetObjectField(TEXT("ids"), PinIdsObject);
                }
                
                EnhancedPins.Add(PinValue);
            }
            
            // Replace the pin array with the enhanced version
            NodeObject->SetArrayField(PinArrayName, EnhancedPins);
        }
    }
}