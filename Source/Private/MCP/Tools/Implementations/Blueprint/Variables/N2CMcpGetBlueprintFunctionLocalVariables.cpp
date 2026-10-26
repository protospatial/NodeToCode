// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetBlueprintFunctionLocalVariables.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Serialization/JsonSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetBlueprintFunctionLocalVariables)

FMcpToolDefinition FN2CMcpGetBlueprintFunctionLocalVariables::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("get-blueprint-function-local-variables"),
        TEXT("Extract local variables from the currently focused Blueprint function graph")
    ,

    	N2CMcpToolsets::BlueprintVariable

    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("includeDetails"), TEXT("boolean"));
    
    TArray<FString> Required; // No required fields
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    // Results carry Blueprint text (local variable names, categories and default values): marked as untrusted data (card 0124)
    Definition.bReturnsUntrustedContent = true;

    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

    return Definition;
}

FMcpToolCallResult FN2CMcpGetBlueprintFunctionLocalVariables::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FN2CMcpArgumentParser Parser(Arguments);
        bool bIncludeDetails = Parser.GetOptionalBool(TEXT("includeDetails"), true);
        
        // Get the focused graph
        UBlueprint* FocusedBlueprint = nullptr;
        UEdGraph* FocusedGraph = nullptr;
        FString ErrorMessage;
        
        if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(FocusedBlueprint, FocusedGraph, ErrorMessage))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMessage);
        }
        
        // Find the function entry node to verify this is a function graph
        UK2Node_FunctionEntry* FunctionEntryNode = FN2CMcpVariableUtils::FindFunctionEntryNode(FocusedGraph);
        if (!FunctionEntryNode)
        {
            return FMcpToolCallResult::CreateErrorResult(
                TEXT("The focused graph is not a function graph. Please focus on a function in the Blueprint editor.")
            );
        }
        
        // Build the result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("blueprintName"), FocusedBlueprint->GetName());
        Result->SetStringField(TEXT("functionName"), FN2CMcpVariableUtils::GetFunctionGraph(FocusedGraph)->GetName());
        Result->SetNumberField(TEXT("localVariableCount"), FunctionEntryNode->LocalVariables.Num());
        
        // Create array of local variables
        TArray<TSharedPtr<FJsonValue>> LocalVariablesArray;
        
        for (const FBPVariableDescription& LocalVar : FunctionEntryNode->LocalVariables)
        {
            TSharedPtr<FJsonObject> VarObj = MakeShareable(new FJsonObject);
            
            // Basic info
            VarObj->SetStringField(TEXT("name"), LocalVar.VarName.ToString());
            VarObj->SetStringField(TEXT("displayName"), LocalVar.FriendlyName.IsEmpty() ? LocalVar.VarName.ToString() : LocalVar.FriendlyName);
            VarObj->SetStringField(TEXT("category"), LocalVar.Category.ToString());
            
            if (bIncludeDetails)
            {
                // Type information
                TSharedPtr<FJsonObject> TypeObj = MakeShareable(new FJsonObject);
                TypeObj->SetStringField(TEXT("typeName"), LocalVar.VarType.PinCategory.ToString());
                TypeObj->SetStringField(TEXT("subCategory"), LocalVar.VarType.PinSubCategory.ToString());
                
                if (LocalVar.VarType.PinSubCategoryObject.IsValid())
                {
                    TypeObj->SetStringField(TEXT("subCategoryObject"), LocalVar.VarType.PinSubCategoryObject->GetPathName());
                }
                
                TypeObj->SetBoolField(TEXT("isArray"), LocalVar.VarType.IsArray());
                TypeObj->SetBoolField(TEXT("isSet"), LocalVar.VarType.IsSet());
                TypeObj->SetBoolField(TEXT("isMap"), LocalVar.VarType.IsMap());
                TypeObj->SetBoolField(TEXT("isReference"), LocalVar.VarType.bIsReference);
                TypeObj->SetBoolField(TEXT("isConst"), LocalVar.VarType.bIsConst);
                
                VarObj->SetObjectField(TEXT("type"), TypeObj);
                
                // Default value
                VarObj->SetStringField(TEXT("defaultValue"), LocalVar.DefaultValue);
                
                // Additional metadata
                VarObj->SetStringField(TEXT("guid"), LocalVar.VarGuid.ToString());
                
                // Property flags
                TArray<FString> Flags;
                if (LocalVar.PropertyFlags & CPF_Edit) Flags.Add(TEXT("Edit"));
                if (LocalVar.PropertyFlags & CPF_BlueprintVisible) Flags.Add(TEXT("BlueprintVisible"));
                if (LocalVar.PropertyFlags & CPF_BlueprintReadOnly) Flags.Add(TEXT("BlueprintReadOnly"));
                if (LocalVar.PropertyFlags & CPF_ExposeOnSpawn) Flags.Add(TEXT("ExposeOnSpawn"));
                if (LocalVar.PropertyFlags & CPF_Transient) Flags.Add(TEXT("Transient"));
                if (LocalVar.PropertyFlags & CPF_SaveGame) Flags.Add(TEXT("SaveGame"));
                if (LocalVar.PropertyFlags & CPF_AdvancedDisplay) Flags.Add(TEXT("AdvancedDisplay"));
                if (LocalVar.PropertyFlags & CPF_Deprecated) Flags.Add(TEXT("Deprecated"));
                if (LocalVar.PropertyFlags & CPF_Config) Flags.Add(TEXT("Config"));
                
                TArray<TSharedPtr<FJsonValue>> FlagsArray;
                for (const FString& Flag : Flags)
                {
                    FlagsArray.Add(MakeShareable(new FJsonValueString(Flag)));
                }
                VarObj->SetArrayField(TEXT("flags"), FlagsArray);
                
                // Replication condition
                VarObj->SetStringField(TEXT("replicationCondition"), StaticEnum<ELifetimeCondition>()->GetNameStringByValue((int64)LocalVar.ReplicationCondition));
            }
            
            LocalVariablesArray.Add(MakeShareable(new FJsonValueObject(VarObj)));
        }
        
        Result->SetArrayField(TEXT("localVariables"), LocalVariablesArray);
        
        // Convert JSON object to string for text result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}
