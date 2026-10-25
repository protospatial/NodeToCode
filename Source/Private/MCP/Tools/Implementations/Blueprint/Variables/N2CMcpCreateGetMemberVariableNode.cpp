// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateGetMemberVariableNode.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_VariableGet.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register the tool
REGISTER_MCP_TOOL(FN2CMcpCreateGetMemberVariableNode)

FMcpToolDefinition FN2CMcpCreateGetMemberVariableNode::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("create-get-member-variable-node"),
        TEXT("Creates a Get node for a member variable in the focused blueprint graph. This node can be used to read values from member variables.")
    ,

    	TEXT("Blueprint Variable Management")

    );
    
    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("location"), TEXT("object"));
    
    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    
    TSharedPtr<FJsonObject> Schema = BuildInputSchema(Properties, Required);
    
    // Add location object properties
    TSharedPtr<FJsonObject> LocationProp = MakeShareable(new FJsonObject);
    LocationProp->SetStringField(TEXT("type"), TEXT("object"));
    LocationProp->SetStringField(TEXT("description"), TEXT("The location to place the node in the graph"));
    
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
    
    // Update the schema properties
    TSharedPtr<FJsonObject> SchemaProperties = Schema->GetObjectField(TEXT("properties"));
    SchemaProperties->SetObjectField(TEXT("location"), LocationProp);
    
    Definition.InputSchema = Schema;
    
    return Definition;
}

FMcpToolCallResult FN2CMcpCreateGetMemberVariableNode::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FString VariableName;
        FVector2D Location(0, 0);
        FString ParseError;
        
        if (!ParseArguments(Arguments, VariableName, Location, ParseError))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }
        
        // Get the active graph context
        FString ContextError;
        UBlueprint* ActiveBlueprint = nullptr;
        UEdGraph* ActiveGraph = nullptr;
        
        if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ActiveBlueprint, ActiveGraph, ContextError))
        {
            return FMcpToolCallResult::CreateErrorResult(ContextError);
        }
        
        // Find the member variable
        const FBPVariableDescription* Variable = FindMemberVariable(ActiveBlueprint, VariableName, ContextError);
        if (!Variable)
        {
            return FMcpToolCallResult::CreateErrorResult(ContextError);
        }
        
        // Create the Get node
        UK2Node_VariableGet* GetNode = CreateGetNode(ActiveBlueprint, ActiveGraph, *Variable, Location);
        if (!GetNode)
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to create Get node"));
        }
        
        // Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
        FN2CMcpBlueprintUtils::MarkModified(ActiveBlueprint, /*bStructural*/ false);
        
        // Show notification
        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "GetNodeCreated", "Created Get node for variable '{0}'"),
            FText::FromString(VariableName)
        ));
        Info.ExpireDuration = 3.0f;
        Info.bFireAndForget = true;
        Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle"));
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Build and return the result
        TSharedPtr<FJsonObject> Result = BuildSuccessResult(GetNode, *Variable, ActiveBlueprint, ActiveGraph);
        
        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        return FMcpToolCallResult::CreateTextResult(ResultJson);
    });
}

bool FN2CMcpCreateGetMemberVariableNode::ParseArguments(
    const TSharedPtr<FJsonObject>& Arguments,
    FString& OutVariableName,
    FVector2D& OutLocation,
    FString& OutError)
{
    if (!Arguments.IsValid())
    {
        OutError = TEXT("Invalid arguments object");
        return false;
    }
    
    FN2CMcpArgumentParser ArgParser(Arguments);
    
    // Required: variableName
    if (!ArgParser.TryGetRequiredString(TEXT("variableName"), OutVariableName, OutError, false))
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

const FBPVariableDescription* FN2CMcpCreateGetMemberVariableNode::FindMemberVariable(
    UBlueprint* Blueprint,
    const FString& VariableName,
    FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Invalid Blueprint");
        return nullptr;
    }
    
    // Search for the variable in the Blueprint's member variables
    for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
    {
        if (VarDesc.VarName.ToString() == VariableName)
        {
            return &VarDesc;
        }
    }
    
    OutError = FString::Printf(TEXT("Variable '%s' not found in Blueprint '%s'"), 
        *VariableName, *Blueprint->GetName());
    return nullptr;
}

