// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CCapture.h"

#include "Algo/AllOf.h"
#include "Core/N2CCaptureContext.h"
#include "Core/N2CNodeCollector.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"

const FString* FN2CCapture::FindNodeID(const FGuid& NodeGuid) const
{
	for (const TPair<FString, FGuid>& Node : NodeGuids)
	{
		if (Node.Value == NodeGuid)
		{
			return &Node.Key;
		}
	}
	return nullptr;
}

namespace N2C
{
	TValueOrError<FN2CCapture, FString> CaptureNodes(TConstArrayView<UK2Node*> Nodes, const FN2CCaptureOptions& Options)
	{
		FN2CCaptureContext Context(Options);
		if (!Context.CaptureNodes(Nodes))
		{
			if (Nodes.Num() == 0)
			{
				return MakeError(FString(TEXT("No nodes to capture")));
			}
			const bool bOnlyGhosts = Algo::AllOf(Nodes, [](const UK2Node* Node) { return Node && Node->IsAutomaticallyPlacedGhostNode(); });
			return MakeError(bOnlyGhosts
				? FString(TEXT("Nothing to capture: the graph holds only unwired default events (ghost nodes)"))
				: FString(TEXT("No graph was captured")));
		}
		return MakeValue(Context.TakeCapture());
	}

	TValueOrError<FN2CCapture, FString> CaptureGraph(const UEdGraph& Graph, const FN2CCaptureOptions& Options)
	{
		// The collector only reads the graph; its signature predates const-correct callers
		TArray<UK2Node*> Nodes;
		if (!FN2CNodeCollector::Get().CollectNodesFromGraph(const_cast<UEdGraph*>(&Graph), Nodes))
		{
			return MakeError(FString::Printf(TEXT("Failed to collect the nodes of graph %s"), *Graph.GetName()));
		}
		return CaptureNodes(Nodes, Options);
	}

	TValueOrError<FN2CCapture, FString> CaptureGraphs(TConstArrayView<UEdGraph*> Graphs, const FN2CCaptureOptions& Options)
	{
		FN2CCaptureContext Context(Options);
		if (!Context.CaptureGraphs(Graphs))
		{
			return MakeError(FString(TEXT("Nothing to capture: none of the graphs has a node")));
		}
		return MakeValue(Context.TakeCapture());
	}

	TValueOrError<FN2CNodeDefinition, FString> DescribeNode(UK2Node& Node, const FString& NodeID)
	{
		FN2CCaptureOptions Options;
		Options.Depth = 0;
		Options.bDiscoverNestedGraphs = false;
		Options.bIncludeRelatedTypes = false;
		FN2CCaptureContext Context(Options);

		FN2CNodeDefinition NodeDef;
		NodeDef.ID = NodeID;
		if (!Context.DescribeNode(&Node, NodeDef))
		{
			return MakeError(FString::Printf(TEXT("Node %s could not be described"), *Node.GetName()));
		}
		return MakeValue(MoveTemp(NodeDef));
	}
}
