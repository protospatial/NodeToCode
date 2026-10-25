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
};

/** The result of one capture. Owns everything it describes; nothing is shared with any other capture. */
struct NODETOCODE_API FN2CCapture
{
	/** The captured graph first, then the nested graphs shallowest first, the related structs and enums */
	FN2CBlueprint Blueprint;

	/** Node GUID -> the node's ID in Blueprint (N1, N2, ...) */
	TMap<FGuid, FString> NodeIDMap;

	/** Pin GUID -> the pin's ID in Blueprint (P1, P2, ...) */
	TMap<FGuid, FString> PinIDMap;
};

/**
 * Blueprint capture: turns editor nodes into the N2C model (FN2CBlueprint) that FN2CSerializer writes as N2C JSON.
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
	 * Describes one node on its own (the MCP add-node and search tools): a fresh context, no nested-graph discovery
	 * and no related types, so it never sees or changes any capture. NodeID, when given, replaces the generated ID.
	 */
	NODETOCODE_API TValueOrError<FN2CNodeDefinition, FString> DescribeNode(UK2Node& Node, const FString& NodeID = FString());
}
