// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/BlueprintGeneratedClass.h"

// Forward declarations
class UK2Node_FunctionEntry;
class UEdGraph;
struct FEdGraphPinType;
struct FBPVariableDescription;

/**
 * MCP tool that creates a new local variable within the currently focused Blueprint function.
 * Local variables are scoped to a specific function and stored in the UK2Node_FunctionEntry node.
 */
class FN2CMcpCreateLocalVariableTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * Adds a local variable to the function that Graph belongs to (its top-level graph, so a collapsed graph inside a
	 * function works too) through FBlueprintEditorUtils::AddLocalVariable, in one FScopedTransaction (card 0154). The
	 * name is made unique against the Blueprint's members and the function's parameters and locals
	 * (MakeUniqueLocalVariableName). Marks the Blueprint modified; inside a tool call the compile runs once when the
	 * call ends. Game thread.
	 * @return The variable's name, or NAME_None and OutError (nothing is changed then)
	 */
	static FName AddLocalVariableToFunction(UBlueprint* Blueprint, UEdGraph* Graph, const FString& DesiredName,
		const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Tooltip, FString& OutError);

	/**
	 * FBlueprintEditorUtils::FindUniqueKismetName in the function's scope (members, graphs, the function's parameters
	 * and locals, invalid characters replaced), also checked against the entry and return nodes' parameters, which the
	 * skeleton function may not list yet
	 */
	static FName MakeUniqueLocalVariableName(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FString& BaseName);

private:
	// Result building
	TSharedPtr<FJsonObject> BuildSuccessResult(UK2Node_FunctionEntry* FunctionEntry, UEdGraph* FunctionGraph,
		const FString& RequestedName, FName ActualName, const FEdGraphPinType& PinType, const FString& ContainerType) const;
};
