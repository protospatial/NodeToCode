// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CCapture.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Templates/ValueOrError.h"

class UBlueprint;
class UEdGraph;
class FJsonObject;

/** What get-blueprint-n2cjson (and the Python bridge's GetBlueprintN2CJson) reads */
struct FN2CBlueprintReadRequest
{
	/** Asset path of the Blueprint (required) */
	FString BlueprintPath;
	/** One graph by name or GraphGuid (FN2CGraphTarget); both empty: every graph of the Blueprint */
	FString GraphName;
	FString GraphGuid;
	/** Max Translation Depth for this capture; INDEX_NONE: Project Settings > Max Translation Depth */
	int32 Depth = INDEX_NONE;
	/** No whitespace (true) or indented, as get-focused-blueprint writes it (false) */
	bool bCompact = true;
	/** Always write the document to a file */
	bool bToFile = false;
	/** Write the document to a file when it would exceed MaxInlineTokens (the MCP tool; the Python bridge does not) */
	bool bSpillLargeToFile = true;
};

/** The outcome of N2CMcpBlueprintExport::Read */
struct FN2CBlueprintReadResult
{
	/** "<CODE>: <message>" on failure (ASSET_NOT_FOUND, GRAPH_NOT_FOUND, AMBIGUOUS_GRAPH_NAME, EMPTY_GRAPH, ...) */
	FString Error;
	UBlueprint* Blueprint = nullptr;
	/** The graph that was read; unset (no Blueprint) when every graph was read */
	FN2CGraphTarget Target;
	/** The document (also when it went to a file) */
	FString Json;
	int32 GraphCount = 0;
	int32 NodeCount = 0;
	int32 EstimatedTokens = 0;
	/** Set when the document was written to a file */
	FString FilePath;
	int64 FileBytes = 0;
	/** "toFile" (asked for) or "size" (over MaxInlineTokens); empty when returned inline */
	FString FileReason;

	bool IsValid() const { return Error.IsEmpty() && Blueprint != nullptr; }
	bool WroteFile() const { return !FilePath.IsEmpty(); }
	/** {path, bytes, estimatedTokens, maxInlineTokens, reason, graphCount, nodeCount} for a document written to a file */
	TSharedRef<FJsonObject> FileResultJson() const;
};

/**
 * Reads Blueprints as N2C JSON by asset path, with no Blueprint editor open (card 0157): get-blueprint-n2cjson,
 * list-blueprint-graphs, get-focused-blueprint (a thin wrapper over CaptureGraph and ToResultJson), find-nodes-in-graph
 * and the Python bridge share it. Every capture is its own (N2C::CaptureGraph, CaptureGraphs) and holds no singleton
 * state. Game thread only.
 */
namespace N2CMcpBlueprintExport
{
	/** A result larger than this (EstimateTokens) goes to a file: Claude Code cuts MCP tool output at 25k tokens */
	constexpr int32 MaxInlineTokens = 20000;

	/**
	 * Characters per token EstimateTokens assumes for N2C JSON. GUIDs, short IDs and punctuation tokenize densely:
	 * Anthropic's count_tokens gave 1.78 characters per token for BP_Sky_Sphere's compact document (115069 characters,
	 * 64525 tokens) and 2.06 for the indented one, so the plugin's usual 4 would let a 45k-token result through.
	 */
	constexpr double JsonCharsPerToken = 1.75;

	/** The capture options the agent-facing reads use: the editor's ghost default events kept, marked disabled (0137) */
	NODETOCODE_API FN2CCaptureOptions CaptureOptions(int32 Depth = INDEX_NONE);

	/** Captures one graph. EMPTY_GRAPH when it has no nodes, CAPTURE_FAILED when the result does not validate. */
	NODETOCODE_API TValueOrError<FN2CCapture, FString> CaptureGraph(UEdGraph& Graph, int32 Depth = INDEX_NONE);

	/**
	 * The graphs get-blueprint-n2cjson reads when no graph is named: UBlueprint::GetAllGraphs (functions with their
	 * collapsed graphs, macros, event graphs, delegate signatures, interface graphs) minus the bound graphs of Math
	 * Expression nodes, which the node itself describes
	 */
	NODETOCODE_API TArray<UEdGraph*> GraphsToCapture(const UBlueprint& Blueprint);

	/** Captures every graph of the Blueprint into one document. EMPTY_GRAPH when no graph has a node. */
	NODETOCODE_API TValueOrError<FN2CCapture, FString> CaptureAllGraphs(const UBlueprint& Blueprint, int32 Depth = INDEX_NONE);

	/**
	 * The document the agent tools return: the N2C JSON with each node's and pin's "id" replaced by "ids" {"short",
	 * "guid"} (pins also "name"), flows keeping the short IDs, and the capture's top-level "guids" table. Compact has no
	 * whitespace; otherwise indented (get-focused-blueprint). Empty when the capture cannot be serialized.
	 */
	NODETOCODE_API FString ToResultJson(const FN2CCapture& Capture, bool bCompact);

	/**
	 * Every graph of the Blueprint (GetAllGraphs order): {blueprintPath, blueprintName, graphCount, graphs: [{name,
	 * graphGuid, kind, nodeCount, readOnly, parentGraph (collapsed graphs)}]}
	 */
	NODETOCODE_API TSharedRef<FJsonObject> ListGraphs(const UBlueprint& Blueprint);

	/** ceil(characters / JsonCharsPerToken): on the high side for N2C JSON, so a result under the limit fits */
	NODETOCODE_API int32 EstimateTokens(const FString& Text);

	/** <Project>/Saved/NodeToCode/Exports, absolute (a test can point it elsewhere) */
	NODETOCODE_API FString ExportDirectory();

	/**
	 * The file a read is written to: <Blueprint>__<graph or AllGraphs>__<hash of path, graph GUID, depth and
	 * compact>.json, so reading the same thing again replaces the file instead of adding one
	 */
	NODETOCODE_API FString ExportFileName(const UBlueprint& Blueprint, const UEdGraph* Graph, int32 Depth, bool bCompact);

	/** Writes Json (UTF-8, no BOM) to ExportDirectory()/FileName through a temporary file; false and OutError on failure */
	NODETOCODE_API bool WriteExport(const FString& Json, const FString& FileName, FString& OutPath, int64& OutBytes, FString& OutError);

	/** The whole read: resolve, capture, serialize, and write a file when asked or when too large */
	NODETOCODE_API FN2CBlueprintReadResult Read(const FN2CBlueprintReadRequest& Request);

#if WITH_DEV_AUTOMATION_TESTS
	/** Points ExportDirectory() at a test's scratch folder; empty restores the default */
	NODETOCODE_API void SetExportDirectoryForTests(const FString& Directory);
#endif
}
