// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateLocalVariableTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "ScopedTransaction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintEditorModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register this tool with the MCP tool manager
REGISTER_MCP_TOOL(FN2CMcpCreateLocalVariableTool)

#define LOCTEXT_NAMESPACE "NodeToCode"

FMcpToolDefinition FN2CMcpCreateLocalVariableTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("create-local-variable"),
		TEXT("Creates a new local variable in the currently focused Blueprint function. The name is made unique against the Blueprint's members and the function's parameters and locals (a taken name gets a _0, _1... suffix; actualName is the name used). One undo removes it. For map variables: 'typeIdentifier' specifies the map's VALUE type, and 'mapKeyTypeIdentifier' (added by common schema utils) specifies the map's KEY type.")
	,
		TEXT("Blueprint Variable Management")
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// variableName property (required)
	TSharedPtr<FJsonObject> VariableNameProp = MakeShareable(new FJsonObject);
	VariableNameProp->SetStringField(TEXT("type"), TEXT("string"));
	VariableNameProp->SetStringField(TEXT("description"), TEXT("Name for the new local variable"));
	Properties->SetObjectField(TEXT("variableName"), VariableNameProp);

	// typeIdentifier property (VALUE type for maps) (required)
	TSharedPtr<FJsonObject> TypeIdentifierProp = MakeShareable(new FJsonObject);
	TypeIdentifierProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeIdentifierProp->SetStringField(TEXT("description"), TEXT("Type identifier for the variable's value. For non-container types, this is the variable's type (e.g., 'bool', 'FVector', '/Script/Engine.Actor'). Classes, structs and enums can be given by name, with or without the C++ prefix ('Actor', 'AActor', 'HitResult', 'FHitResult', 'ECollisionChannel', a user struct such as 'S_Loot'), or by path ('/Script/Engine.HitResult', '/Game/Data/S_Loot.S_Loot'); a name that matches more than one type needs its path. wildcard, exec and delegate types and C++ template spellings such as TArray<...> are refused. For 'array' or 'set' containers, this is the element type. For 'map' containers, this specifies the map's VALUE type; the KEY type is specified by 'mapKeyTypeIdentifier'."));
	Properties->SetObjectField(TEXT("typeIdentifier"), TypeIdentifierProp);

	// defaultValue property (optional)
	TSharedPtr<FJsonObject> DefaultValueProp = MakeShareable(new FJsonObject);
	DefaultValueProp->SetStringField(TEXT("type"), TEXT("string"));
	DefaultValueProp->SetStringField(TEXT("description"), TEXT("Optional default value for the variable"));
	DefaultValueProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("defaultValue"), DefaultValueProp);

	// tooltip property (optional)
	TSharedPtr<FJsonObject> TooltipProp = MakeShareable(new FJsonObject);
	TooltipProp->SetStringField(TEXT("type"), TEXT("string"));
	TooltipProp->SetStringField(TEXT("description"), TEXT("Tooltip description for the variable"));
	TooltipProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("tooltip"), TooltipProp);

	// Add container type properties (includes mapKeyTypeIdentifier)
	FN2CMcpVariableUtils::AddContainerTypeSchemaProperties(Properties);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required parameters
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShareable(new FJsonValueString(TEXT("variableName"))));
	Required.Add(MakeShareable(new FJsonValueString(TEXT("typeIdentifier"))));
	// mapKeyTypeIdentifier will be conditionally required by logic if containerType is 'map'
	Schema->SetArrayField(TEXT("required"), Required);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpCreateLocalVariableTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString ErrorMsg;

		// Extract required parameters
		FString VariableName;
		if (!ArgParser.TryGetRequiredName(TEXT("variableName"), VariableName, ErrorMsg))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}

		FString TypeIdentifier;
		if (!ArgParser.TryGetRequiredName(TEXT("typeIdentifier"), TypeIdentifier, ErrorMsg))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}

		// Optional parameters
		FString DefaultValue = ArgParser.GetOptionalString(TEXT("defaultValue"));
		FString Tooltip = ArgParser.GetOptionalString(TEXT("tooltip"));

		// Container type parameters
		FString ContainerType, ParsedMapKeyTypeIdentifier;
		FN2CMcpVariableUtils::ParseContainerTypeArguments(ArgParser, ContainerType, ParsedMapKeyTypeIdentifier);

		// The focused graph, through the shared graph resolver (card 0152). This tool takes no target arguments (card
		// 0274 adds them), so the empty target is the focused graph; a read-only graph is refused (READ_ONLY_GRAPH).
		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs(), /*bForWrite*/ true);
		if (!Target.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Target.Error);
		}
		UBlueprint* OwningBlueprint = Target.Blueprint;
		UEdGraph* FunctionGraph = FBlueprintEditorUtils::GetTopLevelGraph(Target.Graph);

		// Ensure we're in a K2 function graph (a collapsed graph inside a function counts as its function)
		if (!FunctionGraph || !FunctionGraph->GetSchema() || !FunctionGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Focused graph is not a Blueprint graph"));
		}
		if (FunctionGraph->GetSchema()->GetGraphType(FunctionGraph) != GT_Function)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("NOT_A_FUNCTION_GRAPH: Not in a function graph. Local variables can only be created in functions, not event graphs."));
		}

		// Validate container type and key type combination
		FString ContainerValidationError;
		if (!FN2CMcpVariableUtils::ValidateContainerTypeParameters(ContainerType, ParsedMapKeyTypeIdentifier, ContainerValidationError))
		{
			return FMcpToolCallResult::CreateErrorResult(ContainerValidationError);
		}

		// Resolve type identifier to FEdGraphPinType
		FEdGraphPinType ResolvedPinType; // This will be the final pin type (e.g. TMap<Key,Value>)
		FString ResolveError;
		// TypeIdentifier is the VALUE type for maps. ParsedMapKeyTypeIdentifier is the KEY type.
		if (!FN2CMcpTypeResolver::ResolvePinType(TypeIdentifier, TEXT(""), ContainerType, ParsedMapKeyTypeIdentifier, false, false, ResolvedPinType, ResolveError))
		{
			return FMcpToolCallResult::CreateErrorResult(ResolveError);
		}

		// Create the local variable, in one transaction
		FString CreateError;
		const FName ActualVariableName = AddLocalVariableToFunction(OwningBlueprint, FunctionGraph, VariableName,
			ResolvedPinType, DefaultValue, Tooltip, CreateError);
		if (ActualVariableName.IsNone())
		{
			return FMcpToolCallResult::CreateErrorResult(CreateError);
		}

		// Show notification
		FNotificationInfo Info(FText::Format(
			LOCTEXT("LocalVariableCreated", "Local variable '{0}' created successfully"),
			FText::FromName(ActualVariableName)
		));
		Info.ExpireDuration = 3.0f;
		Info.bFireAndForget = true;
		Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle"));
		FSlateNotificationManager::Get().AddNotification(Info);

		// Build and return success result
		UK2Node_FunctionEntry* FunctionEntry = nullptr;
		{
			TArray<UK2Node_FunctionEntry*> EntryNodes;
			FunctionGraph->GetNodesOfClass(EntryNodes);
			FunctionEntry = EntryNodes.Num() > 0 ? EntryNodes[0] : nullptr;
		}
		TSharedPtr<FJsonObject> ResultJsonObj = BuildSuccessResult(FunctionEntry, FunctionGraph, VariableName, ActualVariableName, ResolvedPinType, ContainerType);

		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ResultJsonObj.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(JsonString);
	});
}

