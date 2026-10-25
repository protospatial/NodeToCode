// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetFocusedBlueprintTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CCapture.h"
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
		TEXT("Collects and serializes the currently focused Blueprint graph in the Unreal Editor into NodeToCode's N2CJSON format."),
		TEXT("Blueprint Discovery")
	);
	
	// This tool takes no input parameters
	Definition.InputSchema = BuildEmptyObjectSchema();
	
	// Add read-only annotation
	AddReadOnlyAnnotation(Definition);
	
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
		
		// Translate nodes - this populates the ID maps
		FN2CBlueprint N2CBlueprintData;
		TMap<FGuid, FString> NodeIDMapCopy;
		TMap<FGuid, FString> PinIDMapCopy;
		
		if (!FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprintWithMaps(CollectedNodes, N2CBlueprintData, NodeIDMapCopy, PinIDMapCopy))
		{
			FN2CLogger::Get().LogWarning(TEXT("get-focused-blueprint tool failed: Translation failed"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to translate collected nodes into N2CBlueprint structure."));
		}
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("GetFocusedBlueprintTool: After translation - NodeIDMap has %d entries, PinIDMap has %d entries"), 
			NodeIDMapCopy.Num(), PinIDMapCopy.Num()), EN2CLogSeverity::Info);
		
		// Serialize to JSON
		FString JsonOutput = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(N2CBlueprintData, false);
		if (JsonOutput.IsEmpty())
		{
			FN2CLogger::Get().LogWarning(TEXT("get-focused-blueprint tool failed: Serialization failed"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize N2CBlueprint to JSON."));
		}
		
		// Enhance the JSON with GUID information using our saved copies
		FString EnhancedJson = EnhanceJsonWithGuids(JsonOutput, NodeIDMapCopy, PinIDMapCopy);
		
		FN2CLogger::Get().Log(TEXT("get-focused-blueprint tool successfully retrieved Blueprint JSON with GUID enhancement"), EN2CLogSeverity::Info);
		return FMcpToolCallResult::CreateTextResult(EnhancedJson);
	});
}

