// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Utils/N2CGraphUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Utils/N2CLogger.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/Notifications/SNotificationList.h"

UEdGraph* FN2CGraphUtils::FindGraphByGuid(const UBlueprint* Blueprint, const FGuid& GraphGuid)
{
	const TArray<UEdGraph*> Found = FN2CGraphTarget::FindGraphsByGuid(Blueprint, GraphGuid);
	return Found.Num() > 0 ? Found[0] : nullptr;
}

FString FN2CGraphUtils::DescribeGraph(const FString& GraphName, const FString& BlueprintPath)
{
	return FString::Printf(TEXT("'%s' in Blueprint '%s'"),
		GraphName.IsEmpty() ? TEXT("(unnamed graph)") : *GraphName,
		BlueprintPath.IsEmpty() ? TEXT("(no path)") : *BlueprintPath);
}

UEdGraph* FN2CGraphUtils::ResolveTaggedGraph(const FString& BlueprintPath, const FString& GraphGuid,
	const FString& GraphName, UBlueprint*& OutBlueprint, FString& OutMessage, EN2CGraphResolveError* OutError)
{
	OutBlueprint = nullptr;
	OutMessage.Empty();
	auto Fail = [&OutMessage, OutError](EN2CGraphResolveError Error, const FString& Message) -> UEdGraph*
	{
		OutMessage = Message;
		if (OutError)
		{
			*OutError = Error;
		}
		return nullptr;
	};
	if (OutError)
	{
		*OutError = EN2CGraphResolveError::None;
	}

	const FString Described = DescribeGraph(GraphName, BlueprintPath);
	if (!BlueprintPath.IsEmpty())
	{
		OutBlueprint = Cast<UBlueprint>(FSoftObjectPath(BlueprintPath).TryLoad());
	}
	if (!OutBlueprint)
	{
		return Fail(EN2CGraphResolveError::BlueprintNotFound, FString::Printf(
			TEXT("Cannot find graph %s: the Blueprint does not load. It may have been moved, renamed or deleted; remove the tag and tag the graph again."),
			*Described));
	}

	FGuid Guid;
	if (!FGuid::Parse(GraphGuid, Guid) || !Guid.IsValid())
	{
		return Fail(EN2CGraphResolveError::InvalidGuid, FString::Printf(
			TEXT("Cannot find graph %s: the tag's graph GUID '%s' is not valid. Remove the tag and tag the graph again."),
			*Described, *GraphGuid));
	}

	UEdGraph* Graph = FindGraphByGuid(OutBlueprint, Guid);
	if (!Graph)
	{
		return Fail(EN2CGraphResolveError::GraphNotFound, FString::Printf(
			TEXT("Cannot find graph %s (GUID %s): no graph of the Blueprint has that GUID. The graph may have been deleted; remove the tag."),
			*Described, *GraphGuid));
	}
	return Graph;
}

bool FN2CGraphUtils::CanTranslateGraph(const UEdGraph* Graph, FString* OutReason)
{
	auto No = [OutReason](const FString& Reason)
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};
	if (OutReason)
	{
		OutReason->Empty();
	}
	if (!Graph)
	{
		return No(TEXT("There is no graph."));
	}

	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return No(FString::Printf(TEXT("NodeToCode cannot translate graph '%s': it belongs to no Blueprint."), *Graph->GetName()));
	}

	const FN2CGraphTypeInfo Type = ClassifyGraphType(Graph);
	if (!Type.bSupported)
	{
		return No(DescribeUnsupportedGraph(*Graph, Type));
	}

	if (FindGraphByGuid(Blueprint, Graph->GraphGuid) == nullptr)
	{
		return No(FString::Printf(
			TEXT("NodeToCode cannot translate graph '%s': it is not one of the graphs of Blueprint '%s', so a tag could not find it again."),
			*Graph->GetName(), *Blueprint->GetPathName()));
	}
	return true;
}

namespace N2CGraphUtilsPrivate
{
	/**
	 * A schema class of an editor module NodeToCode does not link (AnimGraph, RigVMDeveloper), found by its script path;
	 * null when the module is not loaded, and then no graph can have that schema either
	 */
	const UClass* FindSchemaClass(const TCHAR* ClassPath)
	{
		return FindObject<UClass>(nullptr, ClassPath);
	}

