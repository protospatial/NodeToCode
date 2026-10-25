// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRemoveFunctionReturnPinTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
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

REGISTER_MCP_TOOL(FN2CMcpRemoveFunctionReturnPinTool)

FMcpToolDefinition FN2CMcpRemoveFunctionReturnPinTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("remove-function-return-pin"),
		TEXT("Removes a return value from a Blueprint function: the one blueprintPath plus graphName or graphGuid name, else the focused function")
	,

		TEXT("Blueprint Function Management")

	);

	// Build input schema
	TMap<FString, FString> Properties;
	Properties.Add(TEXT("pinName"), TEXT("string"));
	
	TArray<FString> Required;
	Required.Add(TEXT("pinName"));
	
	Definition.InputSchema = BuildInputSchema(Properties, Required);
	
	FN2CGraphTarget::AddSchemaProperties(Definition);

	return Definition;
}

FMcpToolCallResult FN2CMcpRemoveFunctionReturnPinTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
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

		// Validates first (no result node, a misspelled name: no transaction, package clean), then in one transaction
		// removes the pin from every Return node, regenerates the skeleton and rebuilds the call sites (card 0156)
		FN2CFunctionPinEdit Edit;
		if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(Target.Blueprint, FocusedGraph, /*bReturnValue*/ true, PinName,
			FText::Format(NSLOCTEXT("NodeToCode", "RemoveFunctionReturnPin", "Remove Return Pin '{0}'"), FText::FromString(PinName)), Edit))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(Edit.Error));
		}

		// Show notification
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("NodeToCode", "ReturnPinRemoved", "Return pin '{0}' removed from function '{1}'"),
			FText::FromString(PinName),
			FText::FromString(FocusedGraph->GetName())
		));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		// Build and return success result
		TSharedPtr<FJsonObject> ResultJson = FN2CMcpFunctionPinUtils::BuildPinRemovalSuccessResult(
			FocusedGraph, PinName, true /* bIsReturnPin */);
		
		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);
		
		return Target.Finish(FMcpToolCallResult::CreateTextResult(ResultString));
	});
}