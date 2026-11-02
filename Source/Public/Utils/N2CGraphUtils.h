// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;

/** Why a tagged graph did not resolve (FN2CGraphUtils::ResolveTaggedGraph) */
enum class EN2CGraphResolveError : uint8
{
	None,
	/** The tag's Blueprint path loads no Blueprint (deleted, moved or renamed) */
	BlueprintNotFound,
	/** The tag's GraphGuid is not a GUID */
	InvalidGuid,
	/** No graph of the Blueprint has that GraphGuid (the graph was deleted, or the tag predates a copy) */
	GraphNotFound
};

/**
 * What NodeToCode makes of a graph's type (decision 0008, card 0248), decided by the graph's schema class, never by its
 * name. Every Blueprint (K2) graph is supported, including transition and conduit rules (pure boolean K2 graphs whose
 * TransitionResult node is a result boundary). Refused: animation pose graphs (AnimGraph, anim layers, states, custom
 * blends: UAnimationGraphSchema and its subclasses, which derive from UEdGraphSchema_K2 and so pass an IsA K2 check),
 * animation state machines, Control Rig and other RigVM graphs, and any other non-K2 graph.
 */
struct NODETOCODE_API FN2CGraphTypeInfo
{
	/**
	 * The kind the schema decides, as list-blueprint-graphs reports it: "animgraph", "state-machine", "state",
	 * "transition", "conduit", "rig" or "other" (a non-K2 graph of another editor); empty for an ordinary K2 graph,
	 * whose kind comes from where the Blueprint keeps it (FN2CGraphTarget::GraphKind)
	 */
	FString Kind;
	/** For messages, e.g. "an AnimGraph (an animation pose graph)"; empty for an ordinary K2 graph */
	FString Description;
	bool bSupported = true;
};

/**
 * One graph resolver for the editor UI and translation (card 0251): the main window, the batch orchestrator and token
 * estimation find a tagged graph through it. A graph is found among every graph of its Blueprint (UBlueprint::
 * GetAllGraphs, through FN2CGraphTarget, the MCP tools' resolver): event graphs, functions, macros, delegate
 * signatures, interface implementations and the graphs nested in them (collapsed graphs, AnimBP state machines,
 * math expressions), so any graph the overlay can tag can be found again.
 */
struct NODETOCODE_API FN2CGraphUtils
{
	/**
	 * The graph of the Blueprint with this GraphGuid, or null. A GraphGuid is unique only within one Blueprint (a
	 * duplicated Blueprint keeps its graphs' GUIDs); when graphs copied inside one Blueprint share it, the first in
	 * GetAllGraphs order.
	 */
	static UEdGraph* FindGraphByGuid(const UBlueprint* Blueprint, const FGuid& GraphGuid);

	/**
	 * Loads the Blueprint at BlueprintPath and finds the graph with GraphGuid. On failure returns null, sets OutError
	 * (when given) and OutMessage to a sentence naming the graph (GraphName, as the tag recorded it) and the Blueprint.
	 * OutBlueprint is the loaded Blueprint (null when it did not load).
	 */
	static UEdGraph* ResolveTaggedGraph(const FString& BlueprintPath, const FString& GraphGuid, const FString& GraphName,
		UBlueprint*& OutBlueprint, FString& OutMessage, EN2CGraphResolveError* OutError = nullptr);

	/**
	 * Whether translation (capture, Copy JSON, the token estimate) can read this graph: it belongs to a Blueprint, its
	 * GraphGuid finds it again in that Blueprint, and ClassifyGraphType supports its type. That takes in event graphs,
	 * functions, macros, collapsed graphs, interface implementations and transition rules; it leaves out AnimGraphs,
	 * states, state machines, Control Rig graphs and any other non-K2 graph (Sound Cue, Material, Niagara, Behavior
	 * Tree graphs). OutReason (when given) says why not, for a tooltip or notification.
	 */
	static bool CanTranslateGraph(const UEdGraph* Graph, FString* OutReason = nullptr);

	/** The graph's type as NodeToCode sees it (decision 0008): by schema class only */
	static FN2CGraphTypeInfo ClassifyGraphType(const UEdGraph* Graph);

	/** "NodeToCode does not translate graph 'AnimGraph': it is an AnimGraph ..." for a graph ClassifyGraphType refuses */
	static FString DescribeUnsupportedGraph(const UEdGraph& Graph, const FN2CGraphTypeInfo& Info);

	/**
	 * The tools' error for a refused graph: "UNSUPPORTED_GRAPH_TYPE: " and DescribeUnsupportedGraph; empty when the
	 * graph's type is supported
	 */
	static FString UnsupportedGraphTypeError(const UEdGraph& Graph);

	/** "'Name' in Blueprint '/Game/BP_Door.BP_Door'" for messages */
	static FString DescribeGraph(const FString& GraphName, const FString& BlueprintPath);

	/** Logs Message as a warning and shows it as an editor notification (a few seconds, error style). Game thread. */
	static void NotifyFailure(const FString& Message);
};
