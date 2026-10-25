// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CLiveness.h"

#include "Models/N2CBlueprint.h"
#include "Models/N2CJsonContract.h"

namespace N2CLiveness
{
	namespace
	{
		struct FExecPins
		{
			int32 Inputs = 0;
			int32 Outputs = 0;

			bool IsImpure() const { return Inputs + Outputs > 0; }
			bool IsEntry() const { return Outputs > 0 && Inputs == 0; }
			/** Exec passes through a disabled node only with one exec input and one exec output (UK2Node::GetPassThroughPin) */
			bool PassesThrough() const { return Inputs == 1 && Outputs == 1; }
		};

		FExecPins CountExecPins(const FN2CNodeDefinition& Node)
		{
			FExecPins Pins;
			for (const FN2CPinDefinition& Pin : Node.InputPins)
			{
				Pins.Inputs += Pin.Type == EN2CPinType::Exec ? 1 : 0;
			}
			for (const FN2CPinDefinition& Pin : Node.OutputPins)
			{
				Pins.Outputs += Pin.Type == EN2CPinType::Exec ? 1 : 0;
			}
			return Pins;
		}

		/** Graph boundary nodes: never "unused", and without exec pins they are the outputs a pure graph computes */
		bool IsBoundary(const FN2CNodeDefinition& Node)
		{
			return Node.NodeType == EN2CNodeType::FunctionEntry || Node.NodeType == EN2CNodeType::FunctionResult
				|| Node.NodeType == EN2CNodeType::Tunnel || Node.NodeType == EN2CNodeType::TunnelBoundary;
		}

		/** The node ID of a pin ref ("N3.P7" -> "N3") */
		FString NodeOfPinRef(const FString& PinRef)
		{
			FString NodeID;
			return PinRef.Split(N2CJsonContract::PinRefSeparator, &NodeID, nullptr) ? NodeID : PinRef;
		}
	}

	void MarkGraph(FN2CGraph& Graph)
	{
		TMap<FString, int32> IndexOf;
		TArray<FExecPins> ExecPins;
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			FN2CNodeDefinition& Node = Graph.Nodes[Index];
			Node.bUnused = false;
			Node.bUnreachable = false;
			IndexOf.Add(Node.ID, Index);
			ExecPins.Add(CountExecPins(Node));
		}

		// Exec successors by node, from every entry form (pin form "N1.P2->N3.P4", node form "N1->N2->N3")
		TMap<int32, TArray<int32>> Successors;
		for (const FString& Flow : Graph.Flows.Execution)
		{
			TArray<N2CJsonContract::FExecEndpoint> Endpoints;
			if (!N2CJsonContract::ParseExecFlow(Flow, Endpoints))
			{
				continue;
			}
			for (int32 Index = 0; Index + 1 < Endpoints.Num(); ++Index)
			{
				const int32* From = IndexOf.Find(Endpoints[Index].Node);
				const int32* To = IndexOf.Find(Endpoints[Index + 1].Node);
				if (From && To)
				{
					Successors.FindOrAdd(*From).AddUnique(*To);
				}
			}
		}

		// Exec reachability from the entries; through a disabled node only when exec passes through it
		TBitArray<> Reachable(false, Graph.Nodes.Num());
		TBitArray<> Visited(false, Graph.Nodes.Num());
		TArray<int32> Stack;
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			if (!Graph.Nodes[Index].bDisabled && ExecPins[Index].IsEntry())
			{
				Stack.Push(Index);
			}
		}
		while (Stack.Num() > 0)
		{
			const int32 Index = Stack.Pop(EAllowShrinking::No);
			if (Visited[Index])
			{
				continue;
			}
			Visited[Index] = true;
			if (Graph.Nodes[Index].bDisabled)
			{
				if (!ExecPins[Index].PassesThrough())
				{
					continue;
				}
			}
			else
			{
				Reachable[Index] = true;
			}
			if (const TArray<int32>* Next = Successors.Find(Index))
			{
				Stack.Append(*Next);
			}
		}

		// The source node of each data input pin
		TMap<FString, int32> SourceOfInput;
		for (const TPair<FString, FN2CDataFlowTargets>& Flow : Graph.Flows.Data)
		{
			const int32* Source = IndexOf.Find(NodeOfPinRef(Flow.Key));
			if (!Source)
			{
				continue;
			}
			for (const FString& Target : Flow.Value.Targets)
			{
				SourceOfInput.Add(Target, *Source);
			}
		}

		// Live data: upstream from every reachable impure node and every pure graph output, through pure nodes
		TBitArray<> Live(false, Graph.Nodes.Num());
		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			const FN2CNodeDefinition& Node = Graph.Nodes[Index];
			const bool bSeed = !Node.bDisabled
				&& (Reachable[Index] || (!ExecPins[Index].IsImpure() && IsBoundary(Node) && Node.InputPins.Num() > 0));
			if (bSeed)
			{
				Live[Index] = true;
				Stack.Push(Index);
			}
		}
		while (Stack.Num() > 0)
		{
			const FN2CNodeDefinition& Node = Graph.Nodes[Stack.Pop(EAllowShrinking::No)];
			for (const FN2CPinDefinition& Pin : Node.InputPins)
			{
				const int32* Source = Pin.Type == EN2CPinType::Exec ? nullptr : SourceOfInput.Find(Node.ID + N2CJsonContract::PinRefSeparator + Pin.ID);
				if (!Source || Live[*Source] || Graph.Nodes[*Source].bDisabled || ExecPins[*Source].IsImpure())
				{
					continue;
				}
				Live[*Source] = true;
				Stack.Push(*Source);
			}
		}

		for (int32 Index = 0; Index < Graph.Nodes.Num(); ++Index)
		{
			FN2CNodeDefinition& Node = Graph.Nodes[Index];
			if (Node.bDisabled)
			{
				continue;
			}
			if (ExecPins[Index].IsImpure())
			{
				Node.bUnreachable = !ExecPins[Index].IsEntry() && !Reachable[Index];
			}
			else
			{
				Node.bUnused = !Live[Index] && !IsBoundary(Node);
			}
		}
	}
}
