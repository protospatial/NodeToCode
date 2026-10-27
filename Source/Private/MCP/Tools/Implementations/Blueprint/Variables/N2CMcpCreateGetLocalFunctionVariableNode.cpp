// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateGetLocalFunctionVariableNode.h"
#include "MCP/Utils/N2CMcpNotification.h"
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

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpCreateGetLocalFunctionVariableNode)

FMcpToolDefinition FN2CMcpCreateGetLocalFunctionVariableNode::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("create-get-local-function-variable-node"),
        TEXT("Create a Get node for a local function variable in the currently focused Blueprint function graph")
    ,

    	N2CMcpToolsets::BlueprintVariable

    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("x"), TEXT("number"));
    Properties.Add(TEXT("y"), TEXT("number"));
    
    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpCreateGetLocalFunctionVariableNode::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        FN2CMcpArgumentParser Parser(Arguments);
        FString VariableName;
        FString ErrorMsg;
        if (!Parser.TryGetRequiredName(TEXT("variableName"), VariableName, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        const FVector2D Location(Parser.GetOptionalNumber(TEXT("x"), 0.0), Parser.GetOptionalNumber(TEXT("y"), 0.0));

        // The focused graph through the shared resolver (no target arguments, card 0140); read-only graphs are refused.
        // A collapsed graph inside a function counts as that function (card 0155)
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs(), /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }

        // Spawned through UBlueprintVariableNodeSpawner in one transaction, with a node GUID and one set of pins
        // (card 0155); the Blueprint compiles once when the call ends (card 0151)
        FString Error;
        const TSharedPtr<FJsonObject> Result = FN2CMcpNodeSpawn::CreateLocalVariableNode(Target.Blueprint, Target.Graph,
            VariableName, /*bSet*/ false, Location, FString(), Error);
        if (!Result.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Error);
        }

        FN2CMcpNotification::Show(FText::Format(
            NSLOCTEXT("NodeToCode", "GetLocalNodeCreated", "Created Get node for local variable '{0}'"),
            FText::FromString(VariableName)
        ));

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}
