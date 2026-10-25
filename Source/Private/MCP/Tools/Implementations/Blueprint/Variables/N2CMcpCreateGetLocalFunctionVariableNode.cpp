// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateGetLocalFunctionVariableNode.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_VariableGet.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpCreateGetLocalFunctionVariableNode)

FMcpToolDefinition FN2CMcpCreateGetLocalFunctionVariableNode::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("create-get-local-function-variable-node"),
        TEXT("Create a Get node for a local function variable in the currently focused Blueprint function graph")
    ,

    	TEXT("Blueprint Variable Management")

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
        // Parse arguments
        FN2CMcpArgumentParser Parser(Arguments);
        FString VariableName;
        FString ErrorMsg;
        if (!Parser.TryGetRequiredName(TEXT("variableName"), VariableName, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        double X = Parser.GetOptionalNumber(TEXT("x"), 0.0);
        double Y = Parser.GetOptionalNumber(TEXT("y"), 0.0);
        
        // Get the focused graph
        UBlueprint* FocusedBlueprint = nullptr;
        UEdGraph* FocusedGraph = nullptr;
        FString ErrorMessage;
        
        if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(FocusedBlueprint, FocusedGraph, ErrorMessage))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
        }
        
        // Find the function entry node
        UK2Node_FunctionEntry* FunctionEntryNode = FindFunctionEntryNode(FocusedGraph);
        if (!FunctionEntryNode)
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("The focused graph is not a function graph. Please open a function in the Blueprint editor.")
            );
        }
        
        // Find the local variable
        FBPVariableDescription* LocalVarDesc = FindLocalVariable(FunctionEntryNode, VariableName);
        if (!LocalVarDesc)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Local variable '%s' not found in function '%s'"), 
                    *VariableName, *FocusedGraph->GetName())
            );
        }
        
        // Create a transaction for undo/redo
        const FScopedTransaction Transaction(NSLOCTEXT("MCP", "CreateGetLocalVariableNode", "Create Get Local Variable Node"));
        FocusedGraph->Modify();
        
        // Create the Get node
        UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(FocusedGraph);
        GetNode->VariableReference.SetLocalMember(LocalVarDesc->VarName, FocusedGraph->GetFName().ToString(), LocalVarDesc->VarGuid);
        GetNode->NodePosX = X;
        GetNode->NodePosY = Y;
        
        // Create a unique GUID for the node
        GetNode->CreateNewGuid();
        
        // Add the node to the graph
        FocusedGraph->AddNode(GetNode, true);
        
        // Reconstruct the node to create its pins and ensure proper setup
        GetNode->ReconstructNode();
        
        // Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
        FN2CMcpBlueprintUtils::MarkModified(FocusedBlueprint, /*bStructural*/ false);
        
        // Show notification
        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "GetLocalNodeCreated", "Created Get node for local variable '{0}'"),
            FText::FromString(VariableName)
        ));
        Info.ExpireDuration = 3.0f;
        Info.bFireAndForget = true;
        Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle"));
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Build the result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully created Get node for local variable '%s'"), *VariableName));
        Result->SetStringField(TEXT("nodeId"), GetNode->NodeGuid.ToString());
        Result->SetStringField(TEXT("nodeClass"), GetNode->GetClass()->GetName());
        Result->SetStringField(TEXT("nodeType"), TEXT("K2Node_VariableGet"));
        Result->SetStringField(TEXT("variableName"), VariableName);
        Result->SetStringField(TEXT("functionName"), FocusedGraph->GetName());
        Result->SetStringField(TEXT("blueprintName"), FocusedBlueprint->GetName());
        Result->SetNumberField(TEXT("x"), GetNode->NodePosX);
        Result->SetNumberField(TEXT("y"), GetNode->NodePosY);
        
        // Add variable type information
        TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);
        TypeInfo->SetStringField(TEXT("category"), LocalVarDesc->VarType.PinCategory.ToString());
        if (!LocalVarDesc->VarType.PinSubCategory.IsNone())
        {
            TypeInfo->SetStringField(TEXT("subCategory"), LocalVarDesc->VarType.PinSubCategory.ToString());
        }
        if (LocalVarDesc->VarType.PinSubCategoryObject.IsValid())
        {
            TypeInfo->SetStringField(TEXT("typeObject"), LocalVarDesc->VarType.PinSubCategoryObject->GetPathName());
            TypeInfo->SetStringField(TEXT("typeName"), LocalVarDesc->VarType.PinSubCategoryObject->GetName());
        }
        TypeInfo->SetBoolField(TEXT("isArray"), LocalVarDesc->VarType.IsArray());
        TypeInfo->SetBoolField(TEXT("isSet"), LocalVarDesc->VarType.IsSet());
        TypeInfo->SetBoolField(TEXT("isMap"), LocalVarDesc->VarType.IsMap());
        Result->SetObjectField(TEXT("variableType"), TypeInfo);
        
        // Add pin information
        TArray<TSharedPtr<FJsonValue>> PinsArray;
        for (UEdGraphPin* Pin : GetNode->Pins)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
            PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
            PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString());
            
            // Add friendly name for easier identification
            if (Pin->PinFriendlyName.IsEmpty())
            {
                PinObj->SetStringField(TEXT("friendlyName"), Pin->PinName.ToString());
            }
            else
            {
                PinObj->SetStringField(TEXT("friendlyName"), Pin->PinFriendlyName.ToString());
            }
            
            PinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
        Result->SetArrayField(TEXT("pins"), PinsArray);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Created Get node for local variable '%s' at (%.2f, %.2f) in function '%s'"),
                *VariableName, X, Y, *FocusedGraph->GetName()),
            EN2CLogSeverity::Debug
        );
        
        // Convert JSON object to string for text result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

UK2Node_FunctionEntry* FN2CMcpCreateGetLocalFunctionVariableNode::FindFunctionEntryNode(UEdGraph* Graph) const
{
    if (!Graph)
    {
        return nullptr;
    }
    
    // Search through all nodes in the graph
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
        {
            return FunctionEntry;
        }
    }
    
    return nullptr;
}

FBPVariableDescription* FN2CMcpCreateGetLocalFunctionVariableNode::FindLocalVariable(UK2Node_FunctionEntry* FunctionEntry, const FString& VariableName) const
{
    if (!FunctionEntry)
    {
        return nullptr;
    }
    
    FName VarFName(*VariableName);
    
    for (FBPVariableDescription& LocalVar : FunctionEntry->LocalVariables)
    {
        if (LocalVar.VarName == VarFName)
        {
            return &LocalVar;
        }
    }
    
    return nullptr;
}