// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpAddBlueprintNodeTool.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpNodeActions.h"
#include "Utils/N2CLogger.h"
#include "BlueprintActionMenuBuilder.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register the tool
REGISTER_MCP_TOOL(FN2CMcpAddBlueprintNodeTool)

FN2CMcpAddBlueprintNodeTool::FN2CMcpAddBlueprintNodeTool()
{
}

FMcpToolDefinition FN2CMcpAddBlueprintNodeTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("add-bp-node-to-active-graph"),
        TEXT("Adds a Blueprint node to a graph: the one blueprintPath plus graphName or graphGuid name, else the focused graph (the result's target says which). IMPORTANT: Use search-blueprint-nodes first and pass a result's spawnMetadata.actionIdentifier (a stable ID) and spawnMetadata.nodeName. NEXT STEP: After adding a node, use the 'connect-pins' tool to wire the node's pins to other nodes using the pinGuid values returned in inputPins and outputPins arrays."),
        N2CMcpToolsets::BlueprintGraph
    );
    
    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // nodeName property (required)
    TSharedPtr<FJsonObject> NodeNameProp = MakeShareable(new FJsonObject);
    NodeNameProp->SetStringField(TEXT("type"), TEXT("string"));
    NodeNameProp->SetStringField(TEXT("description"), TEXT("The node's menu name, spawnMetadata.nodeName of the search result (e.g., 'Spawn Actor from Class'). Only used to tell apart actions that share an actionIdentifier"));
    Properties->SetObjectField(TEXT("nodeName"), NodeNameProp);
    
    // actionIdentifier property (required)
    TSharedPtr<FJsonObject> ActionIdProp = MakeShareable(new FJsonObject);
    ActionIdProp->SetStringField(TEXT("type"), TEXT("string"));
    ActionIdProp->SetStringField(TEXT("description"), TEXT("The stable action ID from spawnMetadata.actionIdentifier of a search-blueprint-nodes result, passed unchanged, e.g. (FieldName=\"/Script/Engine.KismetSystemLibrary:PrintString\",NodeName=\"/Script/BlueprintGraph.K2Node_CallFunction\"). It is the node spawner's signature, so it stays valid across searches, captures and compiles. Old search-text identifiers are still accepted for now."));
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
    
    FN2CGraphTarget::AddSchemaProperties(Definition);

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
    
        // The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0152); every result
        // after this says which graph was used ('target')
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }
        UBlueprint* ActiveBlueprint = Target.Blueprint;
        UEdGraph* ActiveGraph = Target.Graph;
    
        // Find and spawn the node
        UEdGraphNode* SpawnedNode = nullptr;
        FString SpawnError;

        if (!FindAndSpawnNode(NodeName, ActionIdentifier, ActiveBlueprint, ActiveGraph, Location, SpawnedNode, SpawnError))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(SpawnError));
        }

        FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully added node '%s' to graph '%s'"),
            *NodeName, *ActiveGraph->GetName()), EN2CLogSeverity::Info);

        // Build result with pin information for immediate use
        TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
        ResultObject->SetBoolField(TEXT("success"), true);
        ResultObject->SetStringField(TEXT("graphName"), ActiveGraph->GetName());
        ResultObject->SetStringField(TEXT("blueprintName"), ActiveBlueprint->GetName());

        TSharedPtr<FJsonObject> LocationObject = MakeShareable(new FJsonObject);
        LocationObject->SetNumberField(TEXT("x"), SpawnedNode->NodePosX);
        LocationObject->SetNumberField(TEXT("y"), SpawnedNode->NodePosY);
        ResultObject->SetObjectField(TEXT("location"), LocationObject);

        // nodeGuid, nodeName and the visible pins (inputPins, outputPins), so the caller can connect them without
        // another lookup; the variable node tools return the same fields (FN2CMcpNodeSpawn::AddNodeFields, card 0155)
        FN2CMcpNodeSpawn::AddNodeFields(*ResultObject, SpawnedNode);

        // Add guidance for the next step
        TSharedPtr<FJsonObject> NextStepObject = MakeShareable(new FJsonObject);
        NextStepObject->SetStringField(TEXT("tool"), TEXT("connect-pins"));
        NextStepObject->SetStringField(TEXT("description"), TEXT("Use the connect-pins tool with the nodeGuid and pinGuid values above to wire this node to other nodes in the graph."));
        ResultObject->SetObjectField(TEXT("nextStep"), NextStepObject);

        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);

        return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultJson));
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
    // The same action list search-blueprint-nodes builds for this graph
    FBlueprintActionMenuBuilder MenuBuilder;
    N2CNodeActions::BuildActionList(MenuBuilder, Blueprint, Graph);

    // The ID is the spawner signature search-blueprint-nodes returns (card 0277); identifiers from before that
    // (search text with '>' for line breaks, with or without the carriage returns) are still accepted
    bool bLegacyIdentifier = false;
    const TSharedPtr<FEdGraphSchemaAction> SchemaAction = N2CNodeActions::ResolveAction(MenuBuilder, ActionIdentifier,
        NodeName, OutError, &bLegacyIdentifier);
    if (!SchemaAction.IsValid())
    {
        return false;
    }
    if (bLegacyIdentifier)
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("add-bp-node-to-active-graph: '%s' was given an old-style actionIdentifier; search again for the stable one"), *NodeName));
    }

    UEdGraphNode* SpawnedNode = N2CNodeActions::SpawnAction(SchemaAction, Graph, FVector2f(Location), OutError);
    if (!SpawnedNode)
    {
        return false;
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
