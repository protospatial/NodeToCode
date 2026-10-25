// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRemoveFunctionReturnPinTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
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
		TEXT("Removes a return value from the currently focused Blueprint function")
	,

		TEXT("Blueprint Function Management")

	);

	// Build input schema
	TMap<FString, FString> Properties;
	Properties.Add(TEXT("pinName"), TEXT("string"));
	
	TArray<FString> Required;
	Required.Add(TEXT("pinName"));
	
	Definition.InputSchema = BuildInputSchema(Properties, Required);
	
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

		// Find the function result node
		UK2Node_FunctionResult* FunctionResult = FN2CMcpFunctionPinUtils::FindFunctionResultNode(FocusedGraph);
		if (!FunctionResult)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("No function result node found. This function has no return values."));
		}

		// Find the Blueprint
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionResult);
		if (!Blueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Cannot find Blueprint for function"));
		}

		// Start a transaction for undo/redo
		const FScopedTransaction Transaction(
			FText::Format(NSLOCTEXT("NodeToCode", "RemoveFunctionReturnPin", "Remove Return Pin '{0}'"), 
			FText::FromString(PinName))
		);

		FunctionResult->Modify();

		// Remove the pin using our utility
		FString RemovalError;
		if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(FunctionResult, PinName, RemovalError))
		{
			return FMcpToolCallResult::CreateErrorResult(RemovalError);
		}

		// Update all function call sites
		FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);

		// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);

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
		
		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}