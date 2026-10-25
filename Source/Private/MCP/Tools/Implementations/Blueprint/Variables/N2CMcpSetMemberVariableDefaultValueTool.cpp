// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSetMemberVariableDefaultValueTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpSetMemberVariableDefaultValueTool)

FMcpToolDefinition FN2CMcpSetMemberVariableDefaultValueTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("set-member-variable-default-value"),
        TEXT("Sets the default value of a member variable in the focused Blueprint. This modifies the variable's default value property (as shown in the Details panel), not creating any nodes in the graph.")
    ,

    	TEXT("Blueprint Variable Management")

    );
    
    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("defaultValue"), TEXT("string"));
    
    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    Required.Add(TEXT("defaultValue"));
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpSetMemberVariableDefaultValueTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // Parse arguments
        FN2CMcpArgumentParser ArgParser(Arguments);
        FString ErrorMsg;
        
        FString VariableName;
        if (!ArgParser.TryGetRequiredString(TEXT("variableName"), VariableName, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        
        FString DefaultValue;
        if (!ArgParser.TryGetRequiredString(TEXT("defaultValue"), DefaultValue, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }
        
        // Get the currently focused Blueprint
        FString ResolveError;
        UBlueprint* FocusedBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(TEXT(""), ResolveError);
        
        if (!FocusedBlueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to get focused Blueprint: %s"), *ResolveError)
            );
        }
        
        // Find the variable
        FBPVariableDescription* VarDesc = FindVariable(FocusedBlueprint, VariableName);
        if (!VarDesc)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Variable '%s' not found in Blueprint '%s'"), 
                    *VariableName, *FocusedBlueprint->GetName())
            );
        }
        
        // Store the old default value for the result
        FString OldDefaultValue = VarDesc->DefaultValue;
        
        // Validate the new default value
        FString ValidationError;
        if (!ValidateDefaultValue(*VarDesc, DefaultValue, ValidationError))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Invalid default value for variable '%s': %s"), 
                    *VariableName, *ValidationError)
            );
        }
        
        // Apply the new default value
        if (!ApplyDefaultValue(FocusedBlueprint, *VarDesc, DefaultValue))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to set default value for variable '%s'"), *VariableName)
            );
        }
        
        // Compile once, after ApplyDefaultValue's transaction has closed, to validate the new default value (card 0151)
        FN2CCompileDiagnostics Diagnostics;
        const bool bCompileSuccess = FN2CMcpBlueprintUtils::Compile(FocusedBlueprint, Diagnostics);
        const int32 ErrorCount = Diagnostics.ErrorCount;
        const int32 WarningCount = Diagnostics.WarningCount;
        const float CompilationTime = Diagnostics.CompileSeconds;
        
        // Show notification
        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "VariableDefaultValueSet", "Default value for '{0}' set successfully"),
            FText::FromString(VariableName)
        ));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Build and return success result
        TSharedPtr<FJsonObject> Result = BuildSuccessResult(
            FocusedBlueprint, VariableName, *VarDesc, OldDefaultValue, DefaultValue
        );
        
        // Add compilation info to result
        Result->SetBoolField(TEXT("compilationSuccess"), bCompileSuccess);
        Result->SetNumberField(TEXT("compilationErrorCount"), ErrorCount);
        Result->SetNumberField(TEXT("compilationWarningCount"), WarningCount);
        Result->SetNumberField(TEXT("compilationTime"), CompilationTime);
        
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Set default value for variable '%s' in Blueprint '%s': '%s' -> '%s'"), 
                *VariableName, *FocusedBlueprint->GetName(), *OldDefaultValue, *DefaultValue),
            EN2CLogSeverity::Info
        );

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

FBPVariableDescription* FN2CMcpSetMemberVariableDefaultValueTool::FindVariable(
    UBlueprint* Blueprint, const FString& VariableName) const
{
    if (!Blueprint)
    {
        return nullptr;
    }
    
    // Search through the Blueprint's variables
    for (FBPVariableDescription& VarDesc : Blueprint->NewVariables)
    {
        if (VarDesc.VarName.ToString() == VariableName)
        {
            return &VarDesc;
        }
    }
    
    return nullptr;
}

