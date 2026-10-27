// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpConnectPinsTool.h"
#include "MCP/Utils/N2CMcpNotification.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CPinConnectPolicy.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register this tool with the MCP tool manager
REGISTER_MCP_TOOL(FN2CMcpConnectPinsTool)

#define LOCTEXT_NAMESPACE "NodeToCode"

FMcpToolDefinition FN2CMcpConnectPinsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("connect-pins"),
		TEXT("Connect pins between nodes of a Blueprint graph (the one blueprintPath plus graphName or graphGuid name, else the focused graph) using their GUIDs, in one undo transaction. A data output and an exec input take any number of links; a data input and an exec output take one, and a connection that would replace that link fails unless options.breakExistingLinks is true. Failed connections are listed in the result, which is not an error."),
		N2CMcpToolsets::BlueprintGraph
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// connections array property
	TSharedPtr<FJsonObject> ConnectionsProp = MakeShareable(new FJsonObject);
	ConnectionsProp->SetStringField(TEXT("type"), TEXT("array"));
	ConnectionsProp->SetStringField(TEXT("description"), TEXT("Array of pin connections to create"));
	
	// Connection item schema
	TSharedPtr<FJsonObject> ConnectionItemSchema = MakeShareable(new FJsonObject);
	ConnectionItemSchema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> ConnectionProperties = MakeShareable(new FJsonObject);
	
	// From pin definition
	TSharedPtr<FJsonObject> FromProp = MakeShareable(new FJsonObject);
	FromProp->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> FromProperties = MakeShareable(new FJsonObject);
	
	TSharedPtr<FJsonObject> FromNodeGuidProp = MakeShareable(new FJsonObject);
	FromNodeGuidProp->SetStringField(TEXT("type"), TEXT("string"));
	FromNodeGuidProp->SetStringField(TEXT("description"), TEXT("GUID of the source node"));
	FromProperties->SetObjectField(TEXT("nodeGuid"), FromNodeGuidProp);
	
	TSharedPtr<FJsonObject> FromPinGuidProp = MakeShareable(new FJsonObject);
	FromPinGuidProp->SetStringField(TEXT("type"), TEXT("string"));
	FromPinGuidProp->SetStringField(TEXT("description"), TEXT("GUID of the source pin. Required: an entry without it is skipped"));
	FromProperties->SetObjectField(TEXT("pinGuid"), FromPinGuidProp);
	
	TSharedPtr<FJsonObject> FromPinNameProp = MakeShareable(new FJsonObject);
	FromPinNameProp->SetStringField(TEXT("type"), TEXT("string"));
	FromPinNameProp->SetStringField(TEXT("description"), TEXT("Pin name (PinName such as 'then', or display name), case-insensitive; used when pinGuid matches no pin"));
	FromProperties->SetObjectField(TEXT("pinName"), FromPinNameProp);
	
	TSharedPtr<FJsonObject> FromPinDirectionProp = MakeShareable(new FJsonObject);
	FromPinDirectionProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> FromDirectionEnum;
	FromDirectionEnum.Add(MakeShareable(new FJsonValueString(TEXT("EGPD_Input"))));
	FromDirectionEnum.Add(MakeShareable(new FJsonValueString(TEXT("EGPD_Output"))));
	FromPinDirectionProp->SetArrayField(TEXT("enum"), FromDirectionEnum);
	FromProperties->SetObjectField(TEXT("pinDirection"), FromPinDirectionProp);
	
	FromProp->SetObjectField(TEXT("properties"), FromProperties);
	
	TArray<TSharedPtr<FJsonValue>> FromRequired;
	FromRequired.Add(MakeShareable(new FJsonValueString(TEXT("nodeGuid"))));
	FromRequired.Add(MakeShareable(new FJsonValueString(TEXT("pinGuid"))));
	FromProp->SetArrayField(TEXT("required"), FromRequired);
	
	ConnectionProperties->SetObjectField(TEXT("from"), FromProp);
	
	// To pin definition (similar structure)
	TSharedPtr<FJsonObject> ToProp = MakeShareable(new FJsonObject);
	ToProp->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> ToProperties = MakeShareable(new FJsonObject);
	
	TSharedPtr<FJsonObject> ToNodeGuidProp = MakeShareable(new FJsonObject);
	ToNodeGuidProp->SetStringField(TEXT("type"), TEXT("string"));
	ToNodeGuidProp->SetStringField(TEXT("description"), TEXT("GUID of the target node"));
	ToProperties->SetObjectField(TEXT("nodeGuid"), ToNodeGuidProp);
	
	TSharedPtr<FJsonObject> ToPinGuidProp = MakeShareable(new FJsonObject);
	ToPinGuidProp->SetStringField(TEXT("type"), TEXT("string"));
	ToPinGuidProp->SetStringField(TEXT("description"), TEXT("GUID of the target pin. Required: an entry without it is skipped"));
	ToProperties->SetObjectField(TEXT("pinGuid"), ToPinGuidProp);
	
	TSharedPtr<FJsonObject> ToPinNameProp = MakeShareable(new FJsonObject);
	ToPinNameProp->SetStringField(TEXT("type"), TEXT("string"));
	ToPinNameProp->SetStringField(TEXT("description"), TEXT("Pin name (PinName such as 'execute', or display name), case-insensitive; used when pinGuid matches no pin"));
	ToProperties->SetObjectField(TEXT("pinName"), ToPinNameProp);
	
	TSharedPtr<FJsonObject> ToPinDirectionProp = MakeShareable(new FJsonObject);
	ToPinDirectionProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> ToDirectionEnum;
	ToDirectionEnum.Add(MakeShareable(new FJsonValueString(TEXT("EGPD_Input"))));
	ToDirectionEnum.Add(MakeShareable(new FJsonValueString(TEXT("EGPD_Output"))));
	ToPinDirectionProp->SetArrayField(TEXT("enum"), ToDirectionEnum);
	ToProperties->SetObjectField(TEXT("pinDirection"), ToPinDirectionProp);
	
	ToProp->SetObjectField(TEXT("properties"), ToProperties);
	
	TArray<TSharedPtr<FJsonValue>> ToRequired;
	ToRequired.Add(MakeShareable(new FJsonValueString(TEXT("nodeGuid"))));
	ToRequired.Add(MakeShareable(new FJsonValueString(TEXT("pinGuid"))));
	ToProp->SetArrayField(TEXT("required"), ToRequired);
	
	ConnectionProperties->SetObjectField(TEXT("to"), ToProp);
	
	ConnectionItemSchema->SetObjectField(TEXT("properties"), ConnectionProperties);
	
	TArray<TSharedPtr<FJsonValue>> ConnectionRequired;
	ConnectionRequired.Add(MakeShareable(new FJsonValueString(TEXT("from"))));
	ConnectionRequired.Add(MakeShareable(new FJsonValueString(TEXT("to"))));
	ConnectionItemSchema->SetArrayField(TEXT("required"), ConnectionRequired);
	
	ConnectionsProp->SetObjectField(TEXT("items"), ConnectionItemSchema);
	Properties->SetObjectField(TEXT("connections"), ConnectionsProp);
	
	// Options property (optional)
	TSharedPtr<FJsonObject> OptionsProp = MakeShareable(new FJsonObject);
	OptionsProp->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> OptionsProperties = MakeShareable(new FJsonObject);
	
	TSharedPtr<FJsonObject> TransactionNameProp = MakeShareable(new FJsonObject);
	TransactionNameProp->SetStringField(TEXT("type"), TEXT("string"));
	TransactionNameProp->SetStringField(TEXT("default"), TEXT("NodeToCode: Connect Pins"));
	OptionsProperties->SetObjectField(TEXT("transactionName"), TransactionNameProp);
	
	TSharedPtr<FJsonObject> BreakExistingLinksProp = MakeShareable(new FJsonObject);
	BreakExistingLinksProp->SetStringField(TEXT("type"), TEXT("boolean"));
	BreakExistingLinksProp->SetBoolField(TEXT("default"), false);
	BreakExistingLinksProp->SetStringField(TEXT("description"), TEXT("Allow the connection to replace an existing link where the pin takes only one (data inputs, exec outputs), including a link that needs a conversion node. Exec inputs keep all their incoming links either way."));
	OptionsProperties->SetObjectField(TEXT("breakExistingLinks"), BreakExistingLinksProp);
	
	OptionsProp->SetObjectField(TEXT("properties"), OptionsProperties);
	Properties->SetObjectField(TEXT("options"), OptionsProp);
	
	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// Required parameters
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShareable(new FJsonValueString(TEXT("connections"))));
	Schema->SetArrayField(TEXT("required"), Required);
	
	Definition.InputSchema = Schema;
	
	FN2CGraphTarget::AddSchemaProperties(Definition);

	return Definition;
}

