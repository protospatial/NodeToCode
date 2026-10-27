// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSetLocalFunctionVariableDefaultValue.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpVariableDefaults.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_FunctionEntry.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpSetLocalFunctionVariableDefaultValue)

FMcpToolDefinition FN2CMcpSetLocalFunctionVariableDefaultValue::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("set-local-function-variable-default-value"),
        TEXT("Set the default value of a local variable of a Blueprint function (the one blueprintPath plus graphName or graphGuid name, else the focused function), like editing in the details panel. The value is parsed as the local's type and refused with the reason when it does not parse; an enum takes an entry's name, display name or index, and an empty string clears the default.")
    ,

    	N2CMcpToolsets::BlueprintVariable

    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("defaultValue"), TEXT("string"));
    
    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    Required.Add(TEXT("defaultValue"));
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    FN2CGraphTarget::AddSchemaProperties(Definition);
    
    // Unknown arguments are refused, so a misspelled option is not ignored (card 0194)
    SetStrictArguments(Definition);

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
        if (!Parser.TryGetRequiredIdentifier(TEXT("variableName"), VariableName, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        // An empty value clears the default (card 0283)
        if (!Parser.TryGetRequiredString(TEXT("defaultValue"), DefaultValue, ErrorMsg, /*bAllowEmpty*/ true))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        
        // The function blueprintPath / graphName / graphGuid name, else the focused graph (card 0274); read-only
        // graphs are refused
        const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
        if (!Target.IsValid())
        {
            return FMcpToolCallResult::CreateErrorResult(Target.Error);
        }
        UBlueprint* FocusedBlueprint = Target.Blueprint;
        UEdGraph* FocusedGraph = Target.Graph;

        // Find the function entry node
        UK2Node_FunctionEntry* FunctionEntryNode = FN2CMcpVariableUtils::FindFunctionEntryNode(FocusedGraph);
        if (!FunctionEntryNode)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(
                TEXT("NOT_A_FUNCTION_GRAPH: the graph is not a function graph. Pass a function as graphName or graphGuid, or open one in the Blueprint editor.")
            ));
        }

        // Find the local variable
        FBPVariableDescription* LocalVarDesc = FN2CMcpVariableUtils::FindLocalVariable(FunctionEntryNode, VariableName);
        if (!LocalVarDesc)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Local variable '%s' not found in function '%s'"),
                    *VariableName, *FN2CMcpVariableUtils::GetFunctionGraph(FocusedGraph)->GetName())
            ));
        }
        
        // Parsed against the skeleton function's property before anything changes; the stored text is what the
        // editor stores for that value (card 0283)
        UEdGraph* FunctionGraph = FN2CMcpVariableUtils::GetFunctionGraph(FocusedGraph);
        const FName LocalName = LocalVarDesc->VarName;
        FString StoredValue;
        FString ParseError;
        if (!FN2CMcpVariableDefaults::PrepareLocalDefault(FocusedBlueprint, FunctionGraph, FunctionEntryNode, LocalName, DefaultValue, StoredValue, ParseError))
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Invalid default value for local variable '%s': %s"), *VariableName, *ParseError)));
        }
        // PrepareLocalDefault may have compiled: find the description again
        LocalVarDesc = FN2CMcpVariableUtils::FindLocalVariable(FunctionEntryNode, LocalName.ToString());
        if (!LocalVarDesc)
        {
            return Target.Finish(FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Local variable '%s' not found after compiling"), *VariableName)));
        }
        
        // Create a transaction for undo/redo
        TOptional<FScopedTransaction> Transaction(InPlace, NSLOCTEXT("MCP", "SetLocalVariableDefaultValue", "Set Local Variable Default Value"));
        FunctionEntryNode->Modify();
        FocusedBlueprint->Modify();
        
        // Store the old value for comparison
        FString OldValue = LocalVarDesc->DefaultValue;
        
        // Set the new default value
        LocalVarDesc->DefaultValue = StoredValue;
        
        // Update the function variable cache to ensure the default value is properly applied
        FunctionEntryNode->RefreshFunctionVariableCache();
        
        FN2CMcpBlueprintUtils::MarkModified(FocusedBlueprint, /*bStructural*/ false);

        // Close the undo transaction, then compile once and report what the compiler said (card 0151)
        Transaction.Reset();
        FN2CCompileDiagnostics Diagnostics;
        FN2CMcpBlueprintUtils::Compile(FocusedBlueprint, Diagnostics);
        const int32 ErrorCount = Diagnostics.ErrorCount;
        const int32 WarningCount = Diagnostics.WarningCount;
        const float CompilationTime = Diagnostics.CompileSeconds;
        const bool bCompileSuccess = Diagnostics.bSuccess;

        // Build the result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        
        if (bCompileSuccess)
        {
            Result->SetStringField(TEXT("message"), 
                FString::Printf(TEXT("Successfully set default value for local variable '%s' from '%s' to '%s'"), 
                    *VariableName, *OldValue, *StoredValue));
        }
        else
        {
            Result->SetStringField(TEXT("message"), 
                FString::Printf(TEXT("Set default value for local variable '%s' but compilation failed with %d errors and %d warnings"), 
                    *VariableName, ErrorCount, WarningCount));
        }
        
        Result->SetStringField(TEXT("variableName"), VariableName);
        Result->SetStringField(TEXT("functionName"), FN2CMcpVariableUtils::GetFunctionGraph(FocusedGraph)->GetName());
        Result->SetStringField(TEXT("oldValue"), OldValue);
        Result->SetStringField(TEXT("newValue"), StoredValue);
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
        
        return Target.Finish(FMcpToolCallResult::CreateTextResult(OutputString));
    });
}
