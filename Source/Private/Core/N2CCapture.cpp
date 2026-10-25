// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CCapture.h"

#include "Core/N2CCaptureContext.h"
#include "Core/N2CNodeCollector.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"

namespace N2C
{
	TValueOrError<FN2CCapture, FString> CaptureNodes(TConstArrayView<UK2Node*> Nodes, const FN2CCaptureOptions& Options)
	{
		FN2CCaptureContext Context(Options);
		if (!Context.CaptureNodes(Nodes))
		{
			return MakeError(Nodes.Num() == 0 ? FString(TEXT("No nodes to capture")) : FString(TEXT("No graph was captured")));
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