UK2Node_VariableGet* FN2CMcpCreateGetMemberVariableNode::CreateGetNode(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FBPVariableDescription& Variable,
    const FVector2D& Location)
{
    // Create a new K2Node_VariableGet
    UK2Node_VariableGet* NewNode = NewObject<UK2Node_VariableGet>(Graph);
    if (!NewNode)
    {
        return nullptr;
    }
    
    // Add node to graph
    Graph->AddNode(NewNode, true);
    
    // Set the variable reference
    NewNode->VariableReference.SetSelfMember(Variable.VarName);
    
    // Set position
    NewNode->NodePosX = Location.X;
    NewNode->NodePosY = Location.Y;
    
    // Allocate default pins
    NewNode->AllocateDefaultPins();
    
    // Reconstruct the node to ensure proper setup
    NewNode->ReconstructNode();
    
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Created Get node for variable '%s' at (%.2f, %.2f)"),
            *Variable.VarName.ToString(), Location.X, Location.Y),
        EN2CLogSeverity::Debug
    );
    
    return NewNode;
}

TSharedPtr<FJsonObject> FN2CMcpCreateGetMemberVariableNode::BuildSuccessResult(
    UK2Node_VariableGet* GetNode,
    const FBPVariableDescription& Variable,
    UBlueprint* Blueprint,
    UEdGraph* Graph)
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    
    // Generate a unique ID for the node that can be used with connect-pins
    static int32 GetNodeCounter = 0;
    FString NodeId = FString::Printf(TEXT("GetNode_%s_%d"), *Variable.VarName.ToString(), ++GetNodeCounter);
    
    // Store the node ID in a map for later reference (similar to AddBlueprintNodeTool)
    // Note: In a real implementation, you'd want to store this mapping somewhere persistent
    
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("nodeId"), NodeId);
    Result->SetStringField(TEXT("nodeType"), TEXT("K2Node_VariableGet"));
    Result->SetStringField(TEXT("variableName"), Variable.VarName.ToString());
    Result->SetStringField(TEXT("graphName"), Graph->GetName());
    Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
    
    // Add location
    TSharedPtr<FJsonObject> LocationObj = MakeShareable(new FJsonObject);
    LocationObj->SetNumberField(TEXT("x"), GetNode->NodePosX);
    LocationObj->SetNumberField(TEXT("y"), GetNode->NodePosY);
    Result->SetObjectField(TEXT("location"), LocationObj);
    
    // Add pin information
    TArray<TSharedPtr<FJsonValue>> Pins = GetNodePins(GetNode);
    Result->SetArrayField(TEXT("pins"), Pins);
    
    // Add variable type information
    TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);
    TypeInfo->SetStringField(TEXT("category"), Variable.VarType.PinCategory.ToString());
    if (!Variable.VarType.PinSubCategory.IsNone())
    {
        TypeInfo->SetStringField(TEXT("subCategory"), Variable.VarType.PinSubCategory.ToString());
    }
    if (Variable.VarType.PinSubCategoryObject.IsValid())
    {
        TypeInfo->SetStringField(TEXT("typeObject"), Variable.VarType.PinSubCategoryObject->GetPathName());
        TypeInfo->SetStringField(TEXT("typeName"), Variable.VarType.PinSubCategoryObject->GetName());
    }
    Result->SetObjectField(TEXT("variableType"), TypeInfo);
    
    Result->SetStringField(TEXT("message"), FString::Printf(
        TEXT("Successfully created Get node for variable '%s' in graph '%s'"),
        *Variable.VarName.ToString(), *Graph->GetName()
    ));
    
    return Result;
}

TArray<TSharedPtr<FJsonValue>> FN2CMcpCreateGetMemberVariableNode::GetNodePins(UK2Node_VariableGet* GetNode)
{
    TArray<TSharedPtr<FJsonValue>> PinsArray;
    
    if (!GetNode)
    {
        return PinsArray;
    }
    
    for (UEdGraphPin* Pin : GetNode->Pins)
    {
        if (!Pin)
        {
            continue;
        }
        
        TSharedPtr<FJsonObject> PinInfo = MakeShareable(new FJsonObject);
        PinInfo->SetStringField(TEXT("id"), Pin->PinId.ToString());
        PinInfo->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinInfo->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
        PinInfo->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
        
        // Add friendly name for easier identification
        if (Pin->PinFriendlyName.IsEmpty())
        {
            PinInfo->SetStringField(TEXT("friendlyName"), Pin->PinName.ToString());
        }
        else
        {
            PinInfo->SetStringField(TEXT("friendlyName"), Pin->PinFriendlyName.ToString());
        }
        
        PinsArray.Add(MakeShareable(new FJsonValueObject(PinInfo)));
    }
    
    return PinsArray;
}