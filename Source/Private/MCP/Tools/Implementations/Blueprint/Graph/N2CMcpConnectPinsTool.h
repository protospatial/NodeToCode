// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

// Forward declarations
class UEdGraph;
class UEdGraphSchema;

/**
 * MCP tool that connects pins between Blueprint nodes using their GUIDs.
 * Supports batch connections with transactional safety and comprehensive validation.
 * 
 * Connection behavior:
 * - Output data pins can connect to multiple input pins (one-to-many)
 * - Input data pins can only have one connection (many-to-one)
 * - Exec outputs take one link; exec inputs take any number (fan-in)
 * Each result names the schema's response, its message and any conversion node it placed (card 0276).
 */
class FN2CMcpConnectPinsTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	/**
	 * Connection request structure
	 */
	struct FConnectionRequest
	{
		// From pin info
		FString FromNodeGuid;
		FString FromPinGuid;
		FString FromPinName;	// Optional fallback
		FString FromPinDirection;	// Optional for validation
		
		// To pin info
		FString ToNodeGuid;
		FString ToPinGuid;
		FString ToPinName;		// Optional fallback
		FString ToPinDirection;	// Optional for validation

		// Position in the connections array, and why the entry could not be read (empty when it was)
		int32 Index = INDEX_NONE;
		FString MalformedReason;
	};
	
	/**
	 * Connection processing result
	 */
	struct FConnectionResult
	{
		bool bSuccess = false;
		FString FromNodeGuid;
		FString FromPinGuid;
		FString ToNodeGuid;
		FString ToPinGuid;
		FString ErrorMessage;
		FString ErrorCode;
		int32 Index = INDEX_NONE;
		// The schema's response, its message and the conversion node it placed (N2CPinConnectPolicy::WriteResponse)
		TSharedPtr<FJsonObject> ResponseFields;
	};
	
	/**
	 * Options for connection operations
	 */
	struct FConnectionOptions
	{
		FString TransactionName = TEXT("NodeToCode: Connect Pins");
		bool bBreakExistingLinks = false;
	};
	
	// Core methods
	bool ParseConnectionRequests(const TSharedPtr<FJsonObject>& Arguments, 
		TArray<FConnectionRequest>& OutRequests, FConnectionOptions& OutOptions) const;
	
	TMap<FGuid, UEdGraphNode*> BuildNodeLookupMap(UEdGraph* Graph) const;
	
	UEdGraphPin* FindPinRobustly(UEdGraphNode* Node, const FString& PinGuid, 
		const FString& PinName, const FString& PinDirection) const;
	
	FConnectionResult ProcessConnection(const FConnectionRequest& Request, 
		const TMap<FGuid, UEdGraphNode*>& NodeMap, const UEdGraphSchema* Schema,
		const FConnectionOptions& Options, UBlueprint* Blueprint) const;
	
	TSharedPtr<FJsonObject> BuildResultJson(const TArray<FConnectionResult>& Results) const;
};