// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeleteBlueprintNodeTool.h"
#include "MCP/Utils/N2CMcpNotification.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpNodeDeletion.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register the tool
REGISTER_MCP_TOOL(FN2CMcpDeleteBlueprintNodeTool)

#define LOCTEXT_NAMESPACE "NodeToCode"

FMcpToolDefinition FN2CMcpDeleteBlueprintNodeTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("delete-blueprint-node"),
        TEXT("Deletes one or more Blueprint nodes from a graph using their GUIDs: the graph blueprintPath plus graphName or graphGuid name, else the focused graph. Supports connection preservation and batch operations."),
        N2CMcpToolsets::BlueprintGraph
    );
    
    // Build input schema
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    
    // nodeGuids property (required) - array of strings
    TSharedPtr<FJsonObject> NodeGuidsProp = MakeShareable(new FJsonObject);
    NodeGuidsProp->SetStringField(TEXT("type"), TEXT("array"));
    NodeGuidsProp->SetStringField(TEXT("description"), TEXT("Array of node GUIDs to delete"));
    
    TSharedPtr<FJsonObject> ItemsProp = MakeShareable(new FJsonObject);
    ItemsProp->SetStringField(TEXT("type"), TEXT("string"));
    NodeGuidsProp->SetObjectField(TEXT("items"), ItemsProp);
    NodeGuidsProp->SetNumberField(TEXT("minItems"), 1);
    
    Properties->SetObjectField(TEXT("nodeGuids"), NodeGuidsProp);
    
    // preserveConnections property (optional)
    TSharedPtr<FJsonObject> PreserveConnectionsProp = MakeShareable(new FJsonObject);
    PreserveConnectionsProp->SetStringField(TEXT("type"), TEXT("boolean"));
    PreserveConnectionsProp->SetStringField(TEXT("description"), TEXT("If true, reconnects the links that pass through each deleted node: the sources of its exec input to the targets of its exec output (only on a node with one exec input and one exec output), and through reroute knots. Each link made is listed in preservedConnections; a link the schema refuses or that would replace another link is not made. Data pins of other nodes are not bridged. Default: false"));
    PreserveConnectionsProp->SetBoolField(TEXT("default"), false);
    Properties->SetObjectField(TEXT("preserveConnections"), PreserveConnectionsProp);
    
    // force property (optional)
    TSharedPtr<FJsonObject> ForceProp = MakeShareable(new FJsonObject);
    ForceProp->SetStringField(TEXT("type"), TEXT("boolean"));
    ForceProp->SetStringField(TEXT("description"), TEXT("If true, deletes nodes the editor protects (CanUserDeleteNode false: a function entry, the entry and exit tunnels of a collapsed graph or macro). Deleting those can leave the Blueprint unable to compile. Default: false"));
    ForceProp->SetBoolField(TEXT("default"), false);
    Properties->SetObjectField(TEXT("force"), ForceProp);
    
    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("nodeGuids"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
    FN2CGraphTarget::AddSchemaProperties(Definition);

    return Definition;
}

FMcpToolCallResult FN2CMcpDeleteBlueprintNodeTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        FN2CLogger::Get().Log(TEXT("DeleteBlueprintNode: Starting execution"), EN2CLogSeverity::Debug);
        
        // Parse arguments
        TArray<FGuid> NodeGuids;
        bool bPreserveConnections = false;
        bool bForce = false;
        FString ParseError;
        
        if (!ParseArguments(Arguments, NodeGuids, bPreserveConnections, bForce, ParseError))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("Deleting %d nodes (preserve connections: %s, force: %s)"),
            NodeGuids.Num(), bPreserveConnections ? TEXT("true") : TEXT("false"), bForce ? TEXT("true") : TEXT("false")), 
            EN2CLogSeverity::Info);
        
        // The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0152); every result
        // after this says which graph was used ('target')
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }
        UBlueprint* ActiveBlueprint = Target.Blueprint;
        UEdGraph* ActiveGraph = Target.Graph;
        
        // Validate nodes
        TArray<UEdGraphNode*> NodesToDelete;
        FString ValidationError;
        if (!ValidateNodes(ActiveGraph, NodeGuids, NodesToDelete, bForce, ValidationError))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(ValidationError));
        }
        
        // Create transaction for undo/redo
        const FScopedTransaction Transaction(LOCTEXT("DeleteBlueprintNodes", "Delete Blueprint Nodes"));
        
        // Delete nodes the way the editor's Delete does, shared with the Python bridge (card 0279)
        const TArray<N2CNodeDeletion::FDeletedNode> DeletedInfo = N2CNodeDeletion::DeleteNodes(ActiveBlueprint, NodesToDelete, bPreserveConnections);
        if (DeletedInfo.Num() == 0)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to delete nodes")));
        }
        
        // Show notification
        ShowDeletionNotification(DeletedInfo.Num(), true);
        
        // Build and return result
        TSharedPtr<FJsonObject> Result = BuildSuccessResult(DeletedInfo, ActiveBlueprint->GetName());
        
        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultJson));
    });
}

