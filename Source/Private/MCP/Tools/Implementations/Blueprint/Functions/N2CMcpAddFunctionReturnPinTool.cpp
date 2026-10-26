// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpAddFunctionReturnPinTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
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
		TEXT("Adds a new return value to a Blueprint function: the one blueprintPath plus graphName or graphGuid name, else the focused function")
	,

		N2CMcpToolsets::BlueprintFunction

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
	
	FN2CGraphTarget::AddSchemaProperties(Definition);

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

		// The function blueprintPath / graphName / graphGuid name, else the focused graph (card 0152); every result
		// after this says which graph was used ('target')
		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
		if (!Target.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Target.Error);
		}
		UEdGraph* FocusedGraph = Target.Graph;
		if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(Target.Blueprint, FocusedGraph))
		{
			// Before any change: an event graph never gets a Return node added (the old focused-graph path did that)
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("NOT_A_FUNCTION_GRAPH: %s (%s graph) of %s is not a function. Pass the function as graphName or graphGuid (its functionGuid), or focus it."),
				*FocusedGraph->GetName(), *FN2CGraphTarget::GraphKind(FocusedGraph), *Target.Blueprint->GetName())));
		}

		// Check if this is a K2 graph
		if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("The focused graph is not a Blueprint graph")));
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
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(
				FString::Printf(TEXT("Failed to resolve type '%s': %s"), *TypeIdentifier, *ResolveError)));
		}

		// Validates, then in one transaction adds the pin to every Return node (creating one, wired from the entry, when
		// the function has none), regenerates the skeleton and rebuilds the call sites (card 0156)
		FN2CFunctionPinEdit Edit;
		if (!FN2CMcpFunctionPinUtils::AddFunctionPin(Target.Blueprint, FocusedGraph, /*bReturnValue*/ true, PinName, PinType,
			FString(), Tooltip, FText::Format(NSLOCTEXT("NodeToCode", "AddFunctionReturnPin", "Add Return Pin '{0}'"),
			FText::FromString(PinName)), Edit))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(Edit.Error));
		}
		UEdGraphPin* NewPin = Edit.Pin;

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
		
		return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultString));
	});
}
