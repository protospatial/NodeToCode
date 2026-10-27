// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CGraphStateTypes.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "N2CTagTypes.generated.h"

/**
 * The graph a tag event is about (card 0345): its Blueprint, its GUID (a Blueprint's copies share graph GUIDs, so the
 * Blueprint is part of the identity) and its name as the graph state records it.
 */
struct NODETOCODE_API FN2CGraphRef
{
	FSoftObjectPath Blueprint;
	FGuid GraphGuid;
	FString GraphName;

	FN2CGraphRef() = default;
	FN2CGraphRef(const FSoftObjectPath& InBlueprint, const FGuid& InGraphGuid, const FString& InGraphName)
		: Blueprint(InBlueprint), GraphGuid(InGraphGuid), GraphName(InGraphName)
	{
	}

	/** The graph state's key for this graph */
	FN2CGraphKey GetKey() const { return FN2CGraphKey(Blueprint, GraphGuid); }

	/** The tag as the legacy struct carries it: Entry on this graph */
	FN2CTaggedBlueprintGraph MakeTaggedGraph(const FN2CTagEntry& Entry) const;
};

/**
 * @struct FN2CTagInfo
 * @brief Simplified tag information structure for Blueprint use: one tag on one graph, its GUID and Blueprint as
 * strings. A projection of the graph state store (UN2CGraphStateManager), as UN2CTagBlueprintLibrary, the subsystem,
 * the Python bridge and the batch flow hand it out; it lives here so headers that pass it around need no Blueprint
 * library (card 0345).
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTagInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|Tags")
	FString Tag;

	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|Tags")
	FString Category = TEXT("Default");

	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|Tags")
	FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FString GraphGuid;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FString GraphName;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FString BlueprintPath;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Tags")
	FDateTime Timestamp;

	FN2CTagInfo() {}

	/** Create from FN2CTaggedBlueprintGraph */
	static FN2CTagInfo FromTaggedGraph(const FN2CTaggedBlueprintGraph& TaggedGraph);

	/** Convert to FN2CTaggedBlueprintGraph */
	FN2CTaggedBlueprintGraph ToTaggedGraph() const;
};