bool FN2CMcpDeleteBlueprintNodeTool::ParseArguments(
    const TSharedPtr<FJsonObject>& Arguments,
    TArray<FGuid>& OutNodeGuids,
    bool& OutPreserveConnections,
    bool& OutForce,
    FString& OutError)
{
    if (!Arguments.IsValid())
    {
        OutError = TEXT("Invalid arguments object");
        return false;
    }
    
    FN2CMcpArgumentParser ArgParser(Arguments);
    
    // Required: nodeGuids array
    const TArray<TSharedPtr<FJsonValue>>* NodeGuidsArray;
    if (!Arguments->TryGetArrayField(TEXT("nodeGuids"), NodeGuidsArray) || !NodeGuidsArray || NodeGuidsArray->Num() == 0)
    {
        OutError = TEXT("Missing or empty 'nodeGuids' array");
        return false;
    }
    
    // Parse each GUID
    for (const TSharedPtr<FJsonValue>& Value : *NodeGuidsArray)
    {
        FString GuidString;
        if (!Value->TryGetString(GuidString))
        {
            OutError = TEXT("Invalid GUID value in nodeGuids array - must be string");
            return false;
        }
        
        FGuid ParsedGuid;
        if (!FGuid::Parse(GuidString, ParsedGuid))
        {
            OutError = FString::Printf(TEXT("Invalid GUID format: %s"), *GuidString);
            return false;
        }
        
        OutNodeGuids.Add(ParsedGuid);
    }
    
    // Optional: preserveConnections
    OutPreserveConnections = ArgParser.GetOptionalBool(TEXT("preserveConnections"), false);
    
    // Optional: force
    OutForce = ArgParser.GetOptionalBool(TEXT("force"), false);
    
    return true;
}

bool FN2CMcpDeleteBlueprintNodeTool::ValidateNodes(
    UEdGraph* Graph,
    const TArray<FGuid>& NodeGuids,
    TArray<UEdGraphNode*>& OutValidNodes,
    bool bForce,
    FString& OutError)
{
    if (!Graph)
    {
        OutError = TEXT("Invalid graph");
        return false;
    }
    
    for (const FGuid& NodeGuid : NodeGuids)
    {
        UEdGraphNode* FoundNode = N2CNodeDeletion::FindNode(Graph, NodeGuid);
        if (!FoundNode)
        {
            OutError = FString::Printf(TEXT("Node with GUID '%s' not found in graph"), *NodeGuid.ToString());
            return false;
        }
        
        // The node's own CanUserDeleteNode, as the editor's Delete asks it; force overrides only that (card 0279)
        if (!N2CNodeDeletion::CanDelete(FoundNode, bForce, OutError))
        {
            return false;
        }
        
        OutValidNodes.AddUnique(FoundNode);
    }
    
    return true;
}

TSharedPtr<FJsonObject> FN2CMcpDeleteBlueprintNodeTool::BuildSuccessResult(
    const TArray<N2CNodeDeletion::FDeletedNode>& DeletedInfo,
    const FString& BlueprintName) const
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprintName"), BlueprintName);
    Result->SetNumberField(TEXT("deletedCount"), DeletedInfo.Num());
    
    // Build array of deleted nodes
    TArray<TSharedPtr<FJsonValue>> DeletedNodesArray;
    
    for (const N2CNodeDeletion::FDeletedNode& Info : DeletedInfo)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShareable(new FJsonObject);
        NodeObject->SetStringField(TEXT("guid"), Info.NodeGuid);
        NodeObject->SetStringField(TEXT("title"), Info.Title);
        NodeObject->SetStringField(TEXT("type"), Info.NodeType);
        NodeObject->SetStringField(TEXT("graph"), Info.GraphName);
        
        // Add preserved connections if any
        if (Info.BridgedLinks.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
            
            for (const N2CNodeDeletion::FBridgedLink& Connection : Info.BridgedLinks)
            {
                TSharedPtr<FJsonObject> ConnObject = MakeShareable(new FJsonObject);
                ConnObject->SetStringField(TEXT("fromNode"), Connection.FromNodeGuid);
                ConnObject->SetStringField(TEXT("fromPin"), Connection.FromPinName);
                ConnObject->SetStringField(TEXT("toNode"), Connection.ToNodeGuid);
                ConnObject->SetStringField(TEXT("toPin"), Connection.ToPinName);
                ConnObject->SetStringField(TEXT("type"), Connection.PinType);
                
                ConnectionsArray.Add(MakeShareable(new FJsonValueObject(ConnObject)));
            }
            
            NodeObject->SetArrayField(TEXT("preservedConnections"), ConnectionsArray);
        }
        
        DeletedNodesArray.Add(MakeShareable(new FJsonValueObject(NodeObject)));
    }
    
    Result->SetArrayField(TEXT("deletedNodes"), DeletedNodesArray);
    
    return Result;
}

void FN2CMcpDeleteBlueprintNodeTool::ShowDeletionNotification(int32 DeletedCount, bool bSuccess) const
{
    FText NotificationText;
    
    if (bSuccess)
    {
        if (DeletedCount == 1)
        {
            NotificationText = LOCTEXT("NodeDeleted", "1 node deleted successfully");
        }
        else
        {
            NotificationText = FText::Format(LOCTEXT("NodesDeleted", "{0} nodes deleted successfully"), DeletedCount);
        }
    }
    else
    {
        NotificationText = LOCTEXT("NodeDeletionFailed", "Failed to delete nodes");
    }
    
    FN2CMcpNotification::Show(NotificationText, bSuccess);
}

#undef LOCTEXT_NAMESPACE