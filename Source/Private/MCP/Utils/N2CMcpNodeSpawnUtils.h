// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"
#include "Layout/SlateRect.h"

class FProperty;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphNode_Comment;
class UK2Node_Variable;
struct FBPVariableDescription;

/**
 * Creates variable and comment nodes the way the editor does (card 0155), for the MCP tools and the Python bridge.
 *
 * Every node made here gets a new NodeGuid, RF_Transactional, PostPlacedNewNode and exactly one set of pins
 * (AllocateDefaultPins once), and is added inside one FScopedTransaction after Graph->Modify(): a caller's own
 * transaction absorbs it, so one undo removes the node. None of these mark or compile the Blueprint; the caller
 * does (FN2CMcpBlueprintUtils::MarkModified, or MarkCosmeticChange for a comment).
 */
class FN2CMcpNodeSpawn
{
public:
	/**
	 * A Get (bSet false) or Set node for a member variable, through
	 * UBlueprintVariableNodeSpawner::CreateFromMemberOrParam, as the editor's My Blueprint panel and context menu make
	 * them (a self reference with the variable's GUID for the Blueprint's own variables). Property comes from
	 * FN2CMcpVariableUtils::FindMemberVariableProperty. Null with OutError when Graph is not a graph of Blueprint.
	 */
	static UK2Node_Variable* SpawnMemberVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FProperty* Property,
		bool bSet, const FVector2D& Location, FString& OutError);

	/**
	 * A Get or Set node for a local variable of the function Graph belongs to (Graph may be a collapsed graph inside
	 * it), through UBlueprintVariableNodeSpawner::CreateFromLocal with the function graph as the scope, as the
	 * Blueprint action database does. Null with OutError outside a function.
	 */
	static UK2Node_Variable* SpawnLocalVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FBPVariableDescription& LocalVariable,
		bool bSet, const FVector2D& Location, FString& OutError);

	/**
	 * A comment box with these bounds and settings around the nodes, through the schema's template path
	 * (FEdGraphSchemaAction_NewNode::CreateNode, which FEdGraphSchemaAction_K2AddComment uses), so it is
	 * RF_Transactional and has a GUID. Null when Graph is null.
	 */
	static UEdGraphNode_Comment* SpawnCommentNode(UEdGraph* Graph, const FSlateRect& Bounds, const FString& Text,
		const FLinearColor& Color, int32 FontSize, bool bGroupMovement, const TArray<UEdGraphNode*>& Enclosed);

	/** The node's GUID with hyphens (EGuidFormats::DigitsWithHyphens), the variable node tools' nodeId */
	static FString NodeId(const UEdGraphNode* Node);

	/**
	 * Adds the node fields add-bp-node-to-active-graph returns: nodeGuid (32 digits), nodeName (the title),
	 * inputPins and outputPins (each pinGuid, pinName, displayName, type, and an input's defaultValue), visible pins only
	 */
	static void AddNodeFields(FJsonObject& Result, const UEdGraphNode* Node);

	/**
	 * Every pin of the node, for the variable node tools' pins array: id (pin GUID), name, direction (input/output),
	 * type, friendlyName, and defaultValue on an input that has one
	 */
	static TArray<TSharedPtr<FJsonValue>> PinsToJson(const UEdGraphNode* Node);

	/**
	 * The variable node tools' variableType: category (the pin category), subCategory, typeObject and typeName when
	 * set, and isArray, isSet, isMap
	 */
	static TSharedPtr<FJsonObject> VariableTypeJson(const FEdGraphPinType& PinType);

	/**
	 * What create-get-member-variable-node and create-set-member-variable-node do once they have the graph: find the
	 * member variable (FN2CMcpVariableUtils::FindMemberVariableProperty, so Set refuses read-only properties), spawn
	 * the node in one transaction, put InputValue on a Set node's value pin when not empty, mark the Blueprint
	 * modified (it compiles when the tool call ends) and return the result object. Null with OutError on failure,
	 * with nothing changed.
	 */
	static TSharedPtr<FJsonObject> CreateMemberVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& VariableName,
		bool bSet, const FVector2D& Location, const FString& InputValue, FString& OutError);

	/**
	 * The same for create-get-local-function-variable-node and create-set-local-function-variable-node: the local of
	 * the function Graph is in (a collapsed graph inside a function counts as that function)
	 */
	static TSharedPtr<FJsonObject> CreateLocalVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& VariableName,
		bool bSet, const FVector2D& Location, const FString& InputValue, FString& OutError);
};