FMcpToolCallResult FN2CMcpConnectPinsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		FN2CLogger::Get().Log(TEXT("ConnectPins: Starting execution"), EN2CLogSeverity::Debug);
		
		// Parse input arguments
		TArray<FConnectionRequest> ConnectionRequests;
		FConnectionOptions Options;
		
		if (!ParseConnectionRequests(Arguments, ConnectionRequests, Options))
		{
			FN2CLogger::Get().LogError(TEXT("ConnectPins: Failed to parse connection requests"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to parse connection requests"));
		}
		
		if (ConnectionRequests.Num() == 0)
		{
			FN2CLogger::Get().LogError(TEXT("ConnectPins: No connections specified"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("No connections specified"));
		}
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Processing %d connection requests"), ConnectionRequests.Num()), EN2CLogSeverity::Debug);
		
		// The graph blueprintPath / graphName / graphGuid name, else the focused one (card 0152); every result
		// after this says which graph was used ('target')
		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(Arguments, /*bForWrite*/ true);
		if (!Target.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Target.Error);
		}
		UBlueprint* Blueprint = Target.Blueprint;
		UEdGraph* FocusedGraph = Target.Graph;
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Found focused graph: %s in Blueprint: %s"), 
			*FocusedGraph->GetName(), *Blueprint->GetName()), EN2CLogSeverity::Debug);
		
		// Get graph schema
		const UEdGraphSchema* Schema = FocusedGraph->GetSchema();
		if (!Schema)
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Graph has no schema")));
		}
		
		// Build node lookup map for efficient GUID lookups
		TMap<FGuid, UEdGraphNode*> NodeMap = BuildNodeLookupMap(FocusedGraph);
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Built node lookup map with %d nodes"), NodeMap.Num()), EN2CLogSeverity::Debug);
		
		// Log all nodes in the map for debugging
		for (const auto& NodePair : NodeMap)
		{
			if (NodePair.Value)
			{
				FN2CLogger::Get().Log(FString::Printf(TEXT("  Node GUID: %s, Name: %s, Type: %s"), 
					*NodePair.Key.ToString(), 
					*NodePair.Value->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					*NodePair.Value->GetClass()->GetName()), EN2CLogSeverity::Debug);
			}
		}
		
		// Process connections within a transaction
		TArray<FConnectionResult> Results;
		
		{
			const FScopedTransaction Transaction(FText::FromString(Options.TransactionName));
			Blueprint->Modify();
			FocusedGraph->Modify();
			
			bool bAnyConnected = false;
			for (const FConnectionRequest& Request : ConnectionRequests)
			{
				FConnectionResult Result = ProcessConnection(Request, NodeMap, Schema, Options, Blueprint);
				bAnyConnected |= Result.bSuccess;
				Results.Add(Result);
			}

			// One mark for the whole batch; the tool call compiles once, after this transaction closes, and adds the
			// compiler's errors and warnings to the result (card 0151)
			if (bAnyConnected)
			{
				FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);
			}
		}
		
		// Count successes/failures
		int32 SuccessCount = 0;
		for (const FConnectionResult& Result : Results)
		{
			if (Result.bSuccess)
			{
				SuccessCount++;
			}
		}
		
		// Show notification
		if (SuccessCount > 0)
		{
			FN2CMcpNotification::Show(FText::Format(
				LOCTEXT("ConnectionsCreated", "Created {0} of {1} connections"),
				FText::AsNumber(SuccessCount),
				FText::AsNumber(Results.Num())
			));
		}
		
		// Build and return result JSON
		TSharedPtr<FJsonObject> ResultJson = BuildResultJson(Results);
		
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(ResultJson.ToSharedRef(), Writer);

		return Target.Finish(FMcpToolCallResult::CreateTextResult(JsonString));
	});
}