	bool IsSchema(const UClass* SchemaClass, const TCHAR* ClassPath)
	{
		const UClass* Base = FindSchemaClass(ClassPath);
		return Base && SchemaClass->IsChildOf(Base);
	}
}

FN2CGraphTypeInfo FN2CGraphUtils::ClassifyGraphType(const UEdGraph* Graph)
{
	using namespace N2CGraphUtilsPrivate;
	auto Refused = [](const TCHAR* Kind, const FString& Description)
	{
		FN2CGraphTypeInfo Info;
		Info.Kind = Kind;
		Info.Description = Description;
		Info.bSupported = false;
		return Info;
	};
	auto Supported = [](const TCHAR* Kind, const TCHAR* Description)
	{
		FN2CGraphTypeInfo Info;
		Info.Kind = Kind;
		Info.Description = Description;
		return Info;
	};

	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	if (!Schema)
	{
		return Refused(TEXT("other"), TEXT("a graph with no schema"));
	}
	const UClass* SchemaClass = Schema->GetClass();

	// Order matters: the state and custom-blend schemas subclass the AnimGraph schema, and the AnimGraph and transition
	// schemas subclass the K2 schema
	if (IsSchema(SchemaClass, TEXT("/Script/RigVMDeveloper.RigVMEdGraphSchema")))
	{
		return Refused(TEXT("rig"), TEXT("a Control Rig (RigVM) graph"));
	}
	if (IsSchema(SchemaClass, TEXT("/Script/AnimGraph.AnimationStateMachineSchema")))
	{
		return Refused(TEXT("state-machine"), TEXT("an animation state machine"));
	}
	if (IsSchema(SchemaClass, TEXT("/Script/AnimGraph.AnimationStateGraphSchema")))
	{
		return Refused(TEXT("state"), TEXT("an animation state (an animation pose graph)"));
	}
	if (const UClass* AnimGraphSchema = FindSchemaClass(TEXT("/Script/AnimGraph.AnimationGraphSchema")))
	{
		if (SchemaClass == AnimGraphSchema)
		{
			return Refused(TEXT("animgraph"), TEXT("an AnimGraph (an animation pose graph)"));
		}
		if (SchemaClass->IsChildOf(AnimGraphSchema))
		{
			return Refused(TEXT("animgraph"), FString::Printf(TEXT("an animation pose graph (%s)"), *SchemaClass->GetName()));
		}
	}
	if (IsSchema(SchemaClass, TEXT("/Script/AnimGraph.AnimationTransitionSchema")))
	{
		return Supported(TEXT("transition"), TEXT("a transition rule"));
	}
	if (IsSchema(SchemaClass, TEXT("/Script/AnimGraph.AnimationConduitGraphSchema")))
	{
		return Supported(TEXT("conduit"), TEXT("a conduit rule"));
	}
	if (SchemaClass->IsChildOf(UEdGraphSchema_K2::StaticClass()))
	{
		return FN2CGraphTypeInfo();
	}
	// Named by the graph's class and its schema (card 0448): "a MaterialGraph (schema MaterialGraphSchema)"
	return Refused(TEXT("other"), FString::Printf(TEXT("a %s (schema %s), not a Blueprint graph"), *Graph->GetClass()->GetName(),
		*SchemaClass->GetName()));
}

FString FN2CGraphUtils::DescribeUnsupportedGraph(const UEdGraph& Graph, const FN2CGraphTypeInfo& Info)
{
	return FString::Printf(
		TEXT("NodeToCode does not translate graph '%s': it is %s. NodeToCode translates Blueprint logic graphs (event graphs, functions, macros, collapsed graphs, interface functions, transition rules); animation pose graphs, state machines and Control Rig graphs are not supported."),
		*Graph.GetName(), *Info.Description);
}

FString FN2CGraphUtils::UnsupportedGraphTypeError(const UEdGraph& Graph)
{
	const FN2CGraphTypeInfo Info = ClassifyGraphType(&Graph);
	return Info.bSupported ? FString() : TEXT("UNSUPPORTED_GRAPH_TYPE: ") + DescribeUnsupportedGraph(Graph, Info);
}

void FN2CGraphUtils::NotifyFailure(const FString& Message)
{
	FN2CLogger::Get().LogWarning(Message, TEXT("GraphUtils"));
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 8.0f;
	Info.bUseLargeFont = false;
	Info.bFireAndForget = true;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
}
