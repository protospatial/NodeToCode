// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "N2CPythonBridge.generated.h"

class FJsonObject;
class UBlueprint;
class UEdGraph;

/**
 * Bridge class exposing NodeToCode functionality to Python scripts.
 *
 * These functions are callable from Python via:
 *   unreal.N2CPythonBridge.get_focused_blueprint_json()
 */
UCLASS()
class NODETOCODE_API UN2CPythonBridge : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Get the currently focused Blueprint graph as N2CJSON format.
	 *
	 * Like the MCP get-focused-blueprint tool, the editor's automatically placed ghost default events (a new Actor
	 * Blueprint's greyed-out BeginPlay, ActorBeginOverlap and Tick) are included, marked "disabled": true, so a
	 * script can find the default BeginPlay and wire it instead of adding a second one. Translation leaves them out.
	 *
	 * @return JSON string containing the Blueprint data, or error JSON if no Blueprint is focused or the graph has
	 *         no nodes at all.
	 *         Format: {"success": true/false, "data": {"name", "path", "graph_name", "node_count", "n2c_json": {...}}
	 *         or null, "error": "..." or null}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString GetFocusedBlueprintJson();

	/**
	 * Read N2C JSON text (a saved N2C_BP_*.json, or get_focused_blueprint_json's "n2c_json") with NodeToCode's reader.
	 * Reads version 1.x (1.2.x and earlier 2.0 builds) and 2.x into the current model; the result is that model
	 * written as N2C JSON of the current version (a 1.x document comes back as 2.0.0 with the same nodes and flows;
	 * its node-only exec entries "N1->N2" stay node-only, as the document never said which pins they use).
	 *
	 * @param N2CJson The N2C JSON text.
	 * @return JSON string: {"success": true, "data": {"source_version": "1.0.0", "version": "2.0.0", "valid": bool,
	 *         "graphs": n, "nodes": n, "data_edges": n, "exec_edges": n, "n2c_json": {...}}, "error": null}, or an
	 *         error when the text is not N2C JSON.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString ReadN2CJson(const FString& N2CJson);

	/**
	 * Compile the currently focused Blueprint.
	 *
	 * @return JSON string with compilation result.
	 *         Format: {"success": true/false, "data": {"status": "..."}, "error": "..." or null}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString CompileFocusedBlueprint();

	/**
	 * Save the currently focused Blueprint to disk.
	 *
	 * @param bOnlyIfDirty If true, only saves if the Blueprint has unsaved changes.
	 * @return JSON string with save result.
	 *         Format: {"success": true/false, "data": {"was_saved": true/false}, "error": "..." or null}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString SaveFocusedBlueprint(bool bOnlyIfDirty = true);

	// ========== Tagging System ==========

	/**
	 * Tag the currently focused Blueprint graph.
	 *
	 * @param Tag The tag name to apply
	 * @param Category Optional category (default: "Default")
	 * @param Description Optional description
	 * @return JSON string with tag info or error
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString TagFocusedGraph(const FString& Tag, const FString& Category = TEXT("Default"), const FString& Description = TEXT(""));

	/**
	 * List all tags, optionally filtered by category or tag name.
	 *
	 * @param Category Optional category filter (empty string for all)
	 * @param Tag Optional tag name filter (empty string for all)
	 * @return JSON array of matching tags
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString ListTags(const FString& Category = TEXT(""), const FString& Tag = TEXT(""));

	/**
	 * Remove a tag from a graph.
	 *
	 * @param GraphGuid The GUID of the graph (string format)
	 * @param Tag The tag name to remove
	 * @return JSON result with removal status
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString RemoveTag(const FString& GraphGuid, const FString& Tag);

	// ========== LLM Provider Info ==========

	/**
	 * Get available LLM providers and their configuration.
	 *
	 * @return JSON with provider list, current provider, and status
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString GetLLMProviders();

	/**
	 * Get the currently active LLM provider info.
	 *
	 * @return JSON with current provider name, model, and endpoint
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString GetActiveProvider();

	// ========== Blueprint Editor Navigation ==========

	/**
	 * Open a Blueprint asset in the editor.
	 *
	 * @param BlueprintPath Asset path of the Blueprint (e.g., "/Game/Blueprints/BP_MyActor")
	 * @param FocusGraph Optional graph to focus: the exact name of one of the Blueprint's event, function or macro
	 *        graphs (e.g., "EventGraph", "UserConstructionScript" for the construction script, or a function name).
	 *        A name that matches no graph is not an error: the Blueprint opens and "focusedGraph" comes back empty.
	 * @return JSON with success status and Blueprint info: {"blueprintName", "blueprintPath", "focusedGraph",
	 *         "eventGraphCount", "functionCount"}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString OpenBlueprint(const FString& BlueprintPath, const FString& FocusGraph = TEXT(""));

	/**
	 * Open/focus a function graph in the currently open Blueprint.
	 *
	 * @param FunctionName Name of the function to open
	 * @return JSON with success status and function info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString OpenBlueprintFunction(const FString& FunctionName);

	// ========== Blueprint Graph Editing ==========

	/**
	 * Search for Blueprint nodes/actions matching a search term.
	 *
	 * @param SearchTerm The text query to search for
	 * @param bContextSensitive If true, performs context-sensitive search
	 * @param MaxResults Maximum number of results to return (1-100)
	 * @param CategoryFilter Optional category filter (e.g., "flowcontrol", "operators", "struct")
	 * @param bExcludeVMFunctions If true (default), excludes low-level VM math functions
	 * @return JSON {"nodes": [{"name", "displayName", "spawnMetadata": {"actionIdentifier", "isContextSensitive"}}],
	 *         "count"}. "name" is the action's full search text (lower case: title, keywords and category lines), and
	 *         "displayName" is that text after its last '>' (in practice the same text); neither is a display title.
	 *         Pass "displayName" and "actionIdentifier" to AddNodeToGraph.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString SearchBlueprintNodes(const FString& SearchTerm, bool bContextSensitive = true, int32 MaxResults = 20,
		const FString& CategoryFilter = TEXT(""), bool bExcludeVMFunctions = true);

private:
	/** Check if the action's category matches the filter */
	static bool MatchesCategoryFilter(const FString& ActionSearchText, const FString& CategoryFilter);

	/** Check if the action is a VMFunction that should be excluded */
	static bool IsExcludedVMFunction(const FString& ActionSearchText);

public:

	/**
	 * Add a Blueprint node to the focused graph.
	 *
	 * @param NodeName Text whose space-separated words all appear in the action's search text (a search result's
	 *        "displayName" works); it narrows the actions before the exact ActionIdentifier match
	 * @param ActionIdentifier Identifier from search results spawnMetadata
	 * @param LocationX X position for node placement
	 * @param LocationY Y position for node placement
	 * @return JSON with spawned node info including nodeGuid
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString AddNodeToGraph(const FString& NodeName, const FString& ActionIdentifier, float LocationX = 0.0f, float LocationY = 0.0f);

	/**
	 * Connect pins between Blueprint nodes.
	 *
	 * @param ConnectionsJson JSON array of connection specs: [{"from":{"nodeGuid":"...","pinGuid":"..."},"to":{...}},...]
	 * @param bBreakExistingLinks If true, a connection may replace an existing link (a linked data input, also through a
	 *        conversion node, or a linked exec output); if false such a connection fails and names the existing link
	 * @return JSON with succeeded/failed connections
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString ConnectPins(const FString& ConnectionsJson, bool bBreakExistingLinks = false);

	/**
	 * Set the default value of an input pin on a Blueprint node.
	 *
	 * @param NodeGuid GUID of the node containing the pin
	 * @param PinGuid GUID of the pin to modify
	 * @param Value The value to set (as string)
	 * @param PinName Optional pin name for fallback lookup
	 * @return JSON with old/new values
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString SetPinValue(const FString& NodeGuid, const FString& PinGuid, const FString& Value, const FString& PinName = TEXT(""));

	/**
	 * Delete nodes from the focused Blueprint graph.
	 *
	 * @param NodeGuidsJson JSON array of node GUIDs to delete: ["guid1","guid2",...]
	 * @param bPreserveConnections If true, attempts to bridge connections around deleted nodes
	 * @param bForce If true, bypasses protection checks for entry/result nodes
	 * @return JSON with deleted node info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString DeleteNodes(const FString& NodeGuidsJson, bool bPreserveConnections = false, bool bForce = false);

	/**
	 * Find nodes in the focused graph by keywords or GUIDs.
	 *
	 * @param SearchTermsJson JSON array of search terms: ["term1","term2",...] or ["guid1","guid2",...]; a node matches
	 *        when any term is a substring of its title (as shown and in source text), its class or its member name
	 * @param SearchType Either "keyword" or "guid"
	 * @param bCaseSensitive Whether keyword search is case-sensitive
	 * @param MaxResults Maximum nodes to return (1-200)
	 * @return JSON with matching nodes in N2C format
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString FindNodesInGraph(const FString& SearchTermsJson, const FString& SearchType = TEXT("keyword"), bool bCaseSensitive = false, int32 MaxResults = 50);

	/**
	 * Create a comment node around specified nodes.
	 *
	 * @param NodeGuidsJson JSON array of node GUIDs to encompass
	 * @param CommentText Text for the comment
	 * @param ColorR Red color component (0-1)
	 * @param ColorG Green color component (0-1)
	 * @param ColorB Blue color component (0-1)
	 * @param FontSize Font size (1-1000)
	 * @param MoveMode "group" or "none" for movement behavior
	 * @param Padding Extra padding around nodes
	 * @return JSON with comment node info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString CreateCommentNode(const FString& NodeGuidsJson, const FString& CommentText = TEXT("Comment"), float ColorR = 0.075f, float ColorG = 0.075f, float ColorB = 0.075f, int32 FontSize = 18, const FString& MoveMode = TEXT("group"), float Padding = 50.0f);

	// ========== Function Pin Management ==========

	/**
	 * Add an input parameter to the focused Blueprint function.
	 *
	 * @param PinName Name for the new parameter
	 * @param TypeIdentifier Type identifier (e.g., "bool", "/Script/Engine.Actor")
	 * @param DefaultValue Optional default value
	 * @param bIsPassByReference Whether the parameter is passed by reference
	 * @param Tooltip Optional tooltip description
	 * @return JSON with created pin info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString AddFunctionInputPin(const FString& PinName, const FString& TypeIdentifier, const FString& DefaultValue = TEXT(""), bool bIsPassByReference = false, const FString& Tooltip = TEXT(""));

	/**
	 * Add a return value to the focused Blueprint function.
	 *
	 * @param PinName Name for the new return value
	 * @param TypeIdentifier Type identifier (e.g., "bool", "/Script/Engine.Actor")
	 * @param Tooltip Optional tooltip description
	 * @return JSON with created pin info
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString AddFunctionReturnPin(const FString& PinName, const FString& TypeIdentifier, const FString& Tooltip = TEXT(""));

	/**
	 * Remove an input parameter from the focused Blueprint function.
	 *
	 * @param PinName Name of the pin to remove
	 * @return JSON with removal status
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString RemoveFunctionEntryPin(const FString& PinName);

	/**
	 * Remove a return value from the focused Blueprint function.
	 *
	 * @param PinName Name of the return pin to remove
	 * @return JSON with removal status
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString RemoveFunctionReturnPin(const FString& PinName);

private:
	/**
	 * GetFocusedBlueprintJson's response for one graph: the graph captured with the editor's ghost default events kept
	 * (marked "disabled"), as the MCP get-focused-blueprint tool does, so a new Actor Blueprint's EventGraph reads as
	 * its three default events (card 0433)
	 */
	static FString GraphToJson(UBlueprint* OwningBlueprint, UEdGraph* Graph);

	/**
	 * The one response builder: {"success": bSuccess, "data": Data or null, "error": Error or null}, serialized with a
	 * condensed TJsonWriter so every name, value and message is escaped as JSON (the nodetocode package json.loads it).
	 */
	static FString MakeResultJson(bool bSuccess, const TSharedPtr<FJsonObject>& Data, const FString* Error);

	/** {"success": true, "data": Data, "error": null} */
	static FString MakeSuccessJson(const TSharedPtr<FJsonObject>& Data);

	/** {"success": false, "data": Data or null, "error": ErrorMessage} */
	static FString MakeErrorJson(const FString& ErrorMessage, const TSharedPtr<FJsonObject>& Data = nullptr);
};
