// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declarations
class UEdGraph;
class UBlueprint;

/**
 * Function IDs of the MCP function tools (card 0152). A function's ID is its graph's UEdGraph::GraphGuid, which the
 * engine saves with the graph and keeps when the function is renamed or the Blueprint is renamed or moved. It is not
 * unique across Blueprints (a duplicated Blueprint keeps its graphs' GUIDs), so a function is always looked up inside
 * one Blueprint.
 *
 * Before card 0152 the ID was the MD5 of "<Blueprint path name>::<function name>", which changed on every rename or
 * move. Those old IDs are still accepted, with a warning, until NodeToCode LegacyIdRemovalVersion().
 */
class NODETOCODE_API FN2CMcpFunctionGuidUtils
{
public:
	/** The function's stable ID: its GraphGuid (the old hash only for a graph saved without a valid GraphGuid) */
	static FGuid GetFunctionId(const UEdGraph* FunctionGraph);

	/** The deprecated ID the tools returned before card 0152, MD5("<Blueprint path name>::<graph name>") */
	static FGuid GetLegacyFunctionId(const UEdGraph* FunctionGraph);

	/** MD5("<Blueprint path name>::<FunctionName>") as a GUID (the deprecated ID scheme) */
	static FGuid GenerateDeterministicGuid(const UBlueprint* Blueprint, const FString& FunctionName);

	/**
	 * The function graph of this Blueprint with this ID: its GraphGuid, else (deprecated) its old hash ID, which logs
	 * nothing here but sets bOutLegacy so the caller can warn. Only Blueprint->FunctionGraphs are searched.
	 */
	static UEdGraph* FindFunctionByGuid(UBlueprint* Blueprint, const FGuid& FunctionGuid, bool* bOutLegacy = nullptr);

	/** True when Graph is one of Blueprint's function graphs */
	static bool IsFunctionGraph(const UBlueprint* Blueprint, const UEdGraph* Graph);

	/** The release that stops accepting the old hash IDs */
	static const TCHAR* LegacyIdRemovalVersion() { return TEXT("2.1"); }
};
