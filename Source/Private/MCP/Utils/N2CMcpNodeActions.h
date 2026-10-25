// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UBlueprintNodeSpawner;
class UEdGraph;
class UEdGraphNode;
struct FEdGraphSchemaAction;
struct FBlueprintActionMenuBuilder;

/**
 * Blueprint action-menu helpers shared by search-blueprint-nodes, add-bp-node-to-active-graph and the Python bridge's
 * search_blueprint_nodes / add_node_to_graph (card 0277).
 *
 * Action IDs: an action's ID is its node spawner's signature string (UBlueprintNodeSpawner::GetSpawnerSignature,
 * FBlueprintNodeSignature::ToString), for example
 *   (FieldName="/Script/Engine.KismetSystemLibrary:PrintString",NodeName="/Script/BlueprintGraph.K2Node_CallFunction")
 * It names the node class and the field the node is made from, not display text, so it does not change with the
 * editor language, the search context, a capture or a compile, and it never holds a carriage return or any other
 * control character. An action without a spawner signature gets "menu:<category>|<menu name>" instead.
 *
 * Identifiers from before card 0277 (the action's full search text with each line break written as '>') are still
 * accepted by ResolveAction, with or without the '\r' Windows put before each '>'.
 */
namespace N2CNodeActions
{
	/** The node spawner behind a Blueprint action-menu item (the sample spawner of a drag-drop item), or null */
	NODETOCODE_API const UBlueprintNodeSpawner* GetSpawner(const FEdGraphSchemaAction& Action);

	/** The action's stable ID (see above); empty only for a null action */
	NODETOCODE_API FString GetActionId(const TSharedPtr<FEdGraphSchemaAction>& Action);

	/** The pre-0277 identifier of an action (full search text, line breaks as '>'), with every '\r' removed */
	NODETOCODE_API FString GetLegacyIdentifier(const FEdGraphSchemaAction& Action);

	/** Builds the action menu list for a graph the way the editor's palette does (no context: the global list) */
	NODETOCODE_API void BuildActionList(FBlueprintActionMenuBuilder& MenuBuilder, UBlueprint* Blueprint, UEdGraph* Graph);

	/**
	 * Finds the action an ID (or a pre-0277 identifier) names in a built list. NodeName only breaks a tie between
	 * different spawners that share a signature. On failure returns null and sets OutError
	 * ("ACTION_NOT_FOUND: ..." or "AMBIGUOUS_ACTION: ...").
	 */
	NODETOCODE_API TSharedPtr<FEdGraphSchemaAction> ResolveAction(FBlueprintActionMenuBuilder& MenuBuilder,
		const FString& ActionId, const FString& NodeName, FString& OutError, bool* bOutLegacy = nullptr);

	/** True when the action's menu category (FEdGraphSchemaAction::GetCategory) contains Filter, case-insensitively */
	NODETOCODE_API bool MatchesCategory(const FEdGraphSchemaAction& Action, const FString& Filter);

	/** True for the low-level type-specific math functions search leaves out by default (Multiply_FloatFloat, ...) */
	NODETOCODE_API bool IsExcludedVMFunction(const FEdGraphSchemaAction& Action);

	/** True when every space-separated word of SearchTerm is in the action's full search text, case-insensitively */
	NODETOCODE_API bool MatchesSearchTerm(const FEdGraphSchemaAction& Action, const TArray<FString>& LowerTerms);

	/** SearchTerm split on spaces, lower case */
	NODETOCODE_API TArray<FString> TokenizeSearchTerm(const FString& SearchTerm);

	/** Adds menuName, category, tooltip and keywords (the action's own fields, not its search blob) to Object */
	NODETOCODE_API void AddDescriptionFields(const FEdGraphSchemaAction& Action, FJsonObject& Object);

	/**
	 * Performs the action in Graph (the engine's own "Add Node" transaction) and returns the new node. On failure
	 * returns null and sets OutError.
	 */
	NODETOCODE_API UEdGraphNode* SpawnAction(const TSharedPtr<FEdGraphSchemaAction>& Action, UEdGraph* Graph,
		const FVector2f& Location, FString& OutError);
}