bool FN2CMcpConnectPinsTool::ParseConnectionRequests(const TSharedPtr<FJsonObject>& Arguments,
	TArray<FConnectionRequest>& OutRequests, FConnectionOptions& OutOptions) const
{
	FN2CLogger::Get().Log(TEXT("ConnectPins: Parsing connection requests"), EN2CLogSeverity::Debug);
	
	FN2CMcpArgumentParser ArgParser(Arguments);
	
	// Parse connections array
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = ArgParser.GetOptionalArray(TEXT("connections"));
	if (!ConnectionsArray || ConnectionsArray->Num() == 0)
	{
		FN2CLogger::Get().LogError(TEXT("ConnectPins: No 'connections' array field found in arguments"));
		return false;
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Found %d connections in request"), ConnectionsArray->Num()), EN2CLogSeverity::Debug);
	
	for (const TSharedPtr<FJsonValue>& ConnectionValue : *ConnectionsArray)
	{
		const TSharedPtr<FJsonObject>* ConnectionObject;
		if (!ConnectionValue->TryGetObject(ConnectionObject))
		{
			continue;
		}
		
		FConnectionRequest Request;
		
		// Parse 'from' object
		const TSharedPtr<FJsonObject>* FromObject;
		if (!(*ConnectionObject)->TryGetObjectField(TEXT("from"), FromObject))
		{
			continue;
		}
		
		FN2CMcpArgumentParser FromParser(*FromObject);
		FString ErrorMsg;
		if (!FromParser.TryGetRequiredString(TEXT("nodeGuid"), Request.FromNodeGuid, ErrorMsg) ||
			!FromParser.TryGetRequiredString(TEXT("pinGuid"), Request.FromPinGuid, ErrorMsg))
		{
			continue;
		}
		
		// Optional fields
		Request.FromPinName = FromParser.GetOptionalString(TEXT("pinName"));
		Request.FromPinDirection = FromParser.GetOptionalString(TEXT("pinDirection"));
		
		// Parse 'to' object
		const TSharedPtr<FJsonObject>* ToObject;
		if (!(*ConnectionObject)->TryGetObjectField(TEXT("to"), ToObject))
		{
			continue;
		}
		
		FN2CMcpArgumentParser ToParser(*ToObject);
		if (!ToParser.TryGetRequiredString(TEXT("nodeGuid"), Request.ToNodeGuid, ErrorMsg) ||
			!ToParser.TryGetRequiredString(TEXT("pinGuid"), Request.ToPinGuid, ErrorMsg))
		{
			continue;
		}
		
		// Optional fields
		Request.ToPinName = ToParser.GetOptionalString(TEXT("pinName"));
		Request.ToPinDirection = ToParser.GetOptionalString(TEXT("pinDirection"));
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Parsed connection request - From: Node=%s, Pin=%s, Name=%s, Dir=%s | To: Node=%s, Pin=%s, Name=%s, Dir=%s"),
			*Request.FromNodeGuid, *Request.FromPinGuid, *Request.FromPinName, *Request.FromPinDirection,
			*Request.ToNodeGuid, *Request.ToPinGuid, *Request.ToPinName, *Request.ToPinDirection), EN2CLogSeverity::Debug);
		
		OutRequests.Add(Request);
	}
	
	// Parse options (optional)
	TSharedPtr<FJsonObject> OptionsObject = ArgParser.GetOptionalObject(TEXT("options"));
	if (OptionsObject.IsValid())
	{
		FN2CMcpArgumentParser OptionsParser(OptionsObject);
		OutOptions.TransactionName = OptionsParser.GetOptionalString(TEXT("transactionName"), OutOptions.TransactionName);
		OutOptions.bBreakExistingLinks = OptionsParser.GetOptionalBool(TEXT("breakExistingLinks"), OutOptions.bBreakExistingLinks);
	}
	
	return true;
}

