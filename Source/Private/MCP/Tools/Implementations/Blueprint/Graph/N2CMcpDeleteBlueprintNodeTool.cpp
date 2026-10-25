// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeleteBlueprintNodeTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Event.h"
#include "K2Node_Tunnel.h"
#include "K2Node.h"
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
        TEXT("Deletes one or more Blueprint nodes from the currently focused graph using their GUIDs. Supports connection preservation and batch operations."),
        TEXT("Blueprint Graph Editing")
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
    PreserveConnectionsProp->SetStringField(TEXT("description"), TEXT("Meant to reconnect the deleted node's input sources to its output targets. Currently has no effect: no link is ever made. Default: false"));
    PreserveConnectionsProp->SetBoolField(TEXT("default"), false);
    Properties->SetObjectField(TEXT("preserveConnections"), PreserveConnectionsProp);
    
    // force property (optional)
    TSharedPtr<FJsonObject> ForceProp = MakeShareable(new FJsonObject);
    ForceProp->SetStringField(TEXT("type"), TEXT("boolean"));
    ForceProp->SetStringField(TEXT("description"), TEXT("If true, bypasses validation checks and forces deletion of nodes that would normally be protected. Default: false"));
    ForceProp->SetBoolField(TEXT("default"), false);
    Properties->SetObjectField(TEXT("force"), ForceProp);
    
    Schema->SetObjectField(TEXT("properties"), Properties);
    
    // Required fields
    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("nodeGuids"))));
    Schema->SetArrayField(TEXT("required"), RequiredArray);
    
    Definition.InputSchema = Schema;
    
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
        
        // Get the active graph context
        FString ContextError;
        UBlueprint* ActiveBlueprint = nullptr;
        UEdGraph* ActiveGraph = nullptr;
        if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ActiveBlueprint, ActiveGraph, ContextError))
        {
            return FMcpToolCallResult::CreateErrorResult(ContextError);
        }
        
        // Validate nodes
        TArray<UEdGraphNode*> NodesToDelete;
        FString ValidationError;
        if (!ValidateNodes(ActiveGraph, NodeGuids, NodesToDelete, bForce, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(ValidationError);
        }
        
        // Create transaction for undo/redo
        FGuid TransactionGuid = FGuid::NewGuid();
        const FScopedTransaction Transaction(LOCTEXT("DeleteBlueprintNodes", "Delete Blueprint Nodes"));
        
        // Delete nodes
        TArray<FDeletedNodeInfo> DeletedInfo;
        if (!DeleteNodes(ActiveBlueprint, ActiveGraph, NodesToDelete, bPreserveConnections, DeletedInfo))
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to delete nodes"));
        }
        
        // Show notification
        ShowDeletionNotification(DeletedInfo.Num(), true);
        
        // Build and return result
        TSharedPtr<FJsonObject> Result = BuildSuccessResult(DeletedInfo, ActiveBlueprint->GetName(), TransactionGuid);
        
        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(ResultJson);
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
    
    // Find and validate each node
    for (const FGuid& NodeGuid : NodeGuids)
    {
        UEdGraphNode* FoundNode = nullptr;
        
        // Search for node by GUID
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid == NodeGuid)
            {
                FoundNode = Node;
                break;
            }
        }
        
        if (!FoundNode)
        {
            OutError = FString::Printf(TEXT("Node with GUID '%s' not found in graph"), *NodeGuid.ToString());
            return false;
        }
        
        // Check if node is deletable
        if (!IsNodeDeletable(FoundNode, bForce))
        {
            OutError = FString::Printf(TEXT("Node '%s' is protected and cannot be deleted. Use force=true to override."),
                *FoundNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            return false;
        }
        
        OutValidNodes.Add(FoundNode);
    }
    
    return true;
}

bool FN2CMcpDeleteBlueprintNodeTool::IsNodeDeletable(UEdGraphNode* Node, bool bForce) const
{
    if (!Node || bForce)
    {
        return bForce;
    }
    
    // Check for protected node types
    if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>())
    {
        return false; // Function entry/result nodes should not be deleted
    }
    
    // Check for tunnel nodes (may have special deletion rules)
    if (UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node))
    {
        if (TunnelNode->bCanHaveInputs && TunnelNode->bCanHaveOutputs)
        {
            // Tunnel entry/exit nodes might need special handling
            return false;
        }
    }
    
    // Most other nodes are deletable
    return true;
}

