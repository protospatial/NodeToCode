// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Templates/ValueOrError.h"

class UEdGraph;
class UK2Node;

/** What one capture includes */
struct NODETOCODE_API FN2CCaptureOptions
{
	/**
	 * Max Translation Depth for this capture: a nested graph (a user function, macro or collapsed graph the captured
	 * graph uses) N calls away is captured when N <= Depth. INDEX_NONE (the default) reads UN2CSettings::TranslationDepth
	 * when the capture starts.
	 */
	int32 Depth = INDEX_NONE;

	/** Add the Blueprint-defined structs and enums the captured nodes use to FN2CBlueprint::Structs and Enums */
	bool bIncludeRelatedTypes = true;

	/** Queue and capture nested graphs at all (DescribeNode turns this off) */
	bool bDiscoverNestedGraphs = true;

	/**
	 * Keep the editor's automatically placed ghost nodes (a new Blueprint's greyed-out BeginPlay, ActorBeginOverlap
	 * and Tick), marked "disabled". Off for translation, which never sees them (card 0137); the graph-editing MCP tools
	 * (get-focused-blueprint, find-nodes-in-graph) and the Python bridge's GetFocusedBlueprintJson (card 0433) turn it
	 * on, so an agent can find a default event and wire it instead of adding a second one.
	 */
	bool bIncludeGhostNodes = false;

	/**
	 * Capture the Blueprint's member variables (FN2CBlueprint::Variables) and each captured function graph's local
	 * variables (FN2CGraph::LocalVariables), card 0242. Unset (the default) means on for a whole-Blueprint capture
	 * (N2C::CaptureGraphs: get-blueprint-n2cjson and Python get_blueprint_n2c_json without a graph) and off for a capture of one
	 * graph or a node selection (translation, get-focused-blueprint), so snippet translations stay small.
	 */
	TOptional<bool> IncludeVariables;
};

/** Where one captured pin is in the editor. A pin's PinId is only unique on its node (a paste keeps it), so a pin is
 * the pair (NodeGuid, PinGuid). */
struct NODETOCODE_API FN2CCapturedPin
{
	/** The ID of the pin's node in the capture (N1, N2, ...) */
	FString NodeID;
	/** UEdGraphNode::NodeGuid of the pin's node */
	FGuid NodeGuid;
	/** UEdGraphPin::PinId */
	FGuid PinGuid;
};

/** The result of one capture. Owns everything it describes; nothing is shared with any other capture. */
struct NODETOCODE_API FN2CCapture
{
	/** The captured graph first, then the nested graphs shallowest first, the related structs and enums */
	FN2CBlueprint Blueprint;

	/** UEdGraph::GraphGuid of each graph in Blueprint.Graphs, same order */
	TArray<FGuid> GraphGuids;

	/** Node ID (N1, N2, ...) -> the node's NodeGuid: one entry per captured node */
	TMap<FString, FGuid> NodeGuids;

	/** Pin ID (P1, P2, ...) -> its node and PinId: one entry per captured pin */
	TMap<FString, FN2CCapturedPin> Pins;

	/** The ID of the captured node with this NodeGuid, or null */
	const FString* FindNodeID(const FGuid& NodeGuid) const;
};

/**
 * Blueprint capture: turns editor nodes into the N2C model (FN2CBlueprint) that FN2CSerializer writes as N2C JSON.
 *
 * IDs: nodes are N1, N2, ... and pins P1, P2, ..., unique across the whole capture and given graph by graph, node by
 * node in graph order (pins in the node's pin order, hidden pins skipped) before any flow is recorded, so capturing an
 * unchanged graph again gives the same IDs. They are keyed by the node and pin objects, never by GUID alone, and every
 * graph (the captured one included) is captured at most once.
 *
 * Each call builds its result in its own context and returns it by value, so captures never share state: two
 * captures, a capture started while another is running (for example an overlay's token estimate during a
 * translation), or a capture between sending a request and reading its result cannot change one another's output.
 * Game thread only (it reads UObjects).
 */
namespace N2C
{
	/** Captures Nodes (all from one graph, as FN2CNodeCollector collects them) and the nested graphs they use */
	NODETOCODE_API TValueOrError<FN2CCapture, FString> CaptureNodes(TConstArrayView<UK2Node*> Nodes,
		const FN2CCaptureOptions& Options = FN2CCaptureOptions());

	/** Collects Graph's nodes with FN2CNodeCollector and captures them */
	NODETOCODE_API TValueOrError<FN2CCapture, FString> CaptureGraph(const UEdGraph& Graph,
		const FN2CCaptureOptions& Options = FN2CCaptureOptions());

	/**
	 * Captures several graphs of one Blueprint into one result (get-blueprint-n2cjson without a graph, card 0157):
	 * each is a top-level graph at depth 0, in the order given, so a call from one to another is never captured twice;
	 * nested graphs outside the list (another Blueprint's functions and macros) follow within Options.Depth. Graphs
	 * with no nodes are left out; an error when none has any.
	 */
	NODETOCODE_API TValueOrError<FN2CCapture, FString> CaptureGraphs(TConstArrayView<UEdGraph*> Graphs,
		const FN2CCaptureOptions& Options = FN2CCaptureOptions());

	/**
	 * Describes one node on its own (the MCP add-node and search tools): a fresh context, no nested-graph discovery
	 * and no related types, so it never sees or changes any capture. NodeID, when given, replaces the generated ID.
	 */
	NODETOCODE_API TValueOrError<FN2CNodeDefinition, FString> DescribeNode(UK2Node& Node, const FString& NodeID = FString());
}
