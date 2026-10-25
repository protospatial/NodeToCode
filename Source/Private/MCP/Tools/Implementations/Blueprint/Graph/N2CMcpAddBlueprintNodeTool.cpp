// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpAddBlueprintNodeTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Utils/N2CLogger.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/KismetEditorUtilities.h"

// Register the tool
REGISTER_MCP_TOOL(FN2CMcpAddBlueprintNodeTool)

FN2CMcpAddBlueprintNodeTool::FN2CMcpAddBlueprintNodeTool()
{
}

FMcpToolDefinition FN2CMcpAddBlueprintNodeTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("add-bp-node-to-active-graph"),
        TEXT("Adds a Blueprint node to the currently active graph. IMPORTANT: The search-blueprint-nodes tool MUST have been used before this tool in order to find the node and get its actionIdentifier from the spawnMetadata alongside its Name. The node's Name and actionIdentifier are required to spawn the exact correct variant of the node. NEXT STEP: After adding a node, use the 'connect-pins' tool to wire the node's pins to other nodes using the pinGuid values returned in inputPins and outputPins arrays."),
        TEXT("Blueprint Graph Editing")
    );
    
    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // nodeName property (required)
    TSharedPtr<FJsonObject> NodeNameProp = MakeShareable(new FJsonObject);
    NodeNameProp->SetStringField(TEXT("type"), TEXT("string"));
    NodeNameProp->SetStringField(TEXT("description"), TEXT("The name of the node to add (e.g., 'Spawn Actor from Class')"));
    Properties->SetObjectField(TEXT("nodeName"), NodeNameProp);
    
    // actionIdentifier property (required)
    TSharedPtr<FJsonObject> ActionIdProp = MakeShareable(new FJsonObject);
    ActionIdProp->SetStringField(TEXT("type"), TEXT("string"));
    ActionIdProp->SetStringField(TEXT("description"), TEXT("The unique action identifier obtained from the spawnMetadata.actionIdentifier field in search-blueprint-nodes results. This MUST be the exact value from the search results, including the carriage returns (\\r) before each '>' that it has on Windows."));
    Properties->SetObjectField(TEXT("actionIdentifier"), ActionIdProp);
    
    // location property (optional)
    TSharedPtr<FJsonObject> LocationProp = MakeShareable(new FJsonObject);
    LocationProp->SetStringField(TEXT("type"), TEXT("object"));
    LocationProp->SetStringField(TEXT("description"), TEXT("The location to spawn the node at"));
    
    TSharedPtr<FJsonObject> LocationProps = MakeShareable(new FJsonObject);
    
    TSharedPtr<FJsonObject> XProp = MakeShareable(new FJsonObject);
    XProp->SetStringField(TEXT("type"), TEXT("number"));
    XProp->SetNumberField(TEXT("default"), 0.0);
    LocationProps->SetObjectField(TEXT("x"), XProp);
    
    TSharedPtr<FJsonObject> YProp = MakeShareable(new FJsonObject);
    YProp->SetStringField(TEXT("type"), TEXT("number"));
    YProp->SetNumberField(TEXT("default"), 0.0);
    LocationProps->SetObjectField(TEXT("y"), YProp);
    
    LocationProp->SetObjectField(TEXT("properties"), LocationProps);
    Properties->SetObjectField(TEXT("location"), LocationProp);
    
    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("nodeName"))));
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("actionIdentifier"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    return Definition;
}

