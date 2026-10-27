// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpNodeDeletion.h"

#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CPinConnectPolicy.h"
#include "Utils/N2CLogger.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace N2CNodeDeletionPrivate
{
	/** A pin named by its node, name and direction: a neighbour's pins can be rebuilt when its links change */
	struct FPinRef
	{
		TWeakObjectPtr<UEdGraphNode> Node;
		FName PinName;
		EEdGraphPinDirection Direction = EGPD_Input;

		explicit FPinRef(const UEdGraphPin* Pin)
			: Node(Pin->GetOwningNode()), PinName(Pin->PinName), Direction(Pin->Direction)
		{
		}

		UEdGraphPin* Resolve() const
		{
			UEdGraphNode* Owner = Node.Get();
			return Owner ? Owner->FindPin(PinName, Direction) : nullptr;
		}
	};

	struct FPendingLink
	{
		FPinRef From;
		FPinRef To;
	};

	/** Every source -> target pair that runs through Node's pass-through pins */
	TArray<FPendingLink> RecordLinksThrough(const UEdGraphNode* Node)
	{
		TArray<FPendingLink> Links;
		for (const UEdGraphPin* InputPin : Node->Pins)
		{
			if (!InputPin || InputPin->Direction != EGPD_Input || InputPin->LinkedTo.Num() == 0)
			{
				continue;
			}
			const UEdGraphPin* OutputPin = Node->GetPassThroughPin(InputPin);
			if (!OutputPin || OutputPin->Direction != EGPD_Output)
			{
				continue;
			}
			for (const UEdGraphPin* Source : InputPin->LinkedTo)
			{
				for (const UEdGraphPin* Target : OutputPin->LinkedTo)
				{
					if (Source && Target && Source->GetOwningNode() != Node && Target->GetOwningNode() != Node)
					{
						Links.Add({FPinRef(Source), FPinRef(Target)});
					}
				}
			}
		}
		return Links;
	}

	/** Links each recorded pair through the schema; a pair that would replace or cut a link is left out */
	void Reconnect(const TArray<FPendingLink>& Links, const FString& DeletedTitle, TArray<N2CNodeDeletion::FBridgedLink>& OutBridged)
	{
		for (const FPendingLink& Link : Links)
		{
			UEdGraphPin* From = Link.From.Resolve();
			UEdGraphPin* To = Link.To.Resolve();
			if (!From || !To)
			{
				continue;
			}
			const UEdGraphSchema* Schema = From->GetSchema();
			if (!Schema)
			{
				continue;
			}
			const FPinConnectionResponse Response = Schema->CanCreateConnection(From, To);
			FString Refusal;
			if (!N2CPinConnectPolicy::Allow(Response, From, To, /*bBreakExistingLinks*/ false, Refusal))
			{
				FN2CLogger::Get().Log(FString::Printf(TEXT("Deleting %s: not bridging %s.%s -> %s.%s: %s"), *DeletedTitle,
					*From->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(), *From->PinName.ToString(),
					*To->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(), *To->PinName.ToString(), *Refusal),
					EN2CLogSeverity::Debug);
				continue;
			}
			// Read before the link: a conversion node or a wildcard update may rebuild the pins
			N2CNodeDeletion::FBridgedLink Bridged;
			Bridged.FromNodeGuid = From->GetOwningNode()->NodeGuid.ToString();
			Bridged.FromPinName = From->PinName.ToString();
			Bridged.ToNodeGuid = To->GetOwningNode()->NodeGuid.ToString();
			Bridged.ToPinName = To->PinName.ToString();
			Bridged.PinType = From->PinType.PinCategory.ToString();
			if (Schema->TryCreateConnection(From, To))
			{
				OutBridged.Add(MoveTemp(Bridged));
			}
		}
	}
}

bool N2CNodeDeletion::CanDelete(const UEdGraphNode* Node, bool bForce, FString& OutReason)
{
	if (!Node)
	{
		OutReason = TEXT("Invalid node");
		return false;
	}
	if (bForce || Node->CanUserDeleteNode())
	{
		return true;
	}
	OutReason = FString::Printf(TEXT("Node '%s' (%s) is protected: the editor does not let it be deleted. Use force=true to override."),
		*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *Node->GetClass()->GetName());
	return false;
}

UEdGraphNode* N2CNodeDeletion::FindNode(const UEdGraph* Graph, const FGuid& NodeGuid)
{
	if (!Graph)
	{
		return nullptr;
	}
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeGuid)
		{
			return Node;
		}
	}
	return nullptr;
}

TArray<N2CNodeDeletion::FDeletedNode> N2CNodeDeletion::DeleteNodes(UBlueprint* Blueprint, const TArray<UEdGraphNode*>& Nodes, bool bPreserveConnections)
{
	using namespace N2CNodeDeletionPrivate;

	TArray<FDeletedNode> Deleted;
	if (!Blueprint)
	{
		return Deleted;
	}
	Blueprint->Modify();

	// Weak: removing a collapsed-graph node removes its graph, and a node of that graph may be later in the list
	TArray<TWeakObjectPtr<UEdGraphNode>> Pending;
	for (UEdGraphNode* Node : Nodes)
	{
		Pending.Add(Node);
	}

	for (const TWeakObjectPtr<UEdGraphNode>& Weak : Pending)
	{
		UEdGraphNode* Node = Weak.Get();
		if (!Node || !Node->GetGraph() || !Node->GetGraph()->Nodes.Contains(Node))
		{
			continue;
		}

		FDeletedNode Info;
		Info.NodeGuid = Node->NodeGuid.ToString();
		Info.Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Info.ListTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		Info.NodeType = Node->GetClass()->GetName();
		Info.GraphName = Node->GetGraph()->GetName();

		const TArray<FPendingLink> Links = bPreserveConnections ? RecordLinksThrough(Node) : TArray<FPendingLink>();

		// Breakpoints, pin watches, the schema's BreakNodeLinks, then DestroyNode; compiled once below
		FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile*/ true);

		Reconnect(Links, Info.ListTitle, Info.BridgedLinks);

		FN2CLogger::Get().Log(FString::Printf(TEXT("Deleted node: %s (%d links bridged)"), *Info.Title, Info.BridgedLinks.Num()),
			EN2CLogSeverity::Debug);
		Deleted.Add(MoveTemp(Info));
	}

	if (Deleted.Num() > 0)
	{
		// Removing a node can change the Blueprint's structure (a timeline, a collapsed graph, a delegate signature):
		// one structural mark; the tool call compiles once, after its transaction closes (card 0151)
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
	}
	return Deleted;
}
