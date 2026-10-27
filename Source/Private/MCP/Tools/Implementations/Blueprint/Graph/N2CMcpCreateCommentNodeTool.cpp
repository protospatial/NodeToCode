// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateCommentNodeTool.h"
#include "MCP/Utils/N2CMcpNotification.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpCreateCommentNodeTool)

FMcpToolDefinition FN2CMcpCreateCommentNodeTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("create-comment-node"),
        TEXT("Creates a comment node around specified Blueprint nodes using their GUIDs, in the graph blueprintPath plus graphName or graphGuid name, else the focused graph. Get node GUIDs from find-nodes-in-graph or get-focused-blueprint first. Do NOT set a color unless the user requests it.")
    ,

    	N2CMcpToolsets::BlueprintGraph

    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("nodeGuids"), TEXT("array"));
    Properties.Add(TEXT("commentText"), TEXT("string"));
    Properties.Add(TEXT("color"), TEXT("object"));
    Properties.Add(TEXT("fontSize"), TEXT("number"));
    Properties.Add(TEXT("moveMode"), TEXT("string"));
    Properties.Add(TEXT("padding"), TEXT("number"));
    
    TArray<FString> Required;
    Required.Add(TEXT("nodeGuids"));
    
    TSharedPtr<FJsonObject> Schema = BuildInputSchema(Properties, Required);
    
    // Add array schema for nodeGuids
    TSharedPtr<FJsonObject> NodeGuidsSchema = MakeShareable(new FJsonObject);
    NodeGuidsSchema->SetStringField(TEXT("type"), TEXT("array"));
    NodeGuidsSchema->SetStringField(TEXT("description"), TEXT("Array of node GUIDs to include in the comment"));
    TSharedPtr<FJsonObject> ItemsSchema = MakeShareable(new FJsonObject);
    ItemsSchema->SetStringField(TEXT("type"), TEXT("string"));
    NodeGuidsSchema->SetObjectField(TEXT("items"), ItemsSchema);
    
    TSharedPtr<FJsonObject> SchemaProperties = Schema->GetObjectField(TEXT("properties"));
    SchemaProperties->SetObjectField(TEXT("nodeGuids"), NodeGuidsSchema);
    
    // Add color object schema
    TSharedPtr<FJsonObject> ColorSchema = MakeShareable(new FJsonObject);
    ColorSchema->SetStringField(TEXT("type"), TEXT("object"));
    ColorSchema->SetStringField(TEXT("description"), TEXT("RGB color values (0-1 range)"));
    
    TSharedPtr<FJsonObject> ColorProperties = MakeShareable(new FJsonObject);
    
    TSharedPtr<FJsonObject> RSchema = MakeShareable(new FJsonObject);
    RSchema->SetStringField(TEXT("type"), TEXT("number"));
    RSchema->SetNumberField(TEXT("default"), 1.0);
    ColorProperties->SetObjectField(TEXT("r"), RSchema);
    
    TSharedPtr<FJsonObject> GSchema = MakeShareable(new FJsonObject);
    GSchema->SetStringField(TEXT("type"), TEXT("number"));
    GSchema->SetNumberField(TEXT("default"), 1.0);
    ColorProperties->SetObjectField(TEXT("g"), GSchema);
    
    TSharedPtr<FJsonObject> BSchema = MakeShareable(new FJsonObject);
    BSchema->SetStringField(TEXT("type"), TEXT("number"));
    BSchema->SetNumberField(TEXT("default"), 1.0);
    ColorProperties->SetObjectField(TEXT("b"), BSchema);
    
    ColorSchema->SetObjectField(TEXT("properties"), ColorProperties);
    SchemaProperties->SetObjectField(TEXT("color"), ColorSchema);
    
    // Add moveMode enum
    TSharedPtr<FJsonObject> MoveModeSchema = MakeShareable(new FJsonObject);
    MoveModeSchema->SetStringField(TEXT("type"), TEXT("string"));
    MoveModeSchema->SetStringField(TEXT("description"), TEXT("Movement mode for the comment"));
    TArray<TSharedPtr<FJsonValue>> MoveModeEnum;
    MoveModeEnum.Add(MakeShareable(new FJsonValueString(TEXT("group"))));
    MoveModeEnum.Add(MakeShareable(new FJsonValueString(TEXT("none"))));
    MoveModeSchema->SetArrayField(TEXT("enum"), MoveModeEnum);
    MoveModeSchema->SetStringField(TEXT("default"), TEXT("group"));
    SchemaProperties->SetObjectField(TEXT("moveMode"), MoveModeSchema);
    
    Definition.InputSchema = Schema;
    
    FN2CGraphTarget::AddSchemaProperties(Definition);

    return Definition;
}

