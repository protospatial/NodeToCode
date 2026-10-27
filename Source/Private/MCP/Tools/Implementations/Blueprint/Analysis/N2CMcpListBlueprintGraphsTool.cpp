// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpListBlueprintGraphsTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

REGISTER_MCP_TOOL(FN2CMcpListBlueprintGraphsTool)

FMcpToolDefinition FN2CMcpListBlueprintGraphsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("list-blueprint-graphs"),
		TEXT("Lists every graph of any Blueprint in the project by asset path, with no Blueprint editor open: name, ")
		TEXT("graphGuid (stable across renames; pass it to get-blueprint-n2cjson or the graph tools), kind (event, ")
		TEXT("function, macro, collapsed, delegate, interface), nodeCount, readOnly (shown read-only in the Blueprint ")
		TEXT("editor) and, for a collapsed graph, parentGraph."),
		N2CMcpToolsets::BlueprintDiscovery
	);

	TMap<FString, FString> Properties;
	Properties.Add(TEXT("blueprintPath"), TEXT("string"));
	Definition.InputSchema = BuildInputSchema(Properties, { TEXT("blueprintPath") });
	Definition.InputSchema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("blueprintPath"))->SetStringField(
		TEXT("description"), TEXT("Required. Asset path of the Blueprint, e.g. /Game/Blueprints/BP_Door or /Game/Blueprints/BP_Door.BP_Door."));

	{
		using namespace N2CMcpOutputSchema;
		Definition.OutputSchema = UntrustedObject({
			{ TEXT("blueprintPath"), String(TEXT("Object path of the Blueprint")) },
			{ TEXT("blueprintName"), String() },
			{ TEXT("graphCount"), Integer() },
			{ TEXT("graphs"), Array(Object({
				{ TEXT("name"), String() },
				{ TEXT("graphGuid"), String(TEXT("Stable across renames")) },
				{ TEXT("kind"), String(TEXT("event, function, macro, collapsed, delegate or interface")) },
				{ TEXT("nodeCount"), Integer() },
				{ TEXT("readOnly"), Boolean(TEXT("Shown read-only in the Blueprint editor")) },
				{ TEXT("parentGraph"), String(TEXT("For a collapsed graph: the graph that holds it")) },
			}, { TEXT("name"), TEXT("graphGuid"), TEXT("kind"), TEXT("nodeCount"), TEXT("readOnly") })) },
		}, { TEXT("blueprintPath"), TEXT("blueprintName"), TEXT("graphCount"), TEXT("graphs") });
	}

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	// Results carry names from the project's Blueprints (graph, function and macro names): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpListBlueprintGraphsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([Arguments]() -> FMcpToolCallResult
	{
		const FString BlueprintPath = FN2CGraphTargetArgs::FromJson(Arguments).BlueprintPath;
		if (BlueprintPath.IsEmpty())
		{
			return FMcpToolCallResult::CreateErrorResult(
				TEXT("INVALID_ARGUMENT: blueprintPath is required, the asset path of a Blueprint such as /Game/Blueprints/BP_Door"));
		}
		FString LoadError;
		UBlueprint* Blueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath, LoadError);
		if (!Blueprint)
		{
			return FMcpToolCallResult::CreateErrorResult(LoadError);
		}

		return FMcpToolCallResult::CreateJsonResult(N2CMcpBlueprintExport::ListGraphs(*Blueprint));
	}, 30.0f);
}
