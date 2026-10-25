// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateVariableTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Utils/N2CLogger.h"
#include "Core/N2CEditorIntegration.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "BlueprintEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpCreateVariableTool)

FMcpToolDefinition FN2CMcpCreateVariableTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("create-variable"),
		TEXT("Creates a new member variable in the focused Blueprint (the Blueprint of the active Blueprint editor; no argument selects another Blueprint). For map variables: 'typeIdentifier' specifies the map's VALUE type, and 'mapKeyTypeIdentifier' (added by common schema utils) specifies the map's KEY type.")
	,
		TEXT("Blueprint Variable Management")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// variableName property
	TSharedPtr<FJsonObject> VariableNameProp = MakeShareable(new FJsonObject);
	VariableNameProp->SetStringField(TEXT("type"), TEXT("string"));
	VariableNameProp->SetStringField(TEXT("description"), TEXT("Name for the new variable"));
	Properties->SetObjectField(TEXT("variableName"), VariableNameProp);

	// typeIdentifier property (VALUE type for maps)
	TSharedPtr<FJsonObject> TypeIdentifierProp = MakeShareable(new FJsonObject);
	TypeIdentifierProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeIdentifierProp->SetStringField(TEXT("description"), TEXT("Type identifier for the variable. For non-container types, this is the variable's type (e.g., 'bool', 'FVector', '/Script/Engine.Actor'). Classes, structs and enums can be given by name, with or without the C++ prefix ('Actor', 'AActor', 'HitResult', 'FHitResult', 'ECollisionChannel', a user struct such as 'S_Loot'), or by path ('/Script/Engine.HitResult', '/Game/Data/S_Loot.S_Loot'); a name that matches more than one type needs its path. wildcard, exec and delegate types and C++ template spellings such as TArray<...> are refused. For 'array' or 'set' containers, this is the element type. For 'map' containers, this specifies the map's VALUE type; the KEY type is specified by 'mapKeyTypeIdentifier'."));
	Properties->SetObjectField(TEXT("typeIdentifier"), TypeIdentifierProp);

	// defaultValue property
	TSharedPtr<FJsonObject> DefaultValueProp = MakeShareable(new FJsonObject);
	DefaultValueProp->SetStringField(TEXT("type"), TEXT("string"));
	DefaultValueProp->SetStringField(TEXT("description"), TEXT("Optional default value for the variable"));
	DefaultValueProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("defaultValue"), DefaultValueProp);

	// category property
	TSharedPtr<FJsonObject> CategoryProp = MakeShareable(new FJsonObject);
	CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	CategoryProp->SetStringField(TEXT("description"), TEXT("Category to organize the variable in"));
	CategoryProp->SetStringField(TEXT("default"), TEXT("Default"));
	Properties->SetObjectField(TEXT("category"), CategoryProp);

	// isInstanceEditable property
	TSharedPtr<FJsonObject> InstanceEditableProp = MakeShareable(new FJsonObject);
	InstanceEditableProp->SetStringField(TEXT("type"), TEXT("boolean"));
	InstanceEditableProp->SetStringField(TEXT("description"), TEXT("Whether the variable can be edited per-instance"));
	InstanceEditableProp->SetBoolField(TEXT("default"), true);
	Properties->SetObjectField(TEXT("isInstanceEditable"), InstanceEditableProp);

	// isBlueprintReadOnly property
	TSharedPtr<FJsonObject> BlueprintReadOnlyProp = MakeShareable(new FJsonObject);
	BlueprintReadOnlyProp->SetStringField(TEXT("type"), TEXT("boolean"));
	BlueprintReadOnlyProp->SetStringField(TEXT("description"), TEXT("Whether the variable is read-only in Blueprint graphs"));
	BlueprintReadOnlyProp->SetBoolField(TEXT("default"), false);
	Properties->SetObjectField(TEXT("isBlueprintReadOnly"), BlueprintReadOnlyProp);

	// tooltip property
	TSharedPtr<FJsonObject> TooltipProp = MakeShareable(new FJsonObject);
	TooltipProp->SetStringField(TEXT("type"), TEXT("string"));
	TooltipProp->SetStringField(TEXT("description"), TEXT("Tooltip description for the variable"));
	TooltipProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("tooltip"), TooltipProp);

	// replicationCondition property
	TSharedPtr<FJsonObject> ReplicationProp = MakeShareable(new FJsonObject);
	ReplicationProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> ReplicationEnum;
	ReplicationEnum.Add(MakeShareable(new FJsonValueString(TEXT("none"))));
	ReplicationEnum.Add(MakeShareable(new FJsonValueString(TEXT("replicated"))));
	ReplicationEnum.Add(MakeShareable(new FJsonValueString(TEXT("repnotify"))));
	ReplicationProp->SetArrayField(TEXT("enum"), ReplicationEnum);
	ReplicationProp->SetStringField(TEXT("default"), TEXT("none"));
	ReplicationProp->SetStringField(TEXT("description"), TEXT("Network replication setting"));
	Properties->SetObjectField(TEXT("replicationCondition"), ReplicationProp);

	// Add container type properties (includes mapKeyTypeIdentifier)
	FN2CMcpVariableUtils::AddContainerTypeSchemaProperties(Properties);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("variableName"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("typeIdentifier"))));
	// mapKeyTypeIdentifier will be conditionally required by logic if containerType is 'map'
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpCreateVariableTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FVariableCreationSettings Settings;
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString ErrorMsg;
		
		if (!ArgParser.TryGetRequiredName(TEXT("variableName"), Settings.VariableName, ErrorMsg))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}
		
		if (!ArgParser.TryGetRequiredName(TEXT("typeIdentifier"), Settings.TypeIdentifier, ErrorMsg))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}
		
		// Optional fields
		Settings.DefaultValue = ArgParser.GetOptionalString(TEXT("defaultValue"));
		Settings.Category = ArgParser.GetOptionalString(TEXT("category"), TEXT("Default"));
		Settings.bInstanceEditable = ArgParser.GetOptionalBool(TEXT("isInstanceEditable"), true);
		Settings.bBlueprintReadOnly = ArgParser.GetOptionalBool(TEXT("isBlueprintReadOnly"), false);
		Settings.Tooltip = ArgParser.GetOptionalString(TEXT("tooltip"));
		Settings.ReplicationCondition = ArgParser.GetOptionalString(TEXT("replicationCondition"), TEXT("none"));
		
		// Container type fields
		FString ContainerType, ParsedMapKeyTypeIdentifier;
		FN2CMcpVariableUtils::ParseContainerTypeArguments(ArgParser, ContainerType, ParsedMapKeyTypeIdentifier);
		
		// Validate variable name
		FString ValidationError;
		if (!ValidateVariableName(Settings.VariableName, ValidationError))
		{
			return FMcpToolCallResult::CreateErrorResult(ValidationError);
		}
		
		// Get active Blueprint
		FString ResolveError;
		UBlueprint* ActiveBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(TEXT(""), ResolveError); // Empty path gets focused
		if (!ActiveBlueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(ResolveError);
		}
		
		// Validate Blueprint is modifiable
		if (!ValidateBlueprintModifiable(ActiveBlueprint, ValidationError))
		{
			return FMcpToolCallResult::CreateErrorResult(ValidationError);
		}
		
		// Validate container type and key type combination
		FString ContainerValidationError;
		if (!FN2CMcpVariableUtils::ValidateContainerTypeParameters(ContainerType, ParsedMapKeyTypeIdentifier, ContainerValidationError))
		{
			return FMcpToolCallResult::CreateErrorResult(ContainerValidationError);
		}
		
		// Resolve type identifier to FEdGraphPinType
		FEdGraphPinType ResolvedPinType;
		FString TypeResolveError;
		// Settings.TypeIdentifier is the VALUE type for maps. ParsedMapKeyTypeIdentifier is the KEY type.
		if (!FN2CMcpTypeResolver::ResolvePinType(Settings.TypeIdentifier, TEXT(""), ContainerType, ParsedMapKeyTypeIdentifier, false, false, ResolvedPinType, TypeResolveError))
		{
			return FMcpToolCallResult::CreateErrorResult(TypeResolveError);
		}
		
		// Create the variable
		FName ActualVariableName = CreateVariable(ActiveBlueprint, Settings.VariableName, 
			ResolvedPinType, Settings.DefaultValue, Settings.Category);
		
		if (ActualVariableName.IsNone())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to create variable"));
		}
		
		// Apply additional properties
		ApplyVariableProperties(ActiveBlueprint, ActualVariableName, 
			Settings.bInstanceEditable, Settings.bBlueprintReadOnly, 
			Settings.Tooltip, Settings.ReplicationCondition);
		
		// Show notification
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("NodeToCode", "VariableCreated", "Variable '{0}' created successfully"),
			FText::FromName(ActualVariableName)
		));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		
		// Return success result
		TSharedPtr<FJsonObject> ResultJsonObj = BuildSuccessResult(ActiveBlueprint, Settings.VariableName, // Renamed to avoid conflict
			ActualVariableName, ResolvedPinType, ContainerType);
		
		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultJsonObj.ToSharedRef(), Writer); // Use ResultJsonObj

		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}