TMap<FGuid, UEdGraphNode*> FN2CMcpConnectPinsTool::BuildNodeLookupMap(UEdGraph* Graph) const
{
	TMap<FGuid, UEdGraphNode*> NodeMap;
	
	if (!Graph)
	{
		return NodeMap;
	}
	
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid.IsValid())
		{
			NodeMap.Add(Node->NodeGuid, Node);
		}
	}
	
	return NodeMap;
}

UEdGraphPin* FN2CMcpConnectPinsTool::FindPinRobustly(UEdGraphNode* Node, const FString& PinGuid,
	const FString& PinName, const FString& PinDirection) const
{
	if (!Node)
	{
		FN2CLogger::Get().LogError(TEXT("ConnectPins: FindPinRobustly called with null node"));
		return nullptr;
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: FindPinRobustly - Node: %s, PinGuid: %s, PinName: %s, PinDirection: %s"),
		*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *PinGuid, *PinName, *PinDirection), EN2CLogSeverity::Debug);
	
	// Log all pins on this node
	FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Node has %d pins:"), Node->Pins.Num()), EN2CLogSeverity::Debug);
	for (UEdGraphPin* Pin : Node->Pins)
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
			
			FN2CLogger::Get().Log(FString::Printf(TEXT("  Pin: Name=%s, DisplayName=%s, GUID=%s, Direction=%s, Type=%s"),
				*Pin->PinName.ToString(), 
				*Pin->GetDisplayName().ToString(),
				*Pin->PinId.ToString(),
				*DirectionStr,
				*Pin->PinType.PinCategory.ToString()), EN2CLogSeverity::Debug);
		}
	}
	
	// First, try to find by GUID
	FGuid ParsedGuid;
	if (FGuid::Parse(PinGuid, ParsedGuid))
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Searching for pin by GUID: %s"), *ParsedGuid.ToString()), EN2CLogSeverity::Debug);
		
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->PinId == ParsedGuid)
			{
				FN2CLogger::Get().Log(TEXT("ConnectPins: Found pin by GUID!"), EN2CLogSeverity::Debug);
				return Pin;
			}
		}
		
		FN2CLogger::Get().Log(TEXT("ConnectPins: Pin not found by GUID"), EN2CLogSeverity::Debug);
	}
	else
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Failed to parse GUID: %s"), *PinGuid), EN2CLogSeverity::Debug);
	}
	
	// Fallback: Try to find by name and direction
	if (!PinName.IsEmpty())
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Trying fallback search by name: %s"), *PinName), EN2CLogSeverity::Debug);
		
		// Parse direction if provided
		EEdGraphPinDirection ExpectedDirection = EGPD_MAX;
		if (!PinDirection.IsEmpty())
		{
			if (PinDirection == TEXT("EGPD_Input"))
			{
				ExpectedDirection = EGPD_Input;
			}
			else if (PinDirection == TEXT("EGPD_Output"))
			{
				ExpectedDirection = EGPD_Output;
			}
			FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Expected direction: %s"), *PinDirection), EN2CLogSeverity::Debug);
		}
		
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			
			// Check name match (try both PinName and DisplayName)
			bool bNameMatches = (Pin->PinName.ToString() == PinName);
			bool bDisplayNameMatches = (Pin->GetDisplayName().ToString() == PinName);
			
			if (bNameMatches || bDisplayNameMatches)
			{
				FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Found potential pin match - Name=%s, DisplayName=%s"), 
					*Pin->PinName.ToString(), *Pin->GetDisplayName().ToString()), EN2CLogSeverity::Debug);
				
				// If direction specified, also check that
				if (ExpectedDirection != EGPD_MAX && Pin->Direction != ExpectedDirection)
				{
					FN2CLogger::Get().Log(TEXT("ConnectPins: Pin direction mismatch, skipping"), EN2CLogSeverity::Debug);
					continue;
				}
				
				FN2CLogger::Get().Log(TEXT("ConnectPins: Found pin by name!"), EN2CLogSeverity::Debug);
				return Pin;
			}
		}
		
		FN2CLogger::Get().Log(TEXT("ConnectPins: Pin not found by name"), EN2CLogSeverity::Debug);
	}
	
	FN2CLogger::Get().Log(TEXT("ConnectPins: FindPinRobustly returning nullptr"), EN2CLogSeverity::Debug);
	return nullptr;
}

