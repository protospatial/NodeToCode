// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSetMemberVariableDefaultValueTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpVariableDefaults.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
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
        TEXT("Sets the default value of a member variable of a Blueprint (blueprintPath, else the focused Blueprint), on its class defaults as the Details panel does (one undo step; placed instances still on the old default follow it). The value is parsed as the variable's type and refused with the reason when it does not parse; an enum takes an entry's name, display name or index, and an empty string resets the variable to its type's default value. Adds no node.")
    ,

    	N2CMcpToolsets::BlueprintVariable

    );

    // Build input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("variableName"), TEXT("string"));
    Properties.Add(TEXT("defaultValue"), TEXT("string"));

    TArray<FString> Required;
    Required.Add(TEXT("variableName"));
    Required.Add(TEXT("defaultValue"));

    Definition.InputSchema = BuildInputSchema(Properties, Required);
    FN2CGraphTarget::AddSchemaProperties(Definition, /*bGraphArguments*/ false);

    // Unknown arguments are refused, so a misspelled option is not ignored (card 0194)
    SetStrictArguments(Definition);

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
        if (!ArgParser.TryGetRequiredIdentifier(TEXT("variableName"), VariableName, ErrorMsg))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // An empty value is the reset form (card 0283)
        FString DefaultValue;
        if (!ArgParser.TryGetRequiredString(TEXT("defaultValue"), DefaultValue, ErrorMsg, /*bAllowEmpty*/ true))
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
        }

        // The Blueprint blueprintPath names, else the focused one (card 0274)
        FString ResolveError;
        UBlueprint* FocusedBlueprint = FN2CGraphTarget::LoadBlueprint(ArgParser.GetOptionalString(TEXT("blueprintPath")), ResolveError);

        if (!FocusedBlueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(ResolveError);
        }

        if (FBlueprintEditorUtils::FindNewVariableIndex(FocusedBlueprint, FName(*VariableName)) == INDEX_NONE)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Variable '%s' not found in Blueprint '%s'"),
                    *VariableName, *FocusedBlueprint->GetName())
            );
        }

        // Written on the class defaults, parsed as the compiled property's type (card 0283)
        FN2CMcpVariableDefaults::FMemberResult Applied;
        FString SetError;
        if (!FN2CMcpVariableDefaults::SetMemberDefault(FocusedBlueprint, FName(*VariableName), DefaultValue, Applied, SetError))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Invalid default value for variable '%s': %s"), *VariableName, *SetError)
            );
        }
        const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(FocusedBlueprint, FName(*VariableName));
        const FBPVariableDescription& VarDesc = FocusedBlueprint->NewVariables[VarIndex];

        // Show notification
        FNotificationInfo Info(FText::Format(
            NSLOCTEXT("NodeToCode", "VariableDefaultValueSet", "Default value for '{0}' set successfully"),
            FText::FromName(VarDesc.VarName)
        ));
        Info.ExpireDuration = 3.0f;
        FSlateNotificationManager::Get().AddNotification(Info);

        // Build and return success result
        TSharedPtr<FJsonObject> Result = BuildSuccessResult(
            FocusedBlueprint, VarDesc.VarName.ToString(), VarDesc, Applied.OldValue, Applied.NewValue
        );
        Result->SetNumberField(TEXT("instancesUpdated"), Applied.InstancesUpdated);

        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Set default value for variable '%s' in Blueprint '%s': '%s' -> '%s'"),
                *VariableName, *FocusedBlueprint->GetName(), *Applied.OldValue, *Applied.NewValue),
            EN2CLogSeverity::Info
        );

        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
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

    // Old and new default values, both read from the class defaults
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
