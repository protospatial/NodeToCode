// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * Node deletion shared by the delete-blueprint-node MCP tool and the Python bridge's DeleteNodes (card 0279). It
 * deletes the way the Blueprint editor's Delete command does: the node's own CanUserDeleteNode decides what is
 * protected (so the entry and exit tunnels of a collapsed graph or macro are refused and a collapsed-graph node is
 * not), FBlueprintEditorUtils::RemoveNode removes each node (breakpoints, pin watches, the schema's BreakNodeLinks),
 * and the Blueprint is marked structurally modified once at the end.
 *
 * Preserving connections: before a node is removed, every link that runs through it is recorded. For each linked
 * input pin, the node's pass-through pin (UEdGraphNode::GetPassThroughPin: the one exec output of a node with one exec
 * input and one exec output, or a reroute knot's output) gives the output it would pass to. After the removal each
 * source of the input is connected to each target of that output through the schema (CanCreateConnection, then
 * TryCreateConnection). A connection that would replace a link the schema refuses or that would cut another link is
 * not made. Pins with no pass-through (data pins of most nodes, a Branch's exec outputs) are not bridged.
 */
namespace N2CNodeDeletion
{
	/** One link made around a deleted node */
	struct FBridgedLink
	{
		FString FromNodeGuid;
		FString FromPinName;
		FString ToNodeGuid;
		FString ToPinName;
		/** The source pin's category: exec, bool, object, ... */
		FString PinType;
	};

	/** What was deleted, read before the node went */
	struct FDeletedNode
	{
		FString NodeGuid;
		/** ENodeTitleType::FullTitle */
		FString Title;
		/** ENodeTitleType::ListView */
		FString ListTitle;
		FString NodeType;
		FString GraphName;
		TArray<FBridgedLink> BridgedLinks;
	};

	/**
	 * Whether the node may be deleted: its CanUserDeleteNode(), which bForce overrides (and nothing else). On false
	 * OutReason says why, naming the node and the force flag.
	 */
	NODETOCODE_API bool CanDelete(const UEdGraphNode* Node, bool bForce, FString& OutReason);

	/** The node of Graph with this GUID, or null */
	NODETOCODE_API UEdGraphNode* FindNode(const UEdGraph* Graph, const FGuid& NodeGuid);

	/**
	 * Removes the nodes from Blueprint, in order, bridging their links first when bPreserveConnections, and marks the
	 * Blueprint structurally modified once (FN2CMcpBlueprintUtils::MarkModified: one compile per tool call). The
	 * caller checks CanDelete and holds the undo transaction. Returns what was deleted, in order.
	 */
	NODETOCODE_API TArray<FDeletedNode> DeleteNodes(UBlueprint* Blueprint, const TArray<UEdGraphNode*>& Nodes, bool bPreserveConnections);
}