FMcpToolCallResult FN2CMcpAddBlueprintNodeTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // Through ExecuteOnGameThread like every Blueprint tool, so the node add is one tool call: the Blueprint compiles
    // once when it returns and the result carries the compiler's diagnostics (card 0151)
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FString NodeName;
        FString ActionIdentifier;
        FVector2D Location(0, 0);
        FString ParseError;
    
        if (!ParseArguments(Arguments, NodeName, ActionIdentifier, Location, ParseError))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }
    
        FN2CLogger::Get().Log(FString::Printf(TEXT("Adding Blueprint node: '%s' with identifier: '%s' at location (%.2f, %.2f)"),
            *NodeName, *ActionIdentifier, Location.X, Location.Y), EN2CLogSeverity::Info);
    
        // Get the active graph context
        FString ContextError;
        UBlueprint* ActiveBlueprint = nullptr;
        UEdGraph* ActiveGraph = nullptr;
        if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ActiveBlueprint, ActiveGraph, ContextError))
        {
            return FMcpToolCallResult::CreateErrorResult(ContextError);
        }
    
        // Find and spawn the node
        UEdGraphNode* SpawnedNode = nullptr;
        FString SpawnError;

        if (!FindAndSpawnNode(NodeName, ActionIdentifier, ActiveBlueprint, ActiveGraph, Location, SpawnedNode, SpawnError))
        {
            return FMcpToolCallResult::CreateErrorResult(SpawnError);
        }

        FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully added node '%s' to graph '%s'"),
            *NodeName, *ActiveGraph->GetName()), EN2CLogSeverity::Info);

        // Build result with pin information for immediate use
        TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
        ResultObject->SetBoolField(TEXT("success"), true);
        ResultObject->SetStringField(TEXT("nodeGuid"), SpawnedNode->NodeGuid.ToString());
        ResultObject->SetStringField(TEXT("nodeName"), SpawnedNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
        ResultObject->SetStringField(TEXT("graphName"), ActiveGraph->GetName());
        ResultObject->SetStringField(TEXT("blueprintName"), ActiveBlueprint->GetName());

        TSharedPtr<FJsonObject> LocationObject = MakeShareable(new FJsonObject);
        LocationObject->SetNumberField(TEXT("x"), SpawnedNode->NodePosX);
        LocationObject->SetNumberField(TEXT("y"), SpawnedNode->NodePosY);
        ResultObject->SetObjectField(TEXT("location"), LocationObject);

        // Add pin information so caller can immediately connect pins without additional lookup
        TArray<TSharedPtr<FJsonValue>> InputPinsArray;
        TArray<TSharedPtr<FJsonValue>> OutputPinsArray;

        for (UEdGraphPin* Pin : SpawnedNode->Pins)
        {
            if (!Pin || Pin->bHidden)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
            PinObj->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
            PinObj->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

            // Include default value for input pins
            if (Pin->Direction == EGPD_Input && !Pin->GetDefaultAsString().IsEmpty())
            {
                PinObj->SetStringField(TEXT("defaultValue"), Pin->GetDefaultAsString());
            }

            if (Pin->Direction == EGPD_Input)
            {
                InputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
            }
            else
            {
                OutputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
            }
        }

        ResultObject->SetArrayField(TEXT("inputPins"), InputPinsArray);
        ResultObject->SetArrayField(TEXT("outputPins"), OutputPinsArray);

        // Add guidance for the next step
        TSharedPtr<FJsonObject> NextStepObject = MakeShareable(new FJsonObject);
        NextStepObject->SetStringField(TEXT("tool"), TEXT("connect-pins"));
        NextStepObject->SetStringField(TEXT("description"), TEXT("Use the connect-pins tool with the nodeGuid and pinGuid values above to wire this node to other nodes in the graph."));
        ResultObject->SetObjectField(TEXT("nextStep"), NextStepObject);

        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultJson);
    });
}

bool FN2CMcpAddBlueprintNodeTool::ParseArguments(
    const TSharedPtr<FJsonObject>& Arguments,
    FString& OutNodeName,
    FString& OutActionIdentifier,
    FVector2D& OutLocation,
    FString& OutError)
{
    if (!Arguments.IsValid())
    {
        OutError = TEXT("Invalid arguments object");
        return false;
    }
    
    FN2CMcpArgumentParser ArgParser(Arguments);
    
    // Required: nodeName
    if (!ArgParser.TryGetRequiredString(TEXT("nodeName"), OutNodeName, OutError, false))
    {
        return false;
    }
    
    // Required: actionIdentifier
    if (!ArgParser.TryGetRequiredString(TEXT("actionIdentifier"), OutActionIdentifier, OutError, false))
    {
        return false;
    }
    
    // Optional: location
    TSharedPtr<FJsonObject> LocationObject = ArgParser.GetOptionalObject(TEXT("location"));
    if (LocationObject.IsValid())
    {
        FN2CMcpArgumentParser LocationParser(LocationObject);
        OutLocation.X = LocationParser.GetOptionalNumber(TEXT("x"), 0.0);
        OutLocation.Y = LocationParser.GetOptionalNumber(TEXT("y"), 0.0);
    }
    
    return true;
}