bool FN2CMcpDeleteBlueprintNodeTool::PreserveNodeConnections(
    UEdGraphNode* NodeToDelete,
    TArray<FPreservedConnection>& OutPreservedConnections)
{
    if (!NodeToDelete)
    {
        return false;
    }
    
    // Find input pins with connections and output pins with connections
    TArray<UEdGraphPin*> ConnectedInputPins;
    TArray<UEdGraphPin*> ConnectedOutputPins;
    
    for (UEdGraphPin* Pin : NodeToDelete->Pins)
    {
        if (!Pin || Pin->LinkedTo.Num() == 0)
        {
            continue;
        }
        
        if (Pin->Direction == EGPD_Input)
        {
            ConnectedInputPins.Add(Pin);
        }
        else if (Pin->Direction == EGPD_Output)
        {
            ConnectedOutputPins.Add(Pin);
        }
    }
    
    // For each input pin, try to connect its source to compatible output pins' targets
    for (UEdGraphPin* InputPin : ConnectedInputPins)
    {
        for (UEdGraphPin* OutputPin : ConnectedOutputPins)
        {
            // Check if pins are type-compatible
            const UEdGraphSchema* Schema = InputPin->GetSchema();
            if (!Schema->ArePinsCompatible(InputPin->LinkedTo[0], OutputPin, nullptr, false))
            {
                continue; // Skip incompatible pins
            }
            
            // Connect input's source to output's targets
            for (UEdGraphPin* SourcePin : InputPin->LinkedTo)
            {
                for (UEdGraphPin* TargetPin : OutputPin->LinkedTo)
                {
                    const FPinConnectionResponse ConnectionResponse = Schema->CanCreateConnection(SourcePin, TargetPin);
                    if (ConnectionResponse.CanSafeConnect())
                    {
                        // Record this connection for later creation
                        FPreservedConnection Connection;
                        Connection.FromNodeGuid = SourcePin->GetOwningNode()->NodeGuid.ToString();
                        Connection.FromPinName = SourcePin->PinName.ToString();
                        Connection.ToNodeGuid = TargetPin->GetOwningNode()->NodeGuid.ToString();
                        Connection.ToPinName = TargetPin->PinName.ToString();
                        Connection.PinType = SourcePin->PinType.PinCategory.ToString();
                        
                        OutPreservedConnections.Add(Connection);
                        
                        // Make the connection
                        SourcePin->MakeLinkTo(TargetPin);
                    }
                }
            }
        }
    }
    
    return true;
}

void FN2CMcpDeleteBlueprintNodeTool::CollectNodeInfo(
    UEdGraphNode* Node,
    FDeletedNodeInfo& OutInfo)
{
    if (!Node)
    {
        return;
    }
    
    OutInfo.NodeGuid = Node->NodeGuid.ToString();
    OutInfo.NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
    OutInfo.NodeType = Node->GetClass()->GetName();
    OutInfo.GraphName = Node->GetGraph() ? Node->GetGraph()->GetName() : TEXT("Unknown");
}

bool FN2CMcpDeleteBlueprintNodeTool::DeleteNodes(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const TArray<UEdGraphNode*>& NodesToDelete,
    bool bPreserveConnections,
    TArray<FDeletedNodeInfo>& OutDeletedInfo)
{
    if (!Blueprint || !Graph || NodesToDelete.Num() == 0)
    {
        return false;
    }
    
    // Mark Blueprint and Graph as modified for transaction
    Blueprint->Modify();
    Graph->Modify();
    
    // Process each node
    for (UEdGraphNode* Node : NodesToDelete)
    {
        if (!Node)
        {
            continue;
        }
        
        // Collect node info before deletion
        FDeletedNodeInfo NodeInfo;
        CollectNodeInfo(Node, NodeInfo);
        
        // Preserve connections if requested
        if (bPreserveConnections)
        {
            PreserveNodeConnections(Node, NodeInfo.PreservedConnections);
        }
        
        // Mark node as modified for transaction
        Node->Modify();
        
        // Destroy the node
        Node->DestroyNode();
        
        OutDeletedInfo.Add(NodeInfo);
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("Deleted node: %s"), *NodeInfo.NodeTitle), EN2CLogSeverity::Debug);
    }
    
    // Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
    FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);
    
    return true;
}

TSharedPtr<FJsonObject> FN2CMcpDeleteBlueprintNodeTool::BuildSuccessResult(
    const TArray<FDeletedNodeInfo>& DeletedInfo,
    const FString& BlueprintName,
    const FGuid& TransactionGuid) const
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprintName"), BlueprintName);
    Result->SetStringField(TEXT("transactionId"), TransactionGuid.ToString());
    Result->SetNumberField(TEXT("deletedCount"), DeletedInfo.Num());
    
    // Build array of deleted nodes
    TArray<TSharedPtr<FJsonValue>> DeletedNodesArray;
    
    for (const FDeletedNodeInfo& Info : DeletedInfo)
    {
        TSharedPtr<FJsonObject> NodeObject = MakeShareable(new FJsonObject);
        NodeObject->SetStringField(TEXT("guid"), Info.NodeGuid);
        NodeObject->SetStringField(TEXT("title"), Info.NodeTitle);
        NodeObject->SetStringField(TEXT("type"), Info.NodeType);
        NodeObject->SetStringField(TEXT("graph"), Info.GraphName);
        
        // Add preserved connections if any
        if (Info.PreservedConnections.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
            
            for (const FPreservedConnection& Connection : Info.PreservedConnections)
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
    
    FNotificationInfo Info(NotificationText);
    Info.ExpireDuration = 3.0f;
    Info.bFireAndForget = true;
    Info.Image = bSuccess ? FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle")) : FCoreStyle::Get().GetBrush(TEXT("Icons.ErrorWithCircle"));
    
    FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE