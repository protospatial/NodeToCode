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
	// ========== Tool catalog ==========

	/**
	 * Run one of NodeToCode's MCP tools by name through the tool catalog (card 0342), so Python gets the tool's own
	 * behaviour: the same pin matching, compile reporting, Play In Editor rule, argument check and untrusted-data marking
	 * an MCP client gets. Every registered tool is reachable, whatever the MCP server serves (its tool profile, the tools
	 * left to Unreal MCP, or the server being off), except the PythonTools toolset (run Python directly). The tool runs
	 * inline on the game thread; a long-running tool (translation) is refused with NOT_RUNNABLE_HERE.
	 *
	 * @param ToolName The tool's name, e.g. "connect-pins" (Context/mcp-server/tools.generated.md lists them)
	 * @param ArgumentsJson The tool's arguments as a JSON object, e.g. {"nodeGuids": ["..."]}; empty means {}
	 * @return {"success": true, "data": <the tool's value: its JSON object or array, else its text>, "error": null} or
	 *         {"success": false, "data": null, "error": "<the tool's error text>", "code": "UNKNOWN_TOOL" |
	 *         "INVALID_ARGUMENT" | "PIE_ACTIVE" | "NOT_RUNNABLE_HERE" | "TOOL_FAILED" | ..., "field": "<argument>"}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString InvokeTool(const FString& ToolName, const FString& ArgumentsJson = TEXT("{}"));

	/** InvokeTool with the arguments as an object (not reflected): what the forwarding functions below call */
	static FString InvokeToolWith(const FString& ToolName, const TSharedRef<FJsonObject>& Arguments);

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
	 * Read any Blueprint's graphs as N2C JSON by asset path, with no Blueprint editor open (card 0157): the same
	 * document as the MCP get-blueprint-n2cjson tool (nodes and pins with ids {short, guid}, flows, the guids table).
	 *
	 * @param BlueprintPath Asset path, e.g. /Game/Blueprints/BP_Door
	 * @param GraphName One graph by name; with GraphGuid also empty, every graph of the Blueprint
	 * @param GraphGuid One graph by its GraphGuid (wins over GraphName)
	 * @param Depth Max Translation Depth for this read (0-5); -1 uses the project setting
	 * @param bToFile Write the document to Saved/NodeToCode/Exports instead of returning it (there is no size limit here)
	 * @param bCompact Write the file without whitespace (only used with bToFile)
	 * @return {"success", "data": {"blueprintPath", "blueprintName", "graphCount", "nodeCount", "estimatedTokens",
	 *         and "n2c_json": {...}, or with bToFile "path" and "bytes"}, "error"}; errors start with a code
	 *         (ASSET_NOT_FOUND, GRAPH_NOT_FOUND, AMBIGUOUS_GRAPH_NAME, UNSUPPORTED_GRAPH_TYPE, EMPTY_GRAPH, INVALID_ARGUMENT)
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use unreal.get_editor_subsystem(unreal.N2CEditorSubsystem).get_blueprint_n2c_json, which returns a struct and raises on failure (card 0301)"))
	static FString GetBlueprintN2CJson(const FString& BlueprintPath, const FString& GraphName = TEXT(""),
		const FString& GraphGuid = TEXT(""), int32 Depth = -1, bool bToFile = false, bool bCompact = true);

	/**
	 * List every graph of a Blueprint by asset path, with no Blueprint editor open (card 0157), as the MCP
	 * list-blueprint-graphs tool does.
	 *
	 * @return {"success", "data": {"blueprintPath", "blueprintName", "graphCount", "graphs": [{"name", "graphGuid",
	 *         "kind", "nodeCount", "readOnly", "parentGraph" (collapsed graphs)}]}, "error"}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use unreal.get_editor_subsystem(unreal.N2CEditorSubsystem).list_blueprint_graphs, which returns a struct and raises on failure (card 0301)"))
	static FString ListBlueprintGraphs(const FString& BlueprintPath);

	/**
	 * The dependency index over asset paths and content folders (card 0319), as the MCP get-blueprint-dependencies
	 * tool returns it, always whole (there is no size limit here).
	 *
	 * @param Paths Asset paths (/Game/Blueprints/BP_Door) and content folders (/Game/Blueprints, with subfolders)
	 * @param bIncludeCallGraph Load the Blueprints for call, macro, cast and variable-type edges (false: AssetRegistry only)
	 * @param bIncludeReferenced Also index the project's own assets the roots depend on outside Paths
	 * @param bToFile Also write the JSON export under Saved/NodeToCode/Dependencies (savedTo, bytes)
	 * @return {"success", "data": {"roots", "nodeCount", "edgeCount", "nodes": [{"path", "name", "kind", "root"}],
	 *         "edges": [{"from", "to", "kind", "graph", "node"}], "order", "cycleGroups", "warnings", "inline",
	 *         "savedTo", "bytes"}, "error"}; errors start with INVALID_ARGUMENT, NOT_FOUND or EXPORT_FAILED
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString GetBlueprintDependencies(const TArray<FString>& Paths, bool bIncludeCallGraph = true,
		bool bIncludeReferenced = true, bool bToFile = false);

	/**
	 * Read N2C JSON text (a saved N2C_BP_*.json, get_focused_blueprint_json's "n2c_json", or the agent document of
	 * get_blueprint_n2c_json, whose "ids" {"short", "guid"} are read as the IDs) with NodeToCode's reader.
	 * Reads version 1.x (1.2.x and earlier 2.0 builds) and 2.x into the current model; the result is that model
	 * written as N2C JSON of the current version (a 1.x document comes back as 2.0.0 with the same nodes and flows;
	 * its node-only exec entries "N1->N2" stay node-only, as the document never said which pins they use).
	 *
	 * @param N2CJson The N2C JSON text.
	 * @return JSON string: {"success": true, "data": {"source_version": "1.0.0", "version": "2.0.0", "valid": bool,
	 *         "graphs": n, "nodes": n, "data_edges": n, "exec_edges": n, "n2c_json": {...}}, "error": null}, or an
	 *         error when the text is not N2C JSON, or when a graph, node or pin had to be skipped as malformed (the
	 *         error names them; "data" still holds the rest of the read, with its "warnings").
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString ReadN2CJson(const FString& N2CJson);

	/**
	 * Compile the currently focused Blueprint.
	 *
	 * @param BlueprintPath Optional Blueprint asset path; empty: the focused Blueprint (card 0274)
	 * @return JSON string with compilation result.
	 *         Format: {"success": true/false, "data": {"status": "..."}, "error": "..." or null}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use unreal.get_editor_subsystem(unreal.N2CEditorSubsystem).compile_blueprint, which returns a struct and raises on failure (card 0301)"))
	static FString CompileFocusedBlueprint(const FString& BlueprintPath = TEXT(""));

	/**
	 * Save the currently focused Blueprint to disk.
	 *
	 * @param bOnlyIfDirty If true, only saves if the Blueprint has unsaved changes.
	 * @param BlueprintPath Optional Blueprint asset path; empty: the focused Blueprint (card 0274)
	 * @return JSON string with save result.
	 *         Format: {"success": true/false, "data": {"was_saved": true/false}, "error": "..." or null}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use unreal.get_editor_subsystem(unreal.N2CEditorSubsystem).save_blueprint, which returns a struct and raises on failure (card 0301)"))
	static FString SaveFocusedBlueprint(bool bOnlyIfDirty = true, const FString& BlueprintPath = TEXT(""));

	// ========== Tagging System ==========

	/**
	 * Tag the currently focused Blueprint graph.
	 *
	 * @param Tag The tag name to apply
	 * @param Category Optional category (default: "Default")
	 * @param Description Optional description
	 * @param BlueprintPath Optional Blueprint asset path; empty: the focused Blueprint (card 0274)
	 * @param Graph Optional graph of that Blueprint (or of the focused one): its GUID or its name; empty: the
	 *        focused graph, or with BlueprintPath alone its focused graph, else its event graph
	 * @return JSON string with tag info or error
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use unreal.get_editor_subsystem(unreal.N2CEditorSubsystem).tag_graph, which returns a struct and raises on failure (card 0301)"))
	static FString TagFocusedGraph(const FString& Tag, const FString& Category = TEXT("Default"), const FString& Description = TEXT(""),
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * List all tags, optionally filtered by category or tag name.
	 *
	 * @param Category Optional category filter (empty string for all)
	 * @param Tag Optional tag name filter (empty string for all)
	 * @param BlueprintPath Optional: only the tags of this Blueprint's graphs (object or package path). A duplicated
	 *        Blueprint shares its graph GUIDs with the original, so this is how to tell their tags apart (card 0498)
	 * @return JSON array of matching tags
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString ListTags(const FString& Category = TEXT(""), const FString& Tag = TEXT(""),
		const FString& BlueprintPath = TEXT(""));

	/**
	 * Remove a tag from a graph.
	 *
	 * @param GraphGuid The GUID of the graph (string format)
	 * @param Tag The tag name to remove
	 * @param BlueprintPath The owning Blueprint's path (blueprint_path from ListTags or TagFocusedGraph). The graph is
	 *        {Blueprint, GraphGuid} (FN2CGraphKey, card 0498): copies of a Blueprint share graph GUIDs, so without it
	 *        the removal works only while one Blueprint has a graph with this GUID
	 * @return JSON result with removal status
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString RemoveTag(const FString& GraphGuid, const FString& Tag, const FString& BlueprintPath = TEXT(""));

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

	/**
	 * The languages NodeToCode translates Blueprints into, as the MCP get-available-translation-targets tool lists
	 * them (card 0175).
	 *
	 * @return {"success", "data": {"languages": [{"id", "displayName", "description", "fileExtensions", "category",
	 *         "features", "syntaxHighlightingSupported"}], "defaultLanguage", "currentLanguage", "languageCount"},
	 *         "error"}
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString GetTranslationTargets();

	/**
	 * Marks text taken from a project's Blueprints as untrusted data, exactly as NodeToCode's MCP server marks its
	 * results (card 0124): a JSON object gains "untrusted_content" as its first key, other text is prefixed with the
	 * notice, and "</" is written "<\/". Text that already carries the mark comes back unchanged.
	 *
	 * @param Text A tool result: N2C JSON or any other text that carries Blueprint strings.
	 * @return The marked text.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString MarkUntrustedContent(const FString& Text);

	/** The notice MarkUntrustedContent puts before Blueprint text (card 0124), for results that are not text. */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static FString GetUntrustedContentNotice();

	// ========== MCP runner ==========

	/**
	 * Where nodetocode._mcp_runner hands an answer of run-python (card 0299): the strict JSON
	 * {success, data, error, traceback, output, stderr} under the call's id, which the tool reads once with
	 * TakeMcpResult after the command returns. The C++ side never scrapes stdout, so printed text cannot spoof an
	 * answer. Not for scripts: an id no tool is waiting for is kept only until the next answer is taken.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python")
	static void SubmitMcpResult(const FString& CallId, const FString& ResultJson);

	/** The answer SubmitMcpResult stored under CallId, removed from the store; false when there is none. Game thread */
	static bool TakeMcpResult(const FString& CallId, FString& OutResultJson);

	// ========== Blueprint Editor Navigation ==========

	/**
	 * Deprecated (card 0343): runs the MCP tool open-blueprint-asset through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: asset_path, focus_graph when FocusGraph is given. Result fields: Context/mcp-server/tools.generated.md#open-blueprint-asset
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.open_blueprint, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString OpenBlueprint(const FString& BlueprintPath, const FString& FocusGraph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool open-blueprint-function through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: graphName. Result fields: Context/mcp-server/tools.generated.md#open-blueprint-function
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.open_blueprint_function, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString OpenBlueprintFunction(const FString& FunctionName);

	// ========== Blueprint Graph Editing ==========

	/**
	 * Deprecated (card 0343): runs the MCP tool search-blueprint-nodes through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: searchTerm, contextSensitive, maxResults, category, excludeVMFunctions and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#search-blueprint-nodes
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.search_blueprint_nodes, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString SearchBlueprintNodes(const FString& SearchTerm, bool bContextSensitive = true, int32 MaxResults = 20,
		const FString& CategoryFilter = TEXT(""), bool bExcludeVMFunctions = true,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool add-bp-node-to-active-graph through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: nodeName, actionIdentifier, location {x, y} and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#add-bp-node-to-active-graph
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.add_node_to_graph, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString AddNodeToGraph(const FString& NodeName, const FString& ActionIdentifier, float LocationX = 0.0f, float LocationY = 0.0f,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool connect-pins through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: connections (ConnectionsJson, a JSON array), options.breakExistingLinks and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#connect-pins
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.connect_pins, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString ConnectPins(const FString& ConnectionsJson, bool bBreakExistingLinks = false,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool set-input-pin-value through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: nodeGuid, pinGuid, value, pinName and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#set-input-pin-value
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.set_pin_value, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString SetPinValue(const FString& NodeGuid, const FString& PinGuid, const FString& Value, const FString& PinName = TEXT(""),
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool delete-blueprint-node through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: nodeGuids (NodeGuidsJson, a JSON array), preserveConnections, force and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#delete-blueprint-node
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.delete_nodes, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString DeleteNodes(const FString& NodeGuidsJson, bool bPreserveConnections = false, bool bForce = false,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool find-nodes-in-graph through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: searchTerms (SearchTermsJson, a JSON array), searchType, caseSensitive, maxResults and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#find-nodes-in-graph
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.find_nodes_in_graph, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString FindNodesInGraph(const FString& SearchTermsJson, const FString& SearchType = TEXT("keyword"), bool bCaseSensitive = false, int32 MaxResults = 50,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool create-comment-node through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: nodeGuids (NodeGuidsJson, a JSON array), commentText, color {r, g, b}, fontSize, moveMode, padding and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#create-comment-node
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.create_comment_node, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString CreateCommentNode(const FString& NodeGuidsJson, const FString& CommentText = TEXT("Comment"), float ColorR = 0.075f, float ColorG = 0.075f, float ColorB = 0.075f, int32 FontSize = 18, const FString& MoveMode = TEXT("group"), float Padding = 50.0f,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	// ========== Function Pin Management ==========

	/**
	 * Deprecated (card 0343): runs the MCP tool add-function-input-pin through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: pinName, typeIdentifier, defaultValue, isPassByReference, tooltip and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#add-function-input-pin
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.add_function_input_pin, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString AddFunctionInputPin(const FString& PinName, const FString& TypeIdentifier, const FString& DefaultValue = TEXT(""), bool bIsPassByReference = false, const FString& Tooltip = TEXT(""),
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool add-function-return-pin through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: pinName, typeIdentifier, tooltip and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#add-function-return-pin
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.add_function_return_pin, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString AddFunctionReturnPin(const FString& PinName, const FString& TypeIdentifier, const FString& Tooltip = TEXT(""),
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool remove-function-entry-pin through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: pinName and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#remove-function-entry-pin
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.remove_function_entry_pin, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString RemoveFunctionEntryPin(const FString& PinName,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

	/**
	 * Deprecated (card 0343): runs the MCP tool remove-function-return-pin through InvokeToolWith and returns InvokeTool's answer,
	 * {"success", "data": <the tool's result>, "error"}; "success" is false also when the tool's result says
	 * "success": false. Arguments: pinName and the target. BlueprintPath and Graph (a GraphGuid or a graph name) are the target;
	 * both empty act on the focused graph. Result fields: Context/mcp-server/tools.generated.md#remove-function-return-pin
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Python", meta = (DeprecatedFunction, DeprecationMessage = "Use nodetocode.remove_function_return_pin, or InvokeTool with its MCP tool; this function forwards to that tool and returns InvokeTool's answer (card 0343)"))
	static FString RemoveFunctionReturnPin(const FString& PinName,
		const FString& BlueprintPath = TEXT(""), const FString& Graph = TEXT(""));

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
