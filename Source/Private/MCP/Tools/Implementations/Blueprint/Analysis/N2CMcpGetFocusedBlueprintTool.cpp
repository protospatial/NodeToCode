// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetFocusedBlueprintTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Utils/N2CLogger.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetFocusedBlueprintTool)

FMcpToolDefinition FN2CMcpGetFocusedBlueprintTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-focused-blueprint"),
		TEXT("Collects and serializes the currently focused Blueprint graph in the Unreal Editor into NodeToCode's N2CJSON format. ")
		TEXT("The focused Blueprint is the Blueprint editor the user last activated (clicked its tab or inside it) or that ")
		TEXT("open-blueprint-asset last opened; the graph is the one that editor shows."),
		TEXT("Blueprint Discovery")
	);
	
	// This tool takes no input parameters
	Definition.InputSchema = BuildEmptyObjectSchema();
	
	// Add read-only annotation
	AddReadOnlyAnnotation(Definition);
	
	// Results carry Blueprint text (node comments, titles and string literals): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpGetFocusedBlueprintTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// Since this tool requires Game Thread execution, use the base class helper
	return ExecuteOnGameThread([this]() -> FMcpToolCallResult
	{
		// Get the focused graph and collect nodes directly
		// UEdGraph* FocusedGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor(); // Old way
            UBlueprint* OwningBlueprint = nullptr; // Will be populated by the utility
            UEdGraph* FocusedGraph = nullptr;
            FString GraphError;
            if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
            {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("get-focused-blueprint tool failed: %s"), *GraphError));
                return FMcpToolCallResult::CreateErrorResult(GraphError);
            }
		
		// Collect nodes
		TArray<UK2Node*> CollectedNodes;
		if (!FN2CEditorIntegration::Get().CollectNodesFromGraph(FocusedGraph, CollectedNodes) || CollectedNodes.IsEmpty())
		{
			FN2CLogger::Get().LogWarning(TEXT("get-focused-blueprint tool failed: No nodes collected"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to collect nodes or no nodes found in the focused graph."));
		}
		
		// Capture the nodes: the Blueprint and its ID-to-GUID maps come from this one capture
		FN2CCapture Capture;
		// The default events the editor placed stay visible (marked disabled), so an agent wires them
		// instead of adding a duplicate event (card 0137)
		FN2CCaptureOptions Options;
		Options.bIncludeGhostNodes = true;
		if (!FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprintWithMaps(CollectedNodes, Capture, Options))
		{
			FN2CLogger::Get().LogWarning(TEXT("get-focused-blueprint tool failed: Translation failed"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to translate collected nodes into N2CBlueprint structure."));
		}

		const FString EnhancedJson = BuildResultJson(Capture);
		if (EnhancedJson.IsEmpty())
		{
			FN2CLogger::Get().LogWarning(TEXT("get-focused-blueprint tool failed: Serialization failed"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize N2CBlueprint to JSON."));
		}

		FN2CLogger::Get().Log(FString::Printf(TEXT("get-focused-blueprint: %d node and %d pin GUIDs"),
			Capture.NodeGuids.Num(), Capture.Pins.Num()), EN2CLogSeverity::Info);
		return FMcpToolCallResult::CreateTextResult(EnhancedJson);
	});
}

FString FN2CMcpGetFocusedBlueprintTool::BuildResultJson(const FN2CCapture& Capture)
{
	// The N2C JSON with the capture's "guids" table (one entry per node and per pin)
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
		FN2CSerializer::ToJson(Capture, EN2CJsonStyle::Condensed, EN2CJsonGuids::Table));
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("get-focused-blueprint: the serialized capture is not a JSON object"));
		return FString();
	}

	// Each node's and pin's "id" becomes "ids": {"short", "guid"} (pins also "name"); the flows keep the short IDs
	const TArray<TSharedPtr<FJsonValue>>* GraphsArray = nullptr;
	if (RootObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
		{
			const TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
			if (!GraphObject.IsValid() || !GraphObject->TryGetArrayField(TEXT("nodes"), NodesArray))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
			{
				const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
				if (!NodeObject.IsValid())
				{
					continue;
				}

				FString ShortNodeID;
				if (NodeObject->TryGetStringField(TEXT("id"), ShortNodeID))
				{
					TSharedPtr<FJsonObject> IdsObject = MakeShared<FJsonObject>();
					IdsObject->SetStringField(TEXT("short"), ShortNodeID);
					if (const FGuid* NodeGuid = Capture.NodeGuids.Find(ShortNodeID))
					{
						IdsObject->SetStringField(TEXT("guid"), NodeGuid->ToString(EGuidFormats::DigitsWithHyphens));
					}
					NodeObject->RemoveField(TEXT("id"));
					NodeObject->SetObjectField(TEXT("ids"), IdsObject);
				}

				for (const TCHAR* PinArrayName : { TEXT("input_pins"), TEXT("output_pins") })
				{
					const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
					if (!NodeObject->TryGetArrayField(PinArrayName, PinsArray))
					{
						continue;
					}
					for (const TSharedPtr<FJsonValue>& PinValue : *PinsArray)
					{
						const TSharedPtr<FJsonObject> PinObject = PinValue->AsObject();
						FString ShortPinID;
						if (!PinObject.IsValid() || !PinObject->TryGetStringField(TEXT("id"), ShortPinID))
						{
							continue;
						}
						TSharedPtr<FJsonObject> PinIdsObject = MakeShared<FJsonObject>();
						PinIdsObject->SetStringField(TEXT("short"), ShortPinID);
						if (const FN2CCapturedPin* Pin = Capture.Pins.Find(ShortPinID))
						{
							PinIdsObject->SetStringField(TEXT("guid"), Pin->PinGuid.ToString(EGuidFormats::DigitsWithHyphens));
						}
						FString PinName;
						if (PinObject->TryGetStringField(TEXT("name"), PinName))
						{
							PinIdsObject->SetStringField(TEXT("name"), PinName);
						}
						PinObject->RemoveField(TEXT("id"));
						PinObject->SetObjectField(TEXT("ids"), PinIdsObject);
					}
				}
			}
		}
	}

	FString OutputString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	return OutputString;
}