FN2CMcpConnectPinsTool::FConnectionResult FN2CMcpConnectPinsTool::ProcessConnection(
	const FConnectionRequest& Request, const TMap<FGuid, UEdGraphNode*>& NodeMap,
	const UEdGraphSchema* Schema, const FConnectionOptions& Options, UBlueprint* Blueprint) const
{
	FN2CLogger::Get().Log(TEXT("ConnectPins: ProcessConnection starting"), EN2CLogSeverity::Debug);
	
	FConnectionResult Result;
	Result.FromNodeGuid = Request.FromNodeGuid;
	Result.FromPinGuid = Request.FromPinGuid;
	Result.ToNodeGuid = Request.ToNodeGuid;
	Result.ToPinGuid = Request.ToPinGuid;
	
	// Find nodes
	FGuid FromNodeGuid, ToNodeGuid;
	if (!FGuid::Parse(Request.FromNodeGuid, FromNodeGuid) ||
		!FGuid::Parse(Request.ToNodeGuid, ToNodeGuid))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("ConnectPins: Invalid GUID format - From: %s, To: %s"), 
			*Request.FromNodeGuid, *Request.ToNodeGuid));
		Result.bSuccess = false;
		Result.ErrorCode = TEXT("INVALID_GUID");
		Result.ErrorMessage = TEXT("Invalid GUID format");
		return Result;
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Parsed GUIDs - From: %s, To: %s"), 
		*FromNodeGuid.ToString(), *ToNodeGuid.ToString()), EN2CLogSeverity::Debug);
	
	UEdGraphNode* FromNode = NodeMap.FindRef(FromNodeGuid);
	UEdGraphNode* ToNode = NodeMap.FindRef(ToNodeGuid);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Node lookup - From: %s, To: %s"), 
		FromNode ? TEXT("Found") : TEXT("NOT FOUND"), 
		ToNode ? TEXT("Found") : TEXT("NOT FOUND")), EN2CLogSeverity::Debug);
	
	if (!FromNode)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("ConnectPins: Source node not found with GUID: %s"), *Request.FromNodeGuid));
		Result.bSuccess = false;
		Result.ErrorCode = TEXT("NODE_NOT_FOUND");
		Result.ErrorMessage = FString::Printf(TEXT("Source node not found: %s"), *Request.FromNodeGuid);
		return Result;
	}
	
	if (!ToNode)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("ConnectPins: Target node not found with GUID: %s"), *Request.ToNodeGuid));
		Result.bSuccess = false;
		Result.ErrorCode = TEXT("NODE_NOT_FOUND");
		Result.ErrorMessage = FString::Printf(TEXT("Target node not found: %s"), *Request.ToNodeGuid);
		return Result;
	}
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("ConnectPins: Found nodes - From: %s, To: %s"), 
		*FromNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
		*ToNode->GetNodeTitle(ENodeTitleType::ListView).ToString()), EN2CLogSeverity::Debug);
	
	// Find pins with fallback support
	UEdGraphPin* FromPin = FindPinRobustly(FromNode, Request.FromPinGuid, 
		Request.FromPinName, Request.FromPinDirection);
	
	if (!FromPin)
	{
		Result.bSuccess = false;
		Result.ErrorCode = TEXT("PIN_NOT_FOUND");
		Result.ErrorMessage = FString::Printf(TEXT("Source pin not found: %s"), *Request.FromPinGuid);
		return Result;
	}
	
	UEdGraphPin* ToPin = FindPinRobustly(ToNode, Request.ToPinGuid,
		Request.ToPinName, Request.ToPinDirection);
	
	if (!ToPin)
	{
		Result.bSuccess = false;
		Result.ErrorCode = TEXT("PIN_NOT_FOUND");
		Result.ErrorMessage = FString::Printf(TEXT("Target pin not found: %s"), *Request.ToPinGuid);
		return Result;
	}
	
	// Validate connection via schema; the schema decides what (if anything) a connection breaks
	const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
	FString PolicyError;
	if (!N2CPinConnectPolicy::Allow(Response, FromPin, ToPin, Options.bBreakExistingLinks, PolicyError))
	{
		Result.bSuccess = false;
		Result.ErrorCode = N2CPinConnectPolicy::WouldReplaceExistingLink(Response, FromPin, ToPin) ? TEXT("WOULD_BREAK_EXISTING_LINK") : TEXT("SCHEMA_VALIDATION_FAILED");
		Result.ErrorMessage = PolicyError;
		return Result;
	}

	// Make the connection
	FromNode->Modify();
	ToNode->Modify();
	
	const bool bConnectionMade = Schema->TryCreateConnection(FromPin, ToPin);
	
	if (bConnectionMade)
	{
		Result.bSuccess = true;
		// No compile here: the tool call compiles the Blueprint once, after the transaction (card 0151)

		// Log success
		FString LogMessage = FString::Printf(
			TEXT("Successfully connected %s:%s to %s:%s"),
			*FromNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			*FromPin->PinName.ToString(),
			*ToNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			*ToPin->PinName.ToString()
		);
		FN2CLogger::Get().Log(LogMessage, EN2CLogSeverity::Info);
	}
	else
	{
		Result.bSuccess = false;
		Result.ErrorCode = TEXT("CONNECTION_FAILED");
		Result.ErrorMessage = TEXT("Failed to create connection");
	}
	
	return Result;
}

