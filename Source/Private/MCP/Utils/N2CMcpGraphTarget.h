// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;
struct FMcpToolCallResult;
struct FMcpToolDefinition;

/** How FN2CGraphTarget::Resolve chose the graph */
enum class EN2CGraphResolvedBy : uint8
{
	/** graphGuid or graphName named it */
	Explicit,
	/** The graph focused in the Blueprint editor */
	Focus,
	/** Only blueprintPath was given and none of its graphs is focused: its event graph */
	EventGraph
};

/**
 * The target arguments every Graph and Functions tool takes (card 0152): all optional. With none of them the tool
 * acts on the focused graph; graphGuid or graphName without blueprintPath name a graph of the focused Blueprint.
 */
struct NODETOCODE_API FN2CGraphTargetArgs
{
	/** Blueprint asset path, '/Game/BP_Door.BP_Door' or '/Game/BP_Door' */
	FString BlueprintPath;
	/** A graph's name (a function, macro, event graph or collapsed graph) */
	FString GraphName;
	/** A graph's UEdGraph::GraphGuid (a function's ID), or a function's old name-hash ID */
	FString GraphGuid;

	bool IsEmpty() const { return BlueprintPath.IsEmpty() && GraphName.IsEmpty() && GraphGuid.IsEmpty(); }

	/** blueprintPath, graphName and graphGuid of a tool's arguments (trimmed; absent fields stay empty) */
	static FN2CGraphTargetArgs FromJson(const TSharedPtr<FJsonObject>& Arguments);
};

/**
 * One resolver for the graph a tool reads or edits (card 0152, design arch-arch-mcp-6). A graph is named by its
 * Blueprint plus its GraphGuid or name: a GraphGuid alone is not unique, since a duplicated Blueprint keeps its graphs'
 * GUIDs. Every graph of the Blueprint resolves (FBlueprintEditorUtils::GetAllGraphs): event graphs, functions, macros,
 * delegate signatures, interface implementations and collapsed graphs.
 *
 * Errors carry a code as the start of Error ("<CODE>: <message>"):
 * - ASSET_NOT_FOUND: blueprintPath loads no Blueprint;
 * - GRAPH_NOT_FOUND: no graph of that GUID or name in the Blueprint (the message lists its graphs), or blueprintPath
 *   alone and the Blueprint has no event graph;
 * - AMBIGUOUS_GRAPH_NAME: more than one graph has that name (two collapsed graphs in different graphs can), or more
 *   than one has that GUID (graphs copied inside one Blueprint); pass the other identifier;
 * - READ_ONLY_GRAPH: a write to a graph the Blueprint editor shows read-only (FBlueprintEditorUtils::IsGraphReadOnly:
 *   interface graphs, delegate signatures, math expressions, graphs that are not editable);
 * - NO_FOCUSED_GRAPH: no target arguments and no graph is focused, or a graph argument without blueprintPath and no
 *   Blueprint is focused.
 */
struct NODETOCODE_API FN2CGraphTarget
{
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	EN2CGraphResolvedBy ResolvedBy = EN2CGraphResolvedBy::Explicit;
	/** The graph was found by a function's old name-hash ID (deprecated, see FN2CMcpFunctionGuidUtils) */
	bool bLegacyId = false;
	/** The ID that was passed, when bLegacyId */
	FString LegacyId;
	/** "<CODE>: <message>" when resolution failed; empty on success */
	FString Error;

	bool IsValid() const { return Blueprint != nullptr && Graph != nullptr && Error.IsEmpty(); }
	/** The error code (ASSET_NOT_FOUND, GRAPH_NOT_FOUND, ...), empty on success */
	FString ErrorCode() const;

	/**
	 * Resolves the target. With no argument: the focused graph. With blueprintPath alone: its focused graph when one
	 * of its graphs is focused, else its event graph. graphGuid wins over graphName when both are given. bForWrite
	 * refuses read-only graphs (READ_ONLY_GRAPH). Game thread.
	 */
	static FN2CGraphTarget Resolve(const FN2CGraphTargetArgs& Args, bool bForWrite);
	static FN2CGraphTarget Resolve(const TSharedPtr<FJsonObject>& Arguments, bool bForWrite);

	/**
	 * The Blueprint of an asset path (with or without the '.Name' object part); null and an ASSET_NOT_FOUND error
	 * when nothing loads. An empty path is the focused Blueprint (NO_FOCUSED_GRAPH when there is none).
	 */
	static UBlueprint* LoadBlueprint(const FString& BlueprintPath, FString& OutError);

	/** Every graph of the Blueprint with this name (case-insensitive), in GetAllGraphs order */
	static TArray<UEdGraph*> FindGraphsByName(const UBlueprint* Blueprint, const FString& GraphName);
	/** Every graph of the Blueprint whose GraphGuid is this one */
	static TArray<UEdGraph*> FindGraphsByGuid(const UBlueprint* Blueprint, const FGuid& GraphGuid);

	/** event, function, macro, collapsed, delegate, interface or graph */
	static FString GraphKind(const UEdGraph* Graph);
	/** "explicit", "focus" or "eventGraph" */
	static FString ResolvedByToString(EN2CGraphResolvedBy ResolvedBy);

	/**
	 * {blueprintPath, blueprintName, graphName, graphGuid, graphKind, resolvedBy}, plus deprecatedId and warning when
	 * the graph was found by an old function ID
	 */
	TSharedRef<FJsonObject> ToJson() const;

	/**
	 * Adds ToJson() to a tool result as its "target" field when the result's text is a JSON object, otherwise (an error
	 * text) as an extra text item {"target": ...}. Does nothing when the target did not resolve.
	 */
	void AttachTo(FMcpToolCallResult& Result) const;

	/** AttachTo, then the result: `return Target.Finish(FMcpToolCallResult::CreateTextResult(Json));` */
	FMcpToolCallResult Finish(FMcpToolCallResult Result) const;

	/**
	 * Adds the optional blueprintPath, graphName and graphGuid properties to a tool's input schema (properties it
	 * already declares are left as they are). bGraphArguments false adds blueprintPath only.
	 */
	static void AddSchemaProperties(FMcpToolDefinition& Definition, bool bGraphArguments = true);

	/** The warning a result carries for an old function ID, naming the ID to use instead */
	static FString LegacyIdWarning(const FString& OldId, const UEdGraph* Graph);
};
