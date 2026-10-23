// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpCreateLocalVariableTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
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
		TEXT("Creates a new local variable in the currently focused Blueprint function. For map variables: 'typeIdentifier' specifies the map's VALUE type, and 'mapKeyTypeIdentifier' (added by common schema utils) specifies the map's KEY type.")
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
	TypeIdentifierProp->SetStringField(TEXT("description"), TEXT("Type identifier for the variable's value. For non-container types, this is the variable's type (e.g., 'bool', 'FVector', '/Script/Engine.Actor'). For 'array' or 'set' containers, this is the element type. For 'map' containers, this specifies the map's VALUE type; the KEY type is specified by 'mapKeyTypeIdentifier'."));
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
		
		// Get focused function graph
		UBlueprint* OwningBlueprint = nullptr;
		UEdGraph* FocusedGraph = nullptr;
		FString GraphError;
		if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
		{
			return FMcpToolCallResult::CreateErrorResult(GraphError);
		}
		
		// Ensure we're in a K2 graph
		if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Focused graph is not a Blueprint graph"));
		}
		
		// Find the function entry node
		UK2Node_FunctionEntry* FunctionEntry = FindFunctionEntryNode(FocusedGraph);
		if (!FunctionEntry)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Not in a function graph. Local variables can only be created in functions, not event graphs."));
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
		
		// Create the local variable
		FName ActualVariableName = CreateLocalVariable(FunctionEntry, VariableName, ResolvedPinType, DefaultValue, Tooltip);
		
		// Build and return success result
		TSharedPtr<FJsonObject> ResultJsonObj = BuildSuccessResult(FunctionEntry, FocusedGraph, VariableName, ActualVariableName, ResolvedPinType, ContainerType); // Renamed
		
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ResultJsonObj.ToSharedRef(), Writer); // Use ResultJsonObj

        // Schedule deferred refresh of BlueprintActionDatabase
        FN2CMcpBlueprintUtils::DeferredRefreshBlueprintActionDatabase();
        
		return FMcpToolCallResult::CreateTextResult(JsonString);
	});
}

UK2Node_FunctionEntry* FN2CMcpCreateLocalVariableTool::FindFunctionEntryNode(UEdGraph* Graph) const
{
	if (!Graph)
	{
		return nullptr;
	}
	
	// Iterate through all nodes in the graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
		{
			return Entry;
		}
	}
	
	// For event graphs, check for event nodes (which don't support local variables)
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Cast<UK2Node_Event>(Node) || Cast<UK2Node_CustomEvent>(Node))
		{
			// Events don't support local variables directly
			return nullptr;
		}
	}
	
	return nullptr;
}

FName FN2CMcpCreateLocalVariableTool::MakeUniqueLocalVariableName(UK2Node_FunctionEntry* FunctionEntry, const FString& BaseName) const
{
	if (!FunctionEntry)
	{
		return FName(*BaseName);
	}
	
	FName TestName(*BaseName);
	int32 Counter = 0;
	
	// Check against existing local variables
	while (true)
	{
		bool bNameExists = false;
		
		for (const FBPVariableDescription& LocalVar : FunctionEntry->LocalVariables)
		{
			if (LocalVar.VarName == TestName)
			{
				bNameExists = true;
				break;
			}
		}
		
		if (!bNameExists)
		{
			return TestName;
		}
		
		// Try with counter
		Counter++;
		TestName = FName(*FString::Printf(TEXT("%s_%d"), *BaseName, Counter));
	}
}

FName FN2CMcpCreateLocalVariableTool::CreateLocalVariable(UK2Node_FunctionEntry* FunctionEntry, const FString& DesiredName,
	const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Tooltip)
{
	if (!FunctionEntry)
	{
		return NAME_None;
	}
	
	// Create FBPVariableDescription
	FBPVariableDescription NewVar;
	NewVar.VarName = MakeUniqueLocalVariableName(FunctionEntry, DesiredName);
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.VarType = PinType; // PinType is the fully resolved type (e.g. TMap<Key,Value>)
	NewVar.FriendlyName = DesiredName;
	NewVar.DefaultValue = DefaultValue;
	NewVar.Category = FText::FromString(TEXT("Local"));
	
	// Set metadata
	if (!Tooltip.IsEmpty())
	{
		NewVar.SetMetaData(TEXT("ToolTip"), Tooltip);
	}
	
	// Add to function entry's local variables
	FunctionEntry->LocalVariables.Add(NewVar);
	
	// Reconstruct the node to show the new local variable
	FunctionEntry->ReconstructNode();
	
	// Compile Blueprint synchronously to ensure preview actors are properly updated
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionEntry);
	if (Blueprint)
	{
		FN2CMcpBlueprintUtils::MarkBlueprintAsModifiedAndCompile(Blueprint);
	}
	
	// Show notification
	FNotificationInfo Info(FText::Format(
		LOCTEXT("LocalVariableCreated", "Local variable '{0}' created successfully"),
		FText::FromName(NewVar.VarName)
	));
	Info.ExpireDuration = 3.0f;
	Info.bFireAndForget = true;
	Info.Image = FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle"));
	FSlateNotificationManager::Get().AddNotification(Info);
	
	return NewVar.VarName;
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