FName FN2CMcpCreateVariableTool::CreateVariable(UBlueprint* Blueprint, const FString& DesiredName, 
	const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Category)
{
	// Ensure name is unique
	FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, DesiredName);
	
	// Add the variable
	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, UniqueName, PinType, DefaultValue);
	
	if (bSuccess)
	{
		// Set category if provided
		if (!Category.IsEmpty() && Category != TEXT("Default"))
		{
			FBlueprintEditorUtils::SetBlueprintVariableCategory(
				Blueprint, UniqueName, nullptr, FText::FromString(Category), true);
		}

		// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);

		FN2CLogger::Get().Log(FString::Printf(
			TEXT("Created variable '%s' in Blueprint '%s'"),
			*UniqueName.ToString(), *Blueprint->GetName()), EN2CLogSeverity::Info);

		return UniqueName;
	}
	
	FN2CLogger::Get().LogError(FString::Printf(
		TEXT("Failed to create variable '%s' in Blueprint '%s'"), 
		*DesiredName, *Blueprint->GetName()));
	
	return NAME_None;
}

void FN2CMcpCreateVariableTool::ApplyVariableProperties(UBlueprint* Blueprint, FName VariableName,
	bool bInstanceEditable, bool bBlueprintReadOnly,
	const FString& Tooltip, const FString& ReplicationCondition)
{
	// Get variable property from the Blueprint's GeneratedClass
	FProperty* Property = nullptr;
	if (Blueprint->GeneratedClass)
	{
		Property = Blueprint->GeneratedClass->FindPropertyByName(VariableName);
	}
	
	if (!Property)
	{
		// Try the skeleton class if GeneratedClass didn't have it
		if (Blueprint->SkeletonGeneratedClass)
		{
			Property = Blueprint->SkeletonGeneratedClass->FindPropertyByName(VariableName);
		}
	}
	
	if (!Property)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(
			TEXT("Could not find property for variable '%s'"), *VariableName.ToString()));
		return;
	}
	
	// Instance editable
	if (bInstanceEditable)
	{
		FBlueprintEditorUtils::SetInterpFlag(Blueprint, VariableName, true);
		Property->SetPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
	}
	else
	{
		Property->ClearPropertyFlags(CPF_Edit);
	}
	
	// Blueprint read-only
	if (bBlueprintReadOnly)
	{
		Property->SetPropertyFlags(CPF_BlueprintReadOnly);
	}
	
	// Tooltip
	if (!Tooltip.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(
			Blueprint, VariableName, nullptr, TEXT("tooltip"), Tooltip);
	}
	
	// Replication
	if (ReplicationCondition == TEXT("replicated"))
	{
		Property->SetPropertyFlags(CPF_Net);
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(
			Blueprint, VariableName, nullptr, TEXT("ReplicatedUsing"), TEXT(""));
	}
	else if (ReplicationCondition == TEXT("repnotify"))
	{
		Property->SetPropertyFlags(CPF_Net | CPF_RepNotify);
		
		// Create OnRep function name
		FString OnRepFunctionName = FString::Printf(TEXT("OnRep_%s"), *VariableName.ToString());
		
		// Set the RepNotify metadata
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(
			Blueprint, VariableName, nullptr, TEXT("ReplicatedUsing"), OnRepFunctionName);
		
		// Note: Creating the actual OnRep function would require more complex logic
		// and is typically done through the Blueprint editor UI
	}
	
	// Refresh Blueprint to apply changes
	FBlueprintEditorUtils::RefreshVariables(Blueprint);
}