FName FN2CMcpCreateLocalVariableTool::MakeUniqueLocalVariableName(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FString& BaseName)
{
	UStruct* Scope = (Blueprint && Blueprint->SkeletonGeneratedClass && FunctionGraph)
		? FindUField<UFunction>(Blueprint->SkeletonGeneratedClass, FunctionGraph->GetFName()) : nullptr;
	FName Name = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, BaseName, Scope);

	// The skeleton function lists the parameters only once it has been regenerated: also check the entry and return
	// nodes' parameters and the entry's locals, so a local never shares a parameter's name (the compiler would refuse
	// to create the second property in the function's scope)
	TArray<UK2Node_EditablePinBase*> SignatureNodes;
	if (FunctionGraph)
	{
		FunctionGraph->GetNodesOfClass(SignatureNodes);
	}
	auto IsTaken = [&SignatureNodes](const FName Candidate)
	{
		for (const UK2Node_EditablePinBase* Node : SignatureNodes)
		{
			for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
			{
				if (Pin.IsValid() && Pin->PinName == Candidate)
				{
					return true;
				}
			}
			if (const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
			{
				for (const FBPVariableDescription& Local : Entry->LocalVariables)
				{
					if (Local.VarName == Candidate)
					{
						return true;
					}
				}
			}
		}
		return false;
	};

	const FString Stem = Name.ToString();
	for (int32 Suffix = 0; IsTaken(Name) && Suffix < 1000; ++Suffix)
	{
		Name = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FString::Printf(TEXT("%s_%d"), *Stem, Suffix), Scope);
	}
	return Name;
}

