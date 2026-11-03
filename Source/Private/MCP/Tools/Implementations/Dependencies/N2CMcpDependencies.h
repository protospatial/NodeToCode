// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FN2CDependencyIndex;

/**
 * The dependency index (FN2CDependencyIndex, card 0318) as JSON for agents (card 0319): what the
 * get-blueprint-dependencies tool returns and the Python bridge's GetBlueprintDependencies, plus the JSON export under
 * Saved/NodeToCode/Dependencies. Nodes, edges and the order name assets by object path, so the JSON stands on its own.
 */
namespace N2CMcpDependencies
{
	/** One call: what to index and how */
	struct FRequest
	{
		/** Asset paths (/Game/BP_A or /Game/BP_A.BP_A) and content folders (/Game/Folder, with its subfolders) */
		TArray<FString> Paths;
		/** Load the Blueprints for the edges only their graphs hold (calls, macros, casts, variable types) */
		bool bIncludeCallGraph = true;
		/** Also index the project's own assets the roots depend on, outside the roots */
		bool bIncludeReferenced = true;
		/** Always write the export file */
		bool bToFile = false;
		/**
		 * A result whose JSON is over this many estimated tokens is written to the export file and returned without
		 * nodes, edges, order and cycleGroups (inline false). MAX_int32: always inline.
		 */
		int32 MaxInlineTokens = MAX_int32;
	};

	/**
	 * The index as {roots, nodeCount, edgeCount, nodes [{path, name, kind, root}], edges [{from, to, kind, graph?,
	 * node?}], order [path], cycleGroups [[path]], warnings}. Kinds are FN2CDependencyIndex::NodeKindToString and
	 * EdgeKindToString; from and to are object paths, from depending on to.
	 */
	NODETOCODE_API TSharedRef<FJsonObject> ToJson(const FN2CDependencyIndex& Index, const TArray<FString>& Roots);

	/**
	 * Builds the index (game thread) and its result: ToJson plus inline (bool) and, when the export was written,
	 * savedTo and bytes. Null with OutError ("INVALID_ARGUMENT: ...", "NOT_FOUND: ...", "EXPORT_FAILED: ...") on failure.
	 */
	NODETOCODE_API TSharedPtr<FJsonObject> Run(const FRequest& Request, FString& OutError);

	/** Saved/NodeToCode/Dependencies, absolute (a test may point it elsewhere) */
	NODETOCODE_API FString ExportDirectory();

	/** The export's file name for these roots and options: <first root's name>__<crc of roots and options>.json */
	NODETOCODE_API FString ExportFileName(const FRequest& Request);

#if WITH_DEV_AUTOMATION_TESTS
	/** Points ExportDirectory at Directory (empty: back to Saved/NodeToCode/Dependencies) */
	NODETOCODE_API void SetExportDirectoryForTests(const FString& Directory);
#endif
}
