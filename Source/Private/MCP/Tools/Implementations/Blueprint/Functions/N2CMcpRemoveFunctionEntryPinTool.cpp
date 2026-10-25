// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpRemoveFunctionEntryPinTool.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpRemoveFunctionEntryPinTool)

FMcpToolDefinition FN2CMcpRemoveFunctionEntryPinTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("remove-function-entry-pin"),
		TEXT("Removes an input parameter from the focused Blueprint function (a pin on its entry node). Return values are on the result node: remove them with remove-function-return-pin.")
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

FMcpToolCallResult FN2CMcpRemoveFunctionEntryPinTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
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

		// Find the function entry node
		UK2Node_FunctionEntry* FunctionEntry = FN2CMcpFunctionPinUtils::FindFunctionEntryNode(FocusedGraph);
		if (!FunctionEntry)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("No function entry node found in the graph"));
		}

		FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveFunctionEntryPin: Found function entry node in graph '%s'"), 
			*FocusedGraph->GetName()), EN2CLogSeverity::Debug);

		// Find the Blueprint
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionEntry);
		if (!Blueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Cannot find Blueprint for function"));
		}

		FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveFunctionEntryPin: Blueprint found: %s"), 
			*Blueprint->GetName()), EN2CLogSeverity::Debug);

		// Log all existing pins on the entry node before attempting removal
		FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveFunctionEntryPin: Entry node has %d pins"), 
			FunctionEntry->Pins.Num()), EN2CLogSeverity::Debug);
		
		for (UEdGraphPin* Pin : FunctionEntry->Pins)
		{
			if (Pin)
			{
				FString DirectionStr = TEXT("Unknown");
				switch (Pin->Direction)
				{
					case EGPD_Input: DirectionStr = TEXT("Input"); break;
					case EGPD_Output: DirectionStr = TEXT("Output"); break;
					default: DirectionStr = TEXT("Unknown"); break;
				}
				
				FN2CLogger::Get().Log(FString::Printf(
					TEXT("  Pin: Name='%s', DisplayName='%s', Direction=%s, Type=%s, IsHidden=%s"),
					*Pin->PinName.ToString(), 
					*Pin->GetDisplayName().ToString(),
					*DirectionStr,
					*Pin->PinType.PinCategory.ToString(),
					Pin->bHidden ? TEXT("true") : TEXT("false")), 
					EN2CLogSeverity::Debug);
			}
		}

		// Also log user-defined pins
		FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveFunctionEntryPin: Entry node has %d user-defined pins"), 
			FunctionEntry->UserDefinedPins.Num()), EN2CLogSeverity::Debug);
		
		for (const TSharedPtr<FUserPinInfo>& UserPin : FunctionEntry->UserDefinedPins)
		{
			if (UserPin.IsValid())
			{
				FString DirectionStr = TEXT("Unknown");
				switch (UserPin->DesiredPinDirection)
				{
					case EGPD_Input: DirectionStr = TEXT("Input"); break;
					case EGPD_Output: DirectionStr = TEXT("Output"); break;
					default: DirectionStr = TEXT("Unknown"); break;
				}
				
				FN2CLogger::Get().Log(FString::Printf(
					TEXT("  UserPin: Name='%s', Direction=%s, Type=%s, IsRef=%s"),
					*UserPin->PinName.ToString(),
					*DirectionStr,
					*UserPin->PinType.PinCategory.ToString(),
					UserPin->PinType.bIsReference ? TEXT("true") : TEXT("false")), 
					EN2CLogSeverity::Debug);
			}
		}

		// Log the requested pin name for comparison
		FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveFunctionEntryPin: Attempting to remove pin named '%s'"), 
			*PinName), EN2CLogSeverity::Debug);

		// Start a transaction for undo/redo
		const FScopedTransaction Transaction(
			FText::Format(NSLOCTEXT("NodeToCode", "RemoveFunctionEntryPin", "Remove Entry Pin '{0}'"), 
			FText::FromString(PinName))
		);

		FunctionEntry->Modify();

		// Remove the pin using our utility
		FString RemovalError;
		if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(FunctionEntry, PinName, RemovalError))
		{
			FN2CLogger::Get().LogError(FString::Printf(
				TEXT("RemoveFunctionEntryPin: Failed to remove pin '%s' - %s"), 
				*PinName, *RemovalError));
			return FMcpToolCallResult::CreateErrorResult(RemovalError);
		}

		// Update all function call sites
		FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);

		// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);

		FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveFunctionEntryPin: Successfully removed pin '%s' from function '%s'"), 
			*PinName, *FocusedGraph->GetName()), EN2CLogSeverity::Debug);

		// Show notification
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("NodeToCode", "EntryPinRemoved", "Entry pin '{0}' removed from function '{1}'"),
			FText::FromString(PinName),
			FText::FromString(FocusedGraph->GetName())
		));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		// Build and return success result
		TSharedPtr<FJsonObject> ResultJson = FN2CMcpFunctionPinUtils::BuildPinRemovalSuccessResult(
			FocusedGraph, PinName, false /* bIsReturnPin */);
		
		// Convert JSON object to string
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);
		
		return FMcpToolCallResult::CreateTextResult(ResultString);
	});
}