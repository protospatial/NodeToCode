// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateSetLocalFunctionVariableNode.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpCreateSetLocalFunctionVariableNode)

FMcpToolDefinition FN2CMcpCreateSetLocalFunctionVariableNode::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("create-set-local-function-variable-node"),
        TEXT("Create a Set node for a local function variable in the currently focused Blueprint graph")
    ,

    	TEXT("Blueprint Variable Management")

    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("x"), TEXT("number"));
    Properties.Add(TEXT("y"), TEXT("number"));
    Properties.Add(TEXT("inputPinValue"), TEXT("string"));
    
    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpCreateSetLocalFunctionVariableNode::Execute(const TSharedPtr<FJsonObject>& Arguments)
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
        FString InputPinValue = Parser.GetOptionalString(TEXT("inputPinValue"));
        
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
        const FScopedTransaction Transaction(NSLOCTEXT("MCP", "CreateSetLocalVariableNode", "Create Set Local Variable Node"));
        FocusedGraph->Modify();
        
        // Create the Set node
        UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(FocusedGraph);
        SetNode->VariableReference.SetLocalMember(LocalVarDesc->VarName, FocusedGraph->GetFName().ToString(), LocalVarDesc->VarGuid);
        SetNode->NodePosX = X;
        SetNode->NodePosY = Y;
        
        // Add the node to the graph
        FocusedGraph->AddNode(SetNode, true);
        
        // Reconstruct the node to create its pins
        SetNode->ReconstructNode();
        SetNode->AllocateDefaultPins();
        
        // Set the input pin value if provided
        if (!InputPinValue.IsEmpty())
        {
            UEdGraphPin* ValuePin = SetNode->FindPin(LocalVarDesc->VarName, EGPD_Input);
            if (ValuePin)
            {
                const UEdGraphSchema* Schema = SetNode->GetSchema();
                Schema->TrySetDefaultValue(*ValuePin, InputPinValue);
            }
        }
        
        // Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
        FN2CMcpBlueprintUtils::MarkModified(FocusedBlueprint, /*bStructural*/ false);
        
        // Build the result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully created Set node for local variable '%s'"), *VariableName));
        Result->SetStringField(TEXT("nodeId"), SetNode->NodeGuid.ToString());
        Result->SetStringField(TEXT("nodeClass"), SetNode->GetClass()->GetName());
        Result->SetNumberField(TEXT("x"), SetNode->NodePosX);
        Result->SetNumberField(TEXT("y"), SetNode->NodePosY);
        
        // Add pin information
        TArray<TSharedPtr<FJsonValue>> PinsArray;
        for (UEdGraphPin* Pin : SetNode->Pins)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
            PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
            PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString());
            
            if (Pin->Direction == EGPD_Input && Pin->DefaultValue.Len() > 0)
            {
                PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
            }
            
            PinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
        }
        Result->SetArrayField(TEXT("pins"), PinsArray);
        
        // Convert JSON object to string for text result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

UK2Node_FunctionEntry* FN2CMcpCreateSetLocalFunctionVariableNode::FindFunctionEntryNode(UEdGraph* Graph) const
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

FBPVariableDescription* FN2CMcpCreateSetLocalFunctionVariableNode::FindLocalVariable(UK2Node_FunctionEntry* FunctionEntry, const FString& VariableName) const
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