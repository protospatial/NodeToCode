// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpFindNodesInGraphTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSerializer.h"
#include "Utils/N2CLogger.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "K2Node.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpFindNodesInGraphTool)

FMcpToolDefinition FN2CMcpFindNodesInGraphTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("find-nodes-in-graph"),
        TEXT("Searches for specific nodes in the focused Blueprint graph by keywords or node GUIDs. ")
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
    MaxResultsObj->SetNumberField(TEXT("default"), 10);
    MaxResultsObj->SetNumberField(TEXT("minimum"), 1);
    MaxResultsObj->SetNumberField(TEXT("maximum"), 200);
    
    Definition.InputSchema = Schema;
    
    // Add read-only annotation
    AddReadOnlyAnnotation(Definition);
    
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
        
        // Get the focused graph
        UBlueprint* OwningBlueprint = nullptr;
        UEdGraph* FocusedGraph = nullptr;
        FString GraphError;
        if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("find-nodes-in-graph tool failed: %s"), *GraphError));
            return FMcpToolCallResult::CreateErrorResult(GraphError);
        }
        
        // Collect all nodes from the graph
        TArray<UK2Node*> AllNodes;
        if (!FN2CEditorIntegration::Get().CollectNodesFromGraph(FocusedGraph, AllNodes))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to collect nodes from the focused graph."));
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
            return FMcpToolCallResult::CreateTextResult(TEXT("{\"nodes\":[],\"metadata\":{\"totalFound\":0}}"));
        }
        
        // Process ALL nodes to get complete ID maps (for GUID mappings)
        FN2CBlueprint TempBlueprint;
        TMap<FGuid, FString> NodeIDMap;
        TMap<FGuid, FString> PinIDMap;
        
        // We need to translate all nodes to get proper ID mappings, but we'll only return the matching ones
        if (!FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprintWithMaps(AllNodes, TempBlueprint, NodeIDMap, PinIDMap))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to translate nodes for ID mapping."));
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
        
        // Process each matching node individually to avoid flow validation issues
        TArray<TSharedPtr<FJsonValue>> NodesArray;
        for (int32 i = 0; i < MatchingNodes.Num(); ++i)
        {
            UK2Node* Node = MatchingNodes[i];
            // Use the actual node ID from the full translation to maintain consistency
            FString* ActualNodeId = NodeIDMap.Find(Node->NodeGuid);
            FString NodeId = ActualNodeId ? *ActualNodeId : FString::Printf(TEXT("Node_%d"), i + 1);
            
            TSharedPtr<FJsonObject> NodeJson = ConvertNodeToEnhancedJson(TempBlueprint, Node, NodeId, NodeIDMap, PinIDMap);
            if (NodeJson.IsValid())
            {
                NodesArray.Add(MakeShareable(new FJsonValueObject(NodeJson)));
            }
        }
        
        ResultObject->SetArrayField(TEXT("nodes"), NodesArray);
        
        // Serialize to JSON string
        FString ResultJsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJsonString);
        FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("find-nodes-in-graph tool found %d matching nodes"), 
            MatchingNodes.Num()), EN2CLogSeverity::Info);
        
        return FMcpToolCallResult::CreateTextResult(ResultJsonString);
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
        // Get searchable text from the node
        FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
        FString NodeClass = Node->GetClass()->GetName();
        FString MenuCategory = Node->GetMenuCategory().ToString();
        
        // Combine searchable text
        FString SearchableText = FString::Printf(TEXT("%s %s %s"), *NodeTitle, *NodeClass, *MenuCategory);
        
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

TSharedPtr<FJsonObject> FN2CMcpFindNodesInGraphTool::ConvertNodeToEnhancedJson(
    const FN2CBlueprint& Captured,
    UK2Node* Node,
    const FString& NodeId,
    const TMap<FGuid, FString>& NodeIDMap,
    const TMap<FGuid, FString>& PinIDMap)
{
    if (!Node)
    {
        return nullptr;
    }
    
    // Instead of describing the node again on its own, 
    // let's extract the node from the full translation we already did
    
    // First, serialize the full blueprint structure that we already translated (this call's own capture)
    FString FullJsonString = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(Captured, false);
    
    // Parse the JSON to find our specific node
    TSharedPtr<FJsonObject> FullJsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FullJsonString);
    
    if (!FJsonSerializer::Deserialize(Reader, FullJsonObject) || !FullJsonObject.IsValid())
    {
        FN2CLogger::Get().LogWarning(TEXT("Failed to parse full blueprint JSON"));
        return nullptr;
    }
    
    // Find the node in the translated data
    const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
    if (FullJsonObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
    {
        for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
        {
            TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
            if (!GraphObject.IsValid()) continue;
            
            const TArray<TSharedPtr<FJsonValue>>* NodesArray;
            if (GraphObject->TryGetArrayField(TEXT("nodes"), NodesArray))
            {
                for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
                {
                    TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
                    if (!NodeObject.IsValid()) continue;
                    
                    // Check if this is our node by ID
                    FString CurrentNodeId;
                    if (NodeObject->TryGetStringField(TEXT("id"), CurrentNodeId) && CurrentNodeId == NodeId)
                    {
                        // Found our node! Create a copy to return
                        TSharedPtr<FJsonObject> NodeCopy = MakeShareable(new FJsonObject(*NodeObject));
                        
                        // Enhance with GUIDs
                        EnhanceNodeWithGuids(NodeCopy, NodeId, Node->NodeGuid, NodeIDMap, PinIDMap);
                        
                        return NodeCopy;
                    }
                }
            }
        }
    }
    
    FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Could not find node %s in translated data"), *NodeId));
    return nullptr;
}

void FN2CMcpFindNodesInGraphTool::EnhanceNodeWithGuids(
    TSharedPtr<FJsonObject> NodeObject,
    const FString& ShortNodeId,
    const FGuid& NodeGuid,
    const TMap<FGuid, FString>& NodeIDMap,
    const TMap<FGuid, FString>& PinIDMap)
{
    if (!NodeObject.IsValid())
    {
        return;
    }
    
    // Create reverse map for pin lookups only
    TMap<FString, FGuid> ReversePinIDMap;
    for (const auto& Pair : PinIDMap)
    {
        ReversePinIDMap.Add(Pair.Value, Pair.Key);
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
                    if (FGuid* PinGuid = ReversePinIDMap.Find(ShortPinID))
                    {
                        PinIdsObject->SetStringField(TEXT("guid"), PinGuid->ToString(EGuidFormats::DigitsWithHyphens));
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