// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSetLocalFunctionVariableDefaultValue.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpSetLocalFunctionVariableDefaultValue)

FMcpToolDefinition FN2CMcpSetLocalFunctionVariableDefaultValue::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("set-local-function-variable-default-value"),
        TEXT("Set the default value of a local function variable (like editing in the details panel)")
    ,

    	TEXT("Blueprint Variable Management")

    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("defaultValue"), TEXT("string"));
    
    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    Required.Add(TEXT("defaultValue"));
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpSetLocalFunctionVariableDefaultValue::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FN2CMcpArgumentParser Parser(Arguments);
        FString VariableName;
        FString DefaultValue;
        FString ErrorMsg;
        if (!Parser.TryGetRequiredName(TEXT("variableName"), VariableName, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        if (!Parser.TryGetRequiredString(TEXT("defaultValue"), DefaultValue, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        
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
        const FScopedTransaction Transaction(NSLOCTEXT("MCP", "SetLocalVariableDefaultValue", "Set Local Variable Default Value"));
        FunctionEntryNode->Modify();
        FocusedBlueprint->Modify();
        
        // Store the old value for comparison
        FString OldValue = LocalVarDesc->DefaultValue;
        
        // Set the new default value
        LocalVarDesc->DefaultValue = DefaultValue;
        
        // Update the function variable cache to ensure the default value is properly applied
        FunctionEntryNode->RefreshFunctionVariableCache();
        
        // Compile Blueprint synchronously to ensure preview actors are properly updated
        FN2CMcpBlueprintUtils::MarkBlueprintAsModifiedAndCompile(FocusedBlueprint);

        // Get compilation status (already compiled by MarkBlueprintAsModifiedAndCompile)
        int32 ErrorCount = FocusedBlueprint->Status == BS_Error ? 1 : 0;
        int32 WarningCount = 0;
        float CompilationTime = 0.0f;
        bool bCompileSuccess = (ErrorCount == 0);

        // Build the result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        
        if (bCompileSuccess)
        {
            Result->SetStringField(TEXT("message"), 
                FString::Printf(TEXT("Successfully set default value for local variable '%s' from '%s' to '%s'"), 
                    *VariableName, *OldValue, *DefaultValue));
        }
        else
        {
            Result->SetStringField(TEXT("message"), 
                FString::Printf(TEXT("Set default value for local variable '%s' but compilation failed with %d errors and %d warnings"), 
                    *VariableName, ErrorCount, WarningCount));
        }
        
        Result->SetStringField(TEXT("variableName"), VariableName);
        Result->SetStringField(TEXT("functionName"), FocusedGraph->GetName());
        Result->SetStringField(TEXT("oldValue"), OldValue);
        Result->SetStringField(TEXT("newValue"), DefaultValue);
        Result->SetStringField(TEXT("variableType"), LocalVarDesc->VarType.PinCategory.ToString());
        
        if (LocalVarDesc->VarType.IsArray())
        {
            Result->SetStringField(TEXT("containerType"), TEXT("Array"));
        }
        else if (LocalVarDesc->VarType.IsSet())
        {
            Result->SetStringField(TEXT("containerType"), TEXT("Set"));
        }
        else if (LocalVarDesc->VarType.IsMap())
        {
            Result->SetStringField(TEXT("containerType"), TEXT("Map"));
        }
        else
        {
            Result->SetStringField(TEXT("containerType"), TEXT("None"));
        }
        
        // Add compilation results
        TSharedPtr<FJsonObject> CompileResults = MakeShareable(new FJsonObject);
        CompileResults->SetBoolField(TEXT("success"), bCompileSuccess);
        CompileResults->SetNumberField(TEXT("errorCount"), ErrorCount);
        CompileResults->SetNumberField(TEXT("warningCount"), WarningCount);
        CompileResults->SetNumberField(TEXT("compilationTime"), CompilationTime);
        Result->SetObjectField(TEXT("compilationResults"), CompileResults);
        
        // Convert JSON object to string for text result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

UK2Node_FunctionEntry* FN2CMcpSetLocalFunctionVariableDefaultValue::FindFunctionEntryNode(UEdGraph* Graph) const
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

FBPVariableDescription* FN2CMcpSetLocalFunctionVariableDefaultValue::FindLocalVariable(UK2Node_FunctionEntry* FunctionEntry, const FString& VariableName) const
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