bool FN2CMcpAddBlueprintNodeTool::FindAndSpawnNode(
    const FString& NodeName,
    const FString& ActionIdentifier,
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FVector2D& Location,
    UEdGraphNode*& OutSpawnedNode,
    FString& OutError)
{
    // Build the action filter for the current context
    FBlueprintActionFilter Filter;
    Filter.Context.Blueprints.Add(Blueprint);
    Filter.Context.Graphs.Add(Graph);
    
    // Build the action list
    FBlueprintActionMenuBuilder MenuBuilder;
    MenuBuilder.AddMenuSection(Filter, FText::GetEmpty(), 0);
    MenuBuilder.RebuildActionList();
    
    // Tokenize node name for searching (same logic as search-blueprint-nodes)
    TArray<FString> FilterTerms;
    NodeName.ParseIntoArray(FilterTerms, TEXT(" "), true);
    
    // Also create case-insensitive versions
    TArray<FString> LowerFilterTerms;
    for (const FString& Term : FilterTerms)
    {
        LowerFilterTerms.Add(Term.ToLower());
    }
    
    // Search for actions matching the node name
    TArray<int32> MatchingActionIndices;
    
    for (int32 i = 0; i < MenuBuilder.GetNumActions(); ++i)
    {
        const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(i);
        if (!Action.IsValid())
        {
            continue;
        }
        const FString& ActionSearchText = Action->GetFullSearchText();
        FString LowerSearchText = ActionSearchText.ToLower();
        
        // Check if all search terms match (case-insensitive) - same as search-blueprint-nodes
        bool bMatchesAllTerms = true;
        for (const FString& Term : LowerFilterTerms)
        {
            if (!LowerSearchText.Contains(Term))
            {
                bMatchesAllTerms = false;
                break;
            }
        }
        
        if (bMatchesAllTerms)
        {
            MatchingActionIndices.Add(i);
        }
    }
    
    if (MatchingActionIndices.Num() == 0)
    {
        OutError = FString::Printf(TEXT("No nodes found matching name: %s"), *NodeName);
        return false;
    }
    
    // Now find the exact match using actionIdentifier
    int32 ExactMatchIndex = -1;
    
    // Convert the actionIdentifier back to the original format
    // Simply replace > with \n since we use > as our delimiter
    FString SearchActionId = ActionIdentifier.Replace(TEXT(">"), TEXT("\n"));
    
    for (int32 Index : MatchingActionIndices)
    {
        const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
        FString CurrentActionId = Action.IsValid() ? Action->GetFullSearchText() : FString();
        
        // Compare using the converted identifier
        if (CurrentActionId == SearchActionId)
        {
            ExactMatchIndex = Index;
            break;
        }
    }
    
    if (ExactMatchIndex == -1)
    {
        OutError = FString::Printf(TEXT("Found %d nodes matching name '%s', but none with the exact actionIdentifier"), 
            MatchingActionIndices.Num(), *NodeName);
        return false;
    }
    
    // Get the matching action
    TSharedPtr<FEdGraphSchemaAction> SchemaAction = MenuBuilder.GetSchemaAction(ExactMatchIndex);
    if (!SchemaAction.IsValid())
    {
        OutError = TEXT("Invalid schema action");
        return false;
    }
    
    // Store the current node count to identify the new node
    int32 PreSpawnNodeCount = Graph->Nodes.Num();
    
    // Perform the action to spawn the node
    UEdGraphNode* SpawnedNode = SchemaAction->PerformAction(Graph, nullptr, FVector2f(Location));
    
    if (!SpawnedNode)
    {
        // Sometimes the action doesn't return the node directly, check if a new node was added
        if (Graph->Nodes.Num() > PreSpawnNodeCount)
        {
            SpawnedNode = Graph->Nodes.Last();
        }
        else
        {
            OutError = TEXT("Failed to spawn node - action did not create a new node");
            return false;
        }
    }

    // Return the spawned node for the caller to use
    OutSpawnedNode = SpawnedNode;

    // Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
    FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

    // Log for debugging
    FN2CLogger::Get().Log(FString::Printf(TEXT("Spawned node: %s at (%d, %d)"),
        *SpawnedNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
        SpawnedNode->NodePosX, SpawnedNode->NodePosY), EN2CLogSeverity::Debug);

    return true;
}

TSharedPtr<FJsonObject> FN2CMcpAddBlueprintNodeTool::ConvertSpawnedNodeToJson(UK2Node* Node)
{
    if (!Node)
    {
        return nullptr;
    }
    
    // Describe the node on its own (a fresh capture context; no other capture is read or changed)
    const TValueOrError<FN2CNodeDefinition, FString> NodeDef = N2C::DescribeNode(*Node);
    if (NodeDef.HasError())
    {
        return nullptr;
    }
    
    // Convert to JSON
    return FN2CSerializer::NodeToJsonObject(NodeDef.GetValue());
}
