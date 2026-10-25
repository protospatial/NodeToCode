// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpAddFunctionReturnPinTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionResult.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpAddFunctionReturnPinTool)

FMcpToolDefinition FN2CMcpAddFunctionReturnPinTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("add-function-return-pin"),
		TEXT("Adds a new return value to the currently focused Blueprint function")
	,

		TEXT("Blueprint Function Management")

	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// pinName property
	TSharedPtr<FJsonObject> PinNameProp = MakeShareable(new FJsonObject);
	PinNameProp->SetStringField(TEXT("type"), TEXT("string"));
	PinNameProp->SetStringField(TEXT("description"), TEXT("Name for the new return value"));
	Properties->SetObjectField(TEXT("pinName"), PinNameProp);

	// typeIdentifier property
	TSharedPtr<FJsonObject> TypeIdentifierProp = MakeShareable(new FJsonObject);
	TypeIdentifierProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeIdentifierProp->SetStringField(TEXT("description"), TEXT("Type identifier from search-variable-types (e.g., 'bool', '/Script/Engine.Actor')"));
	Properties->SetObjectField(TEXT("typeIdentifier"), TypeIdentifierProp);

	// tooltip property (optional)
	TSharedPtr<FJsonObject> TooltipProp = MakeShareable(new FJsonObject);
	TooltipProp->SetStringField(TEXT("type"), TEXT("string"));
	TooltipProp->SetStringField(TEXT("description"), TEXT("Tooltip description for the return value"));
	TooltipProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("tooltip"), TooltipProp);
	
	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required fields
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("pinName"))));
	RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("typeIdentifier"))));
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpAddFunctionReturnPinTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString Error;

		// Parse arguments
		FString PinName;
		if (!ArgParser.TryGetRequiredString(TEXT("pinName"), PinName, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}

		FString TypeIdentifier;
		if (!ArgParser.TryGetRequiredString(TEXT("typeIdentifier"), TypeIdentifier, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}

		FString Tooltip = ArgParser.GetOptionalString(TEXT("tooltip"), TEXT(""));

		// Get focused function graph
		UEdGraph* FocusedGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
		if (!FocusedGraph)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("No focused graph found. Please open a Blueprint function in the editor."));
		}

		// Check if this is a K2 graph
		if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("The focused graph is not a Blueprint graph"));
		}

		// Find or create the function result node
		UK2Node_FunctionResult* FunctionResult = FN2CMcpFunctionPinUtils::EnsureFunctionResultNode(FocusedGraph);
		if (!FunctionResult)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to find or create function result node"));
		}
		
		// Verify the node is properly initialized
		if (!FunctionResult->NodeGuid.IsValid())
		{
			UE_LOG(LogNodeToCode, Warning, TEXT("Function result node has invalid GUID, regenerating..."));
			FunctionResult->CreateNewGuid();
		}

		// Resolve type identifier to FEdGraphPinType
		FEdGraphPinType PinType;
		FString ResolveError;
		
		// For simple types, we can resolve directly
		if (!FN2CMcpTypeResolver::ResolvePinType(
			TypeIdentifier,     // Primary type
			TEXT(""),          // SubType (empty for primitives)
			TEXT("none"),      // Container type
			TEXT(""),          // Key type (for maps)
			false,             // Is reference (Blueprint functions don't support return by reference)
			false,             // Is const
			PinType,
			ResolveError))
		{
			return FMcpToolCallResult::CreateErrorResult(
				FString::Printf(TEXT("Failed to resolve type '%s': %s"), *TypeIdentifier, *ResolveError));
		}

		// Start a transaction for undo/redo
		const FScopedTransaction Transaction(
			FText::Format(NSLOCTEXT("NodeToCode", "AddFunctionReturnPin", "Add Return Pin '{0}'"), 
			FText::FromString(PinName))
		);

		// Create the return pin
		UEdGraphPin* NewPin = CreateReturnPin(FunctionResult, PinName, PinType, Tooltip);
		if (!NewPin)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to create return pin"));
		}

		// Update all function call sites
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionResult);
		if (Blueprint)
		{
			FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);
			// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
			FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
		}

		// Show notification
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("NodeToCode", "ReturnPinAdded", "Return pin '{0}' added to function '{1}'"),
			FText::FromString(PinName),
			FText::FromString(FocusedGraph->GetName())
		));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		// Build and return success result
		TSharedPtr<FJsonObject> ResultJson = FN2CMcpFunctionPinUtils::BuildPinCreationSuccessResult(
			FocusedGraph, PinName, NewPin, PinType, true /* bIsReturnPin */);
		
		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);
		
		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}

UEdGraphPin* FN2CMcpAddFunctionReturnPinTool::CreateReturnPin(UK2Node_FunctionResult* FunctionResult, 
	const FString& DesiredName, const FEdGraphPinType& PinType, 
	const FString& Tooltip) const
{
	if (!FunctionResult)
	{
		return nullptr;
	}

	// Log current state
	UE_LOG(LogNodeToCode, Verbose, TEXT("Creating return pin on node: %s (GUID: %s)"), 
		*FunctionResult->GetName(), *FunctionResult->NodeGuid.ToString());
	UE_LOG(LogNodeToCode, Verbose, TEXT("Current user defined pins count: %d"), 
		FunctionResult->UserDefinedPins.Num());

	// Check if we can create input pins (function outputs)
	FText ErrorMessage;
	if (!FunctionResult->CanCreateUserDefinedPin(PinType, EGPD_Input, ErrorMessage))
	{
		UE_LOG(LogNodeToCode, Error, TEXT("Cannot create return pin: %s"), *ErrorMessage.ToString());
		return nullptr;
	}

	// Create the pin (will auto-generate unique name if needed)
	UEdGraphPin* NewPin = FunctionResult->CreateUserDefinedPin(
		FName(*DesiredName), 
		PinType, 
		EGPD_Input,   // Input to FunctionResult = Output from function
		true          // Use unique name
	);

	if (NewPin)
	{
		// Set tooltip metadata
		if (!Tooltip.IsEmpty())
		{
			FN2CMcpFunctionPinUtils::SetPinTooltip(FunctionResult, NewPin, Tooltip);
		}
		
		UE_LOG(LogNodeToCode, Verbose, TEXT("Created return pin '%s' with actual name '%s' (ID: %s)"), 
			*DesiredName, *NewPin->PinName.ToString(), *NewPin->PinId.ToString());
		UE_LOG(LogNodeToCode, Verbose, TEXT("User defined pins count after creation: %d"), 
			FunctionResult->UserDefinedPins.Num());
	}
	else
	{
		UE_LOG(LogNodeToCode, Error, TEXT("CreateUserDefinedPin returned nullptr for pin '%s'"), *DesiredName);
	}

	return NewPin;
}