FMcpToolCallResult FN2CMcpCreateCommentNodeTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FCommentNodeRequest Request;
        FString ParseError;
        
        if (!ParseArguments(Arguments, Request, ParseError))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }
        
        // The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0152); every result
        // after this says which graph was used ('target')
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }
        UBlueprint* FocusedBlueprint = Target.Blueprint;
        UEdGraph* FocusedGraph = Target.Graph;
        
        // Find nodes by GUIDs
        TArray<FString> MissingGuids;
        TArray<UEdGraphNode*> NodesToComment = FindNodesByGuids(FocusedGraph, Request.NodeGuids, MissingGuids);
        
        if (NodesToComment.Num() == 0)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("No valid nodes found with the specified GUIDs")));
        }
        
        // Create a transaction for undo/redo
        const FScopedTransaction Transaction(NSLOCTEXT("MCP", "CreateCommentNode", "Create Comment Node"));
        FocusedGraph->Modify();
        
        // Calculate bounding box
        FSlateRect Bounds = CalculateBoundingBox(NodesToComment, Request.Padding);
        
        // Create the comment node through the schema's template path, around the nodes: RF_Transactional and a GUID
        // (card 0155), in this transaction
        UEdGraphNode_Comment* CommentNode = FN2CMcpNodeSpawn::SpawnCommentNode(FocusedGraph, Bounds, Request.CommentText,
            Request.Color, Request.FontSize, Request.bGroupMovement, NodesToComment);
        if (!CommentNode)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to create comment node")));
        }
        
        // A comment changes nothing the compiler sees: dirty the package, never compile (card 0151)
        FN2CMcpBlueprintUtils::MarkCosmeticChange(FocusedBlueprint);
        
        // Show notification
        FN2CMcpNotification::Show(FText::Format(
            NSLOCTEXT("NodeToCode", "CommentNodeCreated", "Created comment node around {0} nodes"),
            FText::AsNumber(NodesToComment.Num())
        ));
        
        // Build and return result
        TSharedPtr<FJsonObject> Result = BuildResultJson(true, CommentNode, NodesToComment, MissingGuids);
        
        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Created comment node '%s' around %d nodes"),
                *Request.CommentText, NodesToComment.Num()),
            EN2CLogSeverity::Debug
        );
        
        return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultJson));
    });
}