TSharedPtr<FJsonObject> FN2CMcpCreateVariableTool::BuildSuccessResult(const UBlueprint* Blueprint, 
	const FString& RequestedName, FName ActualName, const FEdGraphPinType& ResolvedPinType,
	const FString& ContainerType) const // Removed KeyPinType
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("variableName"), RequestedName);
	Result->SetStringField(TEXT("actualName"), ActualName.ToString());
	Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	
	// Add resolved pin type info (this will include key/value for maps)
	TSharedPtr<FJsonObject> PinTypeInfoJson = FN2CMcpVariableUtils::BuildTypeInfo(ResolvedPinType);
	Result->SetObjectField(TEXT("typeInfo"), PinTypeInfoJson);
	
	// Add container information (e.g., "map", "array", "none")
	// This is somewhat redundant if typeInfo already contains it, but explicit can be good.
	Result->SetStringField(TEXT("containerType"), ContainerType);
	
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Variable '%s' created successfully"), *ActualName.ToString()));
	
	return Result;
}

bool FN2CMcpCreateVariableTool::ValidateVariableName(const FString& VariableName, FString& OutError) const
{
	if (VariableName.IsEmpty())
	{
		OutError = TEXT("Variable name cannot be empty");
		return false;
	}
	
	// Check for valid identifier characters
	if (!FName::IsValidXName(VariableName, INVALID_OBJECTNAME_CHARACTERS))
	{
		OutError = TEXT("Variable name contains invalid characters");
		return false;
	}
	
	// Check for reserved keywords
	static const TArray<FString> ReservedKeywords = {
		TEXT("None"), TEXT("Self"), TEXT("Super"), TEXT("True"), TEXT("False"),
		TEXT("Class"), TEXT("Enum"), TEXT("Struct"), TEXT("Function"),
		TEXT("Const"), TEXT("Return"), TEXT("If"), TEXT("Else"), TEXT("For"), TEXT("While")
	};
	
	if (ReservedKeywords.Contains(VariableName))
	{
		OutError = FString::Printf(TEXT("'%s' is a reserved keyword"), *VariableName);
		return false;
	}
	
	return true;
}

bool FN2CMcpCreateVariableTool::ValidateBlueprintModifiable(const UBlueprint* Blueprint, FString& OutError) const
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}
	
	if (Blueprint->IsA<UAnimBlueprint>())
	{
		// Animation Blueprints have special variable handling
		// For now, we'll allow it but note this limitation
		FN2CLogger::Get().LogWarning(TEXT("Creating variables in Animation Blueprints may have limitations"));
	}
	
	// Check if Blueprint is read-only
	if (Blueprint->bIsRegeneratingOnLoad)
	{
		OutError = TEXT("Cannot modify Blueprint while it is regenerating");
		return false;
	}
	
	return true;
}
