// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;
class FJsonValue;

/** One graph a batch tool works on */
struct FN2CBatchGraph
{
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	/** The Blueprint's object path (/Game/Dir/BP_Door.BP_Door) */
	FString BlueprintPath;
	FString GraphName;
	/** UEdGraph::GraphGuid, as list-blueprint-graphs gives it */
	FString GraphGuid;
	/** FN2CGraphTarget::GraphKind: EventGraph, Function, Macro, Collapsed, ... */
	FString Kind;
	/** The tag and category that selected the graph (a tag filter); empty for explicit targets */
	FString Tag;
	FString Category;

	/** {blueprintPath, graphName, graphGuid, kind} */
	TSharedRef<FJsonObject> ToJson() const;
};

/** A target that names nothing a batch can work on, or a graph it left out */
struct FN2CBatchTargetIssue
{
	/** As the target named it (or as resolved) */
	FString BlueprintPath;
	FString GraphName;
	FString GraphGuid;
	/** "<CODE>: <message>" */
	FString Error;

	/** {blueprintPath, graphName?, graphGuid?, code, error} */
	TSharedRef<FJsonObject> ToJson() const;
};

/** What N2CMcpBatchTargets::Resolve found */
struct FN2CBatchTargets
{
	/** The graphs to work on, in the order the targets named them, each once */
	TArray<FN2CBatchGraph> Graphs;
	/** Targets that resolved to nothing: ASSET_NOT_FOUND, UNSUPPORTED_ASSET_TYPE, GRAPH_NOT_FOUND, ... */
	TArray<FN2CBatchTargetIssue> Failures;
	/** Graphs of a whole-Blueprint target that have no nodes (EMPTY_GRAPH), left out */
	TArray<FN2CBatchTargetIssue> Skipped;
	/** "<CODE>: <message>" when the request itself is wrong (INVALID_ARGUMENT, TOO_MANY_GRAPHS): nothing was resolved */
	FString Error;

	bool IsValid() const { return Error.IsEmpty(); }

	/** Failures and Skipped as JSON arrays */
	TArray<TSharedPtr<FJsonValue>> FailuresJson() const;
	TArray<TSharedPtr<FJsonValue>> SkippedJson() const;
};

/**
 * The graph selection export-n2cjson-batch and translate-batch share (card 0169). Arguments:
 * - targets: [{blueprintPath, graphName?, graphGuid?}]. A target with a graph names that graph (FN2CGraphTarget, so
 *   every graph kind resolves); a target with only blueprintPath names every graph of the Blueprint that has nodes
 *   (N2CMcpBlueprintExport::GraphsToCapture), empty ones going to Skipped.
 * - or tag and/or category: the graphs tagged so (tag-blueprint-graph, list-blueprint-tags).
 * Exactly one of the two selections. A path that names an asset which is not a Blueprint (a Material, a Data Asset)
 * fails that target with UNSUPPORTED_ASSET_TYPE: the batch tools read Blueprints only until materials get their own
 * capture (epic 0442, card 0469). Game thread.
 */
namespace N2CMcpBatchTargets
{
	/**
	 * Resolves the selection. MaxGraphs caps the graphs one call may name (TOO_MANY_GRAPHS beyond it); ToolName names the
	 * calling tool in error messages.
	 */
	FN2CBatchTargets Resolve(const TSharedPtr<FJsonObject>& Arguments, int32 MaxGraphs, const TCHAR* ToolName);

	/**
	 * Loads the Blueprint at Path. A path that names a loadable asset of another class fails with
	 * "UNSUPPORTED_ASSET_TYPE: '<path>' is a <Class>, not a Blueprint..." instead of ASSET_NOT_FOUND.
	 */
	UBlueprint* LoadBlueprint(const FString& Path, FString& OutError);

	/** The schema of the shared selection arguments (targets, tag, category), added to Properties */
	void AddSchema(const TSharedPtr<FJsonObject>& Properties, int32 MaxGraphs);

	/** The code of "<CODE>: <message>" (the whole text when it has no code) */
	FString ErrorCode(const FString& Error);
}
