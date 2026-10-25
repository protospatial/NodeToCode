// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_EditablePinBase.h" // Need full definition for FUserPinInfo

// Forward declarations
class UK2Node_FunctionEntry;
class UK2Node_FunctionResult;
class UEdGraph;
class UEdGraphPin;
class UBlueprint;

/** What a function signature edit (FN2CMcpFunctionPinUtils::AddFunctionPin / RemoveFunctionPin) did */
struct FN2CFunctionPinEdit
{
	/** Why the edit was refused; nothing was changed and no transaction was opened when this is set */
	FString Error;
	/** Add: the pin as created on the entry node, or on the first result node. Null after a remove */
	UEdGraphPin* Pin = nullptr;
	/** Add: the name the pin got (a taken name is made unique). Remove: the name of the pin removed */
	FName PinName;
	/** The entry or result nodes that were edited (every result node of the function for a return value) */
	int32 EditedNodeCount = 0;
	/** Add of a return value: a result node had to be created (FBlueprintEditorUtils::FindOrCreateFunctionResultNode) */
	bool bCreatedResultNode = false;
};

/**
 * Function signature edits shared by the four MCP pin tools (add-function-input-pin, add-function-return-pin,
 * remove-function-entry-pin, remove-function-return-pin) and the Python bridge's AddFunctionInputPin,
 * AddFunctionReturnPin, RemoveFunctionEntryPin and RemoveFunctionReturnPin (card 0156). They follow the Blueprint
 * editor's own My Blueprint > Inputs / Outputs path (FBlueprintGraphActionDetails::OnAddNewOutputClicked and
 * OnParamsChanged): validate first, then in one transaction Modify the Blueprint, the graph and every node edited,
 * apply the edit to the entry node or to every result node, ReconstructNode each, and call
 * UEdGraphSchema_K2::HandleParameterDefaultValueChanged, which regenerates the skeleton class and only then rebuilds
 * the function's call sites (and delegates) against the new signature. The Blueprint is then marked modified
 * (FN2CMcpBlueprintUtils::MarkModified), so the tool call compiles it once after the transaction has closed.
 */
class FN2CMcpFunctionPinUtils
{
public:
	/**
	 * Finds the function entry node in a graph.
	 * @param Graph The graph to search
	 * @return The function entry node, or nullptr if not found
	 */
	static UK2Node_FunctionEntry* FindFunctionEntryNode(UEdGraph* Graph);

	/**
	 * Finds the first function result node in a graph (a function can have several; see GetAllResultNodes).
	 * @param Graph The graph to search
	 * @return The function result node, or nullptr if not found
	 */
	static UK2Node_FunctionResult* FindFunctionResultNode(UEdGraph* Graph);

	/** Every result (Return) node of the function graph, in graph order; empty when it has none */
	static TArray<UK2Node_FunctionResult*> GetAllResultNodes(UEdGraph* Graph);

	/**
	 * Adds an input parameter (bReturnValue false: a user-defined output pin of the entry node) or a return value
	 * (bReturnValue true: a user-defined input pin on every result node, after creating a result node wired from the
	 * entry when the function has none). A taken name is made unique (a second Tag becomes Tag1). DefaultValue applies
	 * to inputs only. Tooltip is kept in the function's metadata as an "@param <name> <text>" line, which the engine
	 * shows as the pin's tooltip on call nodes. Validation failures set Out.Error and change nothing.
	 * @return True when the pin was added
	 */
	static bool AddFunctionPin(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bReturnValue, const FString& DesiredName,
		const FEdGraphPinType& PinType, const FString& DefaultValue, const FString& Tooltip, const FText& TransactionTitle,
		FN2CFunctionPinEdit& Out);

	/**
	 * Removes an input parameter (bReturnValue false: from the entry node) or a return value (from every result node).
	 * PinName is matched exactly first, then case-insensitively against the pin name or the display name. The pin's
	 * links are broken first, and its "@param" tooltip line is dropped from the function metadata. Validation failures
	 * (no such pin, the exec pin, a pin that is not user-defined, no result node) set Out.Error, open no transaction
	 * and leave the package clean.
	 * @return True when the pin was removed
	 */
	static bool RemoveFunctionPin(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bReturnValue, const FString& PinName,
		const FText& TransactionTitle, FN2CFunctionPinEdit& Out);

	/**
	 * Finds the pin RemoveFunctionPin would remove from Node and checks that it can be removed, without changing anything.
	 * @return The pin, or null with OutError set
	 */
	static UEdGraphPin* FindPinForRemoval(UK2Node_EditablePinBase* Node, const FString& PinName, FString& OutError);

	/**
	 * Sets (or with an empty Tooltip, drops) the "@param <PinName> <text>" line of the function's tooltip metadata
	 * (UK2Node_FunctionEntry::MetaData.ToolTip). Any earlier line for that name is replaced. The caller has called
	 * Modify on the entry node.
	 */
	static void SetParameterTooltip(UK2Node_FunctionEntry* FunctionEntry, FName PinName, const FString& Tooltip);

	/**
	 * Builds a standard success result JSON object for pin creation operations.
	 * @param FunctionGraph The function graph
	 * @param RequestedName The originally requested pin name
	 * @param CreatedPin The actually created pin
	 * @param PinType The type of the created pin
	 * @param bIsReturnPin Whether this is a return pin (vs input parameter)
	 * @return JSON object with success information
	 */
	static TSharedPtr<FJsonObject> BuildPinCreationSuccessResult(
		UEdGraph* FunctionGraph,
		const FString& RequestedName,
		UEdGraphPin* CreatedPin,
		const FEdGraphPinType& PinType,
		bool bIsReturnPin
	);

	/**
	 * Validates that a pin can be removed from a node.
	 * @param Node The node containing the pin
	 * @param Pin The pin to validate
	 * @param OutError Error message if validation fails
	 * @return True if the pin can be removed, false otherwise
	 */
	static bool ValidatePinForRemoval(UK2Node_EditablePinBase* Node, UEdGraphPin* Pin, FString& OutError);

	/**
	 * Builds a standard success result JSON object for pin removal operations.
	 * @param FunctionGraph The function graph
	 * @param RemovedPinName The name of the removed pin
	 * @param bIsReturnPin Whether this was a return pin (vs input parameter)
	 * @return JSON object with success information
	 */
	static TSharedPtr<FJsonObject> BuildPinRemovalSuccessResult(
		UEdGraph* FunctionGraph,
		const FString& RemovedPinName,
		bool bIsReturnPin
	);

private:
	/** ReconstructNode on each edited node, then HandleParameterDefaultValueChanged and MarkModified */
	static void FinishSignatureEdit(UBlueprint* Blueprint, const TArray<UK2Node_EditablePinBase*>& EditedNodes);
};
