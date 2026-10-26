// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateGetMemberVariableNode.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
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
        TEXT("Creates a Get node for a member variable in the focused blueprint graph: its own, an inherited, a native (C++) or a component variable. The result's nodeId is the node GUID, for connect-pins.")
    ,

    	N2CMcpToolsets::BlueprintVariable

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
        if (!Arguments.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(TEXT("Invalid arguments object"));
        }
        FN2CMcpArgumentParser ArgParser(Arguments);
        FString VariableName;
        FString ParseError;
        if (!ArgParser.TryGetRequiredString(TEXT("variableName"), VariableName, ParseError, false))
        {
            return FMcpToolCallResult::CreateErrorResult(ParseError);
        }
        FVector2D Location(0, 0);
        const TSharedPtr<FJsonObject> LocationObject = ArgParser.GetOptionalObject(TEXT("location"));
        if (LocationObject.IsValid())
        {
            FN2CMcpArgumentParser LocationParser(LocationObject);
            Location.X = LocationParser.GetOptionalNumber(TEXT("x"), 0.0);
            Location.Y = LocationParser.GetOptionalNumber(TEXT("y"), 0.0);
        }

        // The focused graph through the shared resolver (no target arguments, card 0140); read-only graphs are refused
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs(), /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }

        // Spawned through UBlueprintVariableNodeSpawner in one transaction, with a node GUID and one set of pins
        // (card 0155); the Blueprint compiles once when the call ends (card 0151)
        FString Error;
        const TSharedPtr<FJsonObject> Result = FN2CMcpNodeSpawn::CreateMemberVariableNode(Target.Blueprint, Target.Graph,
            VariableName, /*bSet*/ false, Location, FString(), Error);
        if (!Result.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Error);
        }

        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "GetNodeCreated", "Created Get node for variable '{0}'"),
            FText::FromString(VariableName)
        ));
        Info.ExpireDuration = 3.0f;
        Info.bFireAndForget = true;
        Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle"));
        FSlateNotificationManager::Get().AddNotification(Info);

        FString ResultJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultJson);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return FMcpToolCallResult::CreateTextResult(ResultJson);
    });
}