bool FN2CMcpCreateCommentNodeTool::ParseArguments(const TSharedPtr<FJsonObject>& Arguments, FCommentNodeRequest& OutRequest, FString& OutError) const
{
    if (!Arguments.IsValid())
    {
        OutError = TEXT("Invalid arguments object");
        return false;
    }
    
    FN2CMcpArgumentParser ArgParser(Arguments);
    
    // Required: nodeGuids array
    const TArray<TSharedPtr<FJsonValue>>* NodeGuidsArray = ArgParser.GetOptionalArray(TEXT("nodeGuids"));
    if (!NodeGuidsArray || NodeGuidsArray->Num() == 0)
    {
        OutError = TEXT("nodeGuids array is required and must not be empty");
        return false;
    }
    
    // Extract node GUIDs from array
    for (const TSharedPtr<FJsonValue>& Value : *NodeGuidsArray)
    {
        FString Guid;
        if (Value->TryGetString(Guid))
        {
            OutRequest.NodeGuids.Add(Guid);
        }
    }
    
    if (OutRequest.NodeGuids.Num() == 0)
    {
        OutError = TEXT("No valid GUIDs found in nodeGuids array");
        return false;
    }
    
    // Optional: commentText
    OutRequest.CommentText = ArgParser.GetOptionalString(TEXT("commentText"), TEXT("Comment"));
    
    // Optional: color
    TSharedPtr<FJsonObject> ColorObject = ArgParser.GetOptionalObject(TEXT("color"));
    if (ColorObject.IsValid())
    {
        FN2CMcpArgumentParser ColorParser(ColorObject);
        float R = ColorParser.GetOptionalNumber(TEXT("r"), 0.075);
        float G = ColorParser.GetOptionalNumber(TEXT("g"), 0.075);
        float B = ColorParser.GetOptionalNumber(TEXT("b"), 0.075);
        OutRequest.Color = FLinearColor(R, G, B);
    }
    
    // Optional: fontSize
    OutRequest.FontSize = FMath::Clamp(ArgParser.GetOptionalNumber(TEXT("fontSize"), 18.0), 1.0, 1000.0);
    
    // Optional: moveMode
    FString MoveMode = ArgParser.GetOptionalString(TEXT("moveMode"), TEXT("group"));
    OutRequest.bGroupMovement = (MoveMode == TEXT("group"));
    
    // Optional: padding
    OutRequest.Padding = ArgParser.GetOptionalNumber(TEXT("padding"), 50.0);
    
    return true;
}

TArray<UEdGraphNode*> FN2CMcpCreateCommentNodeTool::FindNodesByGuids(UEdGraph* Graph, const TArray<FString>& NodeGuids, TArray<FString>& OutMissingGuids) const
{
    TArray<UEdGraphNode*> FoundNodes;
    
    if (!Graph)
    {
        return FoundNodes;
    }
    
    // Build a map of all nodes by GUID for efficient lookup
    TMap<FGuid, UEdGraphNode*> NodeMap;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node && Node->NodeGuid.IsValid())
        {
            NodeMap.Add(Node->NodeGuid, Node);
        }
    }
    
    // Find requested nodes
    for (const FString& GuidString : NodeGuids)
    {
        FGuid ParsedGuid;
        if (FGuid::Parse(GuidString, ParsedGuid))
        {
            UEdGraphNode* Node = NodeMap.FindRef(ParsedGuid);
            if (Node)
            {
                // Skip comment nodes to avoid nesting
                if (!Node->IsA<UEdGraphNode_Comment>())
                {
                    FoundNodes.Add(Node);
                }
            }
            else
            {
                OutMissingGuids.Add(GuidString);
            }
        }
        else
        {
            OutMissingGuids.Add(GuidString);
        }
    }
    
    return FoundNodes;
}

FSlateRect FN2CMcpCreateCommentNodeTool::CalculateBoundingBox(const TArray<UEdGraphNode*>& Nodes, float Padding) const
{
    if (Nodes.Num() == 0)
    {
        return FSlateRect(0, 0, 100, 100);
    }
    
    // Initialize bounds with the first node
    float MinX = Nodes[0]->NodePosX;
    float MinY = Nodes[0]->NodePosY;
    
    // Use default size for nodes that don't have width/height set
    const float DefaultNodeWidth = 150.0f;
    const float DefaultNodeHeight = 50.0f;
    
    float NodeWidth = Nodes[0]->NodeWidth > 0 ? Nodes[0]->NodeWidth : DefaultNodeWidth;
    float NodeHeight = Nodes[0]->NodeHeight > 0 ? Nodes[0]->NodeHeight : DefaultNodeHeight;
    
    float MaxX = MinX + NodeWidth;
    float MaxY = MinY + NodeHeight;
    
    // Expand bounds to include all nodes
    for (int32 i = 1; i < Nodes.Num(); i++)
    {
        UEdGraphNode* Node = Nodes[i];
        MinX = FMath::Min(MinX, (float)Node->NodePosX);
        MinY = FMath::Min(MinY, (float)Node->NodePosY);
        
        NodeWidth = Node->NodeWidth > 0 ? Node->NodeWidth : DefaultNodeWidth;
        NodeHeight = Node->NodeHeight > 0 ? Node->NodeHeight : DefaultNodeHeight;
        
        MaxX = FMath::Max(MaxX, Node->NodePosX + NodeWidth);
        MaxY = FMath::Max(MaxY, Node->NodePosY + NodeHeight);
    }
    
    // Add padding
    MinX -= Padding;
    MinY -= Padding;
    MaxX += Padding;
    MaxY += Padding;
    
    return FSlateRect(MinX, MinY, MaxX, MaxY);
}