TSharedPtr<FJsonObject> FN2CMcpConnectPinsTool::BuildResultJson(const TArray<FConnectionResult>& Results) const
{
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	
	// Build succeeded array
	TArray<TSharedPtr<FJsonValue>> SucceededArray;
	
	// Build failed array
	TArray<TSharedPtr<FJsonValue>> FailedArray;
	
	int32 SuccessCount = 0;
	int32 FailureCount = 0;
	
	for (const FConnectionResult& Result : Results)
	{
		if (Result.bSuccess)
		{
			SuccessCount++;
			
			TSharedPtr<FJsonObject> SuccessObject = MakeShareable(new FJsonObject);
			
			// From object
			TSharedPtr<FJsonObject> FromObject = MakeShareable(new FJsonObject);
			FromObject->SetStringField(TEXT("nodeGuid"), Result.FromNodeGuid);
			FromObject->SetStringField(TEXT("pinGuid"), Result.FromPinGuid);
			SuccessObject->SetObjectField(TEXT("from"), FromObject);
			
			// To object
			TSharedPtr<FJsonObject> ToObject = MakeShareable(new FJsonObject);
			ToObject->SetStringField(TEXT("nodeGuid"), Result.ToNodeGuid);
			ToObject->SetStringField(TEXT("pinGuid"), Result.ToPinGuid);
			SuccessObject->SetObjectField(TEXT("to"), ToObject);
			
			SucceededArray.Add(MakeShareable(new FJsonValueObject(SuccessObject)));
		}
		else
		{
			FailureCount++;
			
			TSharedPtr<FJsonObject> FailureObject = MakeShareable(new FJsonObject);
			
			// From object
			TSharedPtr<FJsonObject> FromObject = MakeShareable(new FJsonObject);
			FromObject->SetStringField(TEXT("nodeGuid"), Result.FromNodeGuid);
			FromObject->SetStringField(TEXT("pinGuid"), Result.FromPinGuid);
			FailureObject->SetObjectField(TEXT("from"), FromObject);
			
			// To object
			TSharedPtr<FJsonObject> ToObject = MakeShareable(new FJsonObject);
			ToObject->SetStringField(TEXT("nodeGuid"), Result.ToNodeGuid);
			ToObject->SetStringField(TEXT("pinGuid"), Result.ToPinGuid);
			FailureObject->SetObjectField(TEXT("to"), ToObject);
			
			// Error info
			FailureObject->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			FailureObject->SetStringField(TEXT("reason"), Result.ErrorMessage);
			
			FailedArray.Add(MakeShareable(new FJsonValueObject(FailureObject)));
		}
	}
	
	RootObject->SetArrayField(TEXT("succeeded"), SucceededArray);
	RootObject->SetArrayField(TEXT("failed"), FailedArray);
	
	// Add summary
	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("totalRequested"), Results.Num());
	SummaryObject->SetNumberField(TEXT("succeeded"), SuccessCount);
	SummaryObject->SetNumberField(TEXT("failed"), FailureCount);
	RootObject->SetObjectField(TEXT("summary"), SummaryObject);
	
	return RootObject;
}

#undef LOCTEXT_NAMESPACE