FString FN2CMcpGetFocusedBlueprintTool::EnhanceJsonWithGuids(const FString& JsonString, 
	const TMap<FGuid, FString>& NodeIDMap, const TMap<FGuid, FString>& PinIDMap) const
{
	// Parse the JSON
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("Failed to parse JSON for GUID enhancement"));
		return JsonString; // Return original if parsing fails
	}
	
	// Log the map sizes for debugging
	FN2CLogger::Get().Log(FString::Printf(TEXT("EnhanceJsonWithGuids: NodeIDMap has %d entries, PinIDMap has %d entries"), 
		NodeIDMap.Num(), PinIDMap.Num()), EN2CLogSeverity::Debug);
	
	// Create reverse maps (ID -> GUID)
	TMap<FString, FGuid> ReverseNodeIDMap;
	for (const auto& Pair : NodeIDMap)
	{
		ReverseNodeIDMap.Add(Pair.Value, Pair.Key);
	}
	
	TMap<FString, FGuid> ReversePinIDMap;
	for (const auto& Pair : PinIDMap)
	{
		ReversePinIDMap.Add(Pair.Value, Pair.Key);
	}
	
	// Log some sample entries from PinIDMap
	int32 SampleCount = 0;
	for (const auto& Pair : PinIDMap)
	{
		if (SampleCount < 5)
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("  PinIDMap sample: GUID=%s -> ID=%s"), 
				*Pair.Key.ToString(), *Pair.Value), EN2CLogSeverity::Debug);
			SampleCount++;
		}
	}
	
	// Process each graph in the blueprint
	const TArray<TSharedPtr<FJsonValue>>* GraphsArray;
	if (RootObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
	{
		for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
		{
			TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
			if (!GraphObject.IsValid()) continue;
			
			// Process nodes in this graph
			const TArray<TSharedPtr<FJsonValue>>* NodesArray;
			if (GraphObject->TryGetArrayField(TEXT("nodes"), NodesArray))
			{
				for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
				{
					TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
					if (!NodeObject.IsValid()) continue;
					
					// Get the short ID
					FString ShortNodeID;
					if (NodeObject->TryGetStringField(TEXT("id"), ShortNodeID))
					{
						// Create the nested ID structure
						TSharedPtr<FJsonObject> IdsObject = MakeShareable(new FJsonObject);
						IdsObject->SetStringField(TEXT("short"), ShortNodeID);
						
						// Find and add the GUID if available
						if (FGuid* NodeGuid = ReverseNodeIDMap.Find(ShortNodeID))
						{
							IdsObject->SetStringField(TEXT("guid"), NodeGuid->ToString(EGuidFormats::DigitsWithHyphens));
						}
						
						// Replace the simple ID with the nested structure
						NodeObject->RemoveField(TEXT("id"));
						NodeObject->SetObjectField(TEXT("ids"), IdsObject);
					}
					
					// Process pins
					TArray<FString> PinArrayNames = { TEXT("input_pins"), TEXT("output_pins") };
					for (const FString& PinArrayName : PinArrayNames)
					{
						const TArray<TSharedPtr<FJsonValue>>* PinsArray;
						if (NodeObject->TryGetArrayField(PinArrayName, PinsArray))
						{
							// Create a new array for enhanced pins
							TArray<TSharedPtr<FJsonValue>> EnhancedPins;
							
							for (const TSharedPtr<FJsonValue>& PinValue : *PinsArray)
							{
								TSharedPtr<FJsonObject> PinObject = PinValue->AsObject();
								if (!PinObject.IsValid()) continue;
								
								// Get the short pin ID
								FString ShortPinID;
								if (PinObject->TryGetStringField(TEXT("id"), ShortPinID))
								{
									// Log the pin ID we're looking for
									FN2CLogger::Get().Log(FString::Printf(TEXT("EnhanceJsonWithGuids: Looking for pin ID '%s' in ReversePinIDMap"), 
										*ShortPinID), EN2CLogSeverity::Debug);
									
									// Log all entries in ReversePinIDMap for this specific search
									bool bFoundMatch = false;
									for (const auto& MapEntry : ReversePinIDMap)
									{
										if (MapEntry.Key == ShortPinID)
										{
											FN2CLogger::Get().Log(FString::Printf(TEXT("  Found exact match: ID='%s' -> GUID=%s"), 
												*MapEntry.Key, *MapEntry.Value.ToString()), EN2CLogSeverity::Debug);
											bFoundMatch = true;
										}
									}
									
									if (!bFoundMatch)
									{
										FN2CLogger::Get().Log(FString::Printf(TEXT("  No exact match found for '%s'. Map has %d entries."), 
											*ShortPinID, ReversePinIDMap.Num()), EN2CLogSeverity::Debug);
										
										// Log first few entries to debug case issues
										int32 Count = 0;
										for (const auto& MapEntry : ReversePinIDMap)
										{
											if (Count++ < 3)
											{
												FN2CLogger::Get().Log(FString::Printf(TEXT("    Sample entry: ID='%s' -> GUID=%s"), 
													*MapEntry.Key, *MapEntry.Value.ToString()), EN2CLogSeverity::Debug);
											}
										}
									}
									
									// Create the nested ID structure for pin
									TSharedPtr<FJsonObject> PinIdsObject = MakeShareable(new FJsonObject);
									PinIdsObject->SetStringField(TEXT("short"), ShortPinID);
									
									// Find and add the GUID if available
									if (FGuid* PinGuid = ReversePinIDMap.Find(ShortPinID))
									{
										PinIdsObject->SetStringField(TEXT("guid"), PinGuid->ToString(EGuidFormats::DigitsWithHyphens));
										FN2CLogger::Get().Log(FString::Printf(TEXT("EnhanceJsonWithGuids: Found GUID for pin %s: %s on node %s"), 
											*ShortPinID, *PinGuid->ToString(), *ShortNodeID), EN2CLogSeverity::Debug);
									}
									else
									{
										// Log when we can't find a GUID for a pin
										FN2CLogger::Get().Log(FString::Printf(TEXT("EnhanceJsonWithGuids: No GUID found for pin ID: %s on node %s"), 
											*ShortPinID, *ShortNodeID), EN2CLogSeverity::Debug);
									}
									
									// Add pin name as fallback identifier
									FString PinName;
									if (PinObject->TryGetStringField(TEXT("name"), PinName))
									{
										PinIdsObject->SetStringField(TEXT("name"), PinName);
									}
									
									// Replace the simple ID with the nested structure
									PinObject->RemoveField(TEXT("id"));
									PinObject->SetObjectField(TEXT("ids"), PinIdsObject);
								}
								
								// Add the enhanced pin to the new array
								EnhancedPins.Add(PinValue);
							}
							
							// Replace the pin array with the enhanced version
							NodeObject->SetArrayField(PinArrayName, EnhancedPins);
						}
					}
				}
			}
			
			// Note: Per user requirement, Pin GUIDs should NOT appear in flows section
			// The flows section should only use short IDs for readability
		}
	}
	
	// Serialize back to JSON string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	
	return OutputString;
}