bool FN2CMcpSetMemberVariableDefaultValueTool::ValidateDefaultValue(
    const FBPVariableDescription& VarDesc, const FString& DefaultValue, FString& OutError) const
{
    // For now, we'll do basic validation. In the future, this could be expanded
    // to do type-specific validation (e.g., validating numeric ranges, enum values, etc.)
    
    const FEdGraphPinType& PinType = VarDesc.VarType;
    
    // Empty string is always valid (represents default/zero value)
    if (DefaultValue.IsEmpty())
    {
        return true;
    }
    
    // Basic type validation
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
    {
        // Boolean values should be "true" or "false"
        if (DefaultValue != TEXT("true") && DefaultValue != TEXT("false") && 
            DefaultValue != TEXT("True") && DefaultValue != TEXT("False"))
        {
            OutError = TEXT("Boolean values must be 'true' or 'false'");
            return false;
        }
    }
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int || 
             PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
    {
        // Validate integer format
        if (!DefaultValue.IsNumeric() && DefaultValue[0] != '-')
        {
            OutError = TEXT("Integer values must be numeric");
            return false;
        }
    }
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Float || 
             PinType.PinCategory == UEdGraphSchema_K2::PC_Double)
    {
        // Validate float format - basic check
        bool bHasDecimal = false;
        for (int32 i = 0; i < DefaultValue.Len(); i++)
        {
            TCHAR Ch = DefaultValue[i];
            if (Ch == '.')
            {
                if (bHasDecimal)
                {
                    OutError = TEXT("Float values cannot have multiple decimal points");
                    return false;
                }
                bHasDecimal = true;
            }
            else if (!FChar::IsDigit(Ch) && Ch != '-' && Ch != 'f' && Ch != 'F')
            {
                OutError = TEXT("Float values must be numeric");
                return false;
            }
        }
    }
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
    {
        // Validate byte range (0-255)
        if (DefaultValue.IsNumeric())
        {
            int32 Value = FCString::Atoi(*DefaultValue);
            if (Value < 0 || Value > 255)
            {
                OutError = TEXT("Byte values must be between 0 and 255");
                return false;
            }
        }
        else
        {
            OutError = TEXT("Byte values must be numeric");
            return false;
        }
    }
    // For other types (structs, objects, etc.), we'll accept any string for now
    // The engine will validate these when compiling
    
    return true;
}

bool FN2CMcpSetMemberVariableDefaultValueTool::ApplyDefaultValue(
    UBlueprint* Blueprint, FBPVariableDescription& VarDesc, const FString& DefaultValue) const
{
    // Begin transaction
    const FScopedTransaction Transaction(NSLOCTEXT("NodeToCode", "SetVariableDefaultValue", "Set Variable Default Value"));
    
    // Modify the Blueprint
    Blueprint->Modify();
    
    // Set the new default value
    VarDesc.DefaultValue = DefaultValue;
    
    // Marked modified; Execute compiles it once after this transaction closes (card 0151)
    FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);
    
    // Refresh variables to ensure the change is applied
    FBlueprintEditorUtils::RefreshVariables(Blueprint);
    
    // Reconstruct and refresh to apply the changes
    FBlueprintEditorUtils::ReconstructAllNodes(Blueprint);
    FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
    
    return true;
}

TSharedPtr<FJsonObject> FN2CMcpSetMemberVariableDefaultValueTool::BuildSuccessResult(
    const UBlueprint* Blueprint,
    const FString& VariableName,
    const FBPVariableDescription& VarDesc,
    const FString& OldDefaultValue,
    const FString& NewDefaultValue) const
{
    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("variableName"), VariableName);
    Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
    Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
    
    // Add old and new default values
    Result->SetStringField(TEXT("oldDefaultValue"), OldDefaultValue);
    Result->SetStringField(TEXT("newDefaultValue"), NewDefaultValue);
    
    // Add type information
    TSharedPtr<FJsonObject> TypeInfo = FN2CMcpVariableUtils::BuildTypeInfo(VarDesc.VarType);
    Result->SetObjectField(TEXT("typeInfo"), TypeInfo);
    
    // Add category
    Result->SetStringField(TEXT("category"), VarDesc.Category.ToString());
    
    // Success message
    Result->SetStringField(TEXT("message"), 
        FString::Printf(TEXT("Successfully set default value for variable '%s' from '%s' to '%s'"), 
            *VariableName, 
            OldDefaultValue.IsEmpty() ? TEXT("(empty)") : *OldDefaultValue,
            NewDefaultValue.IsEmpty() ? TEXT("(empty)") : *NewDefaultValue
        )
    );
    
    return Result;
}