FName FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(UBlueprint* Blueprint, UEdGraph* Graph, const FString& DesiredName,
	const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Tooltip, FString& OutError)
{
	UEdGraph* FunctionGraph = Graph ? FBlueprintEditorUtils::GetTopLevelGraph(Graph) : nullptr;
	if (!Blueprint || !FunctionGraph || !FunctionGraph->GetSchema()
		|| FunctionGraph->GetSchema()->GetGraphType(FunctionGraph) != GT_Function)
	{
		OutError = TEXT("NOT_A_FUNCTION_GRAPH: Not in a function graph. Local variables can only be created in functions, not event graphs.");
		return NAME_None;
	}
	TArray<UK2Node_FunctionEntry*> EntryNodes;
	FunctionGraph->GetNodesOfClass(EntryNodes);
	if (EntryNodes.Num() == 0)
	{
		OutError = FString::Printf(TEXT("NOT_A_FUNCTION_GRAPH: '%s' has no function entry node"), *FunctionGraph->GetName());
		return NAME_None;
	}
	UK2Node_FunctionEntry* FunctionEntry = EntryNodes[0];

	const FName UniqueName = MakeUniqueLocalVariableName(Blueprint, FunctionGraph, DesiredName);

	// One undo step for the variable and its tooltip (card 0154). AddLocalVariable opens its own transaction, which
	// joins this one, and calls Modify on the Blueprint and the entry node. The compile runs after this transaction
	// has closed (FN2CMcpToolCallScope, card 0151).
	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("CreateLocalVariableTransaction", "Create Local Variable '{0}'"), FText::FromName(UniqueName)));
	if (!FBlueprintEditorUtils::AddLocalVariable(Blueprint, FunctionGraph, UniqueName, PinType, DefaultValue))
	{
		Transaction.Cancel();
		OutError = FString::Printf(TEXT("Failed to create local variable '%s' in function '%s'"), *DesiredName, *FunctionGraph->GetName());
		FN2CLogger::Get().LogError(OutError);
		return NAME_None;
	}

	if (!Tooltip.IsEmpty())
	{
		FunctionEntry->Modify();
		for (FBPVariableDescription& Local : FunctionEntry->LocalVariables)
		{
			if (Local.VarName == UniqueName)
			{
				Local.SetMetaData(FBlueprintMetadata::MD_Tooltip, Tooltip);
				break;
			}
		}
	}

	// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);

	FN2CLogger::Get().Log(FString::Printf(TEXT("Created local variable '%s' in function '%s' of Blueprint '%s'"),
		*UniqueName.ToString(), *FunctionGraph->GetName(), *Blueprint->GetName()), EN2CLogSeverity::Info);
	return UniqueName;
}

TSharedPtr<FJsonObject> FN2CMcpCreateLocalVariableTool::BuildSuccessResult(UK2Node_FunctionEntry* FunctionEntry, UEdGraph* FunctionGraph,
	const FString& RequestedName, FName ActualName, const FEdGraphPinType& ResolvedPinType, const FString& ContainerType) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("variableName"), RequestedName);
	Result->SetStringField(TEXT("actualName"), ActualName.ToString());
	
	// Add type info (this will include key/value for maps)
	TSharedPtr<FJsonObject> TypeInfo = FN2CMcpVariableUtils::BuildTypeInfo(ResolvedPinType);
	Result->SetObjectField(TEXT("typeInfo"), TypeInfo);
	
	// Add container information (e.g., "map", "array", "none")
	// This is somewhat redundant if typeInfo already contains it, but explicit can be good.
	FN2CMcpVariableUtils::AddContainerInfoToResult(Result, ContainerType, true);
	
	// Add function and Blueprint info
	if (FunctionGraph)
	{
		Result->SetStringField(TEXT("functionName"), FunctionGraph->GetName());
		
		if (UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(FunctionGraph))
		{
			Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
		}
	}
	
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Local variable '%s' created successfully in function '%s'"),
		*ActualName.ToString(),
		FunctionGraph ? *FunctionGraph->GetName() : TEXT("Unknown")
	));
	
	return Result;
}

#undef LOCTEXT_NAMESPACE