TSharedPtr<FJsonObject> FN2CMcpCreateCommentNodeTool::BuildResultJson(
    bool bSuccess,
    UEdGraphNode_Comment* CommentNode,
    const TArray<UEdGraphNode*>& IncludedNodes,
    const TArray<FString>& MissingGuids,
    const FString& ErrorMessage) const
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    
    Result->SetBoolField(TEXT("success"), bSuccess);
    
    if (!bSuccess)
    {
        Result->SetStringField(TEXT("error"), ErrorMessage);
        return Result;
    }
    
    // Comment node info
    if (CommentNode)
    {
        TSharedPtr<FJsonObject> CommentInfo = MakeShareable(new FJsonObject);
        CommentInfo->SetStringField(TEXT("guid"), CommentNode->NodeGuid.ToString());
        CommentInfo->SetStringField(TEXT("text"), CommentNode->NodeComment);
        CommentInfo->SetNumberField(TEXT("x"), CommentNode->NodePosX);
        CommentInfo->SetNumberField(TEXT("y"), CommentNode->NodePosY);
        CommentInfo->SetNumberField(TEXT("width"), CommentNode->NodeWidth);
        CommentInfo->SetNumberField(TEXT("height"), CommentNode->NodeHeight);
        
        TSharedPtr<FJsonObject> ColorObj = MakeShareable(new FJsonObject);
        ColorObj->SetNumberField(TEXT("r"), CommentNode->CommentColor.R);
        ColorObj->SetNumberField(TEXT("g"), CommentNode->CommentColor.G);
        ColorObj->SetNumberField(TEXT("b"), CommentNode->CommentColor.B);
        CommentInfo->SetObjectField(TEXT("color"), ColorObj);
        
        CommentInfo->SetNumberField(TEXT("fontSize"), CommentNode->FontSize);
        CommentInfo->SetStringField(TEXT("moveMode"), 
            CommentNode->MoveMode == ECommentBoxMode::GroupMovement ? TEXT("group") : TEXT("none"));
        
        Result->SetObjectField(TEXT("commentNode"), CommentInfo);
    }
    
    // Included nodes
    TArray<TSharedPtr<FJsonValue>> IncludedArray;
    for (UEdGraphNode* Node : IncludedNodes)
    {
        TSharedPtr<FJsonObject> NodeInfo = MakeShareable(new FJsonObject);
        NodeInfo->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
        NodeInfo->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        IncludedArray.Add(MakeShareable(new FJsonValueObject(NodeInfo)));
    }
    Result->SetArrayField(TEXT("includedNodes"), IncludedArray);
    Result->SetNumberField(TEXT("includedCount"), IncludedNodes.Num());
    
    // Missing nodes
    if (MissingGuids.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> MissingArray;
        for (const FString& Guid : MissingGuids)
        {
            MissingArray.Add(MakeShareable(new FJsonValueString(Guid)));
        }
        Result->SetArrayField(TEXT("missingGuids"), MissingArray);
    }
    
    Result->SetStringField(TEXT("message"), FString::Printf(
        TEXT("Successfully created comment node around %d nodes"), IncludedNodes.Num()));
    
    return Result;
}