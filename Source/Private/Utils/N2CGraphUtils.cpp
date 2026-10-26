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

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema || !Schema->IsA<UEdGraphSchema_K2>())
	{
		return No(FString::Printf(
			TEXT("NodeToCode cannot translate graph '%s': it is a %s graph, not a Blueprint graph. Only Blueprint graphs (event graphs, functions, macros, collapsed graphs, interface functions) can be tagged, copied as JSON or translated."),
			*Graph->GetName(), Schema ? *Schema->GetClass()->GetName() : TEXT("schema-less")));
	}

	if (FindGraphByGuid(Blueprint, Graph->GraphGuid) == nullptr)
	{
		return No(FString::Printf(
			TEXT("NodeToCode cannot translate graph '%s': it is not one of the graphs of Blueprint '%s', so a tag could not find it again."),
			*Graph->GetName(), *Blueprint->GetPathName()));
	}
	return true;
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
