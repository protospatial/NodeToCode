// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpGetBlueprintDependenciesTool.h"
#include "N2CMcpDependencies.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

REGISTER_MCP_TOOL(FN2CMcpGetBlueprintDependenciesTool)

FMcpToolDefinition FN2CMcpGetBlueprintDependenciesTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-blueprint-dependencies"),
		TEXT("Builds the dependency index of the project's Blueprints, Blueprint interfaces, macro libraries, user structs ")
		TEXT("and user enums under asset paths or content folders, by path, with no editor open: nodes (path, name, kind, ")
		TEXT("root), typed edges (from depends on to: hard-package, parent, interface, calls, macro, cast, variable-type; ")
		TEXT("node-level edges name their graph and node), order (every node once, dependencies first: the order to convert ")
		TEXT("or translate them in) and cycleGroups (assets that depend on each other and must be handled together). Use it ")
		TEXT("before planning a conversion of many assets. With toFile, or when the result would exceed about 20000 tokens, ")
		TEXT("the full index is written as JSON under Saved/NodeToCode/Dependencies and the result names it in savedTo (over ")
		TEXT("the limit the lists are left out: inline false). Engine content is never indexed."),
		N2CMcpToolsets::BlueprintDiscovery
	);

	const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("type"), TEXT("string"));
		const TSharedRef<FJsonObject> Paths = MakeShared<FJsonObject>();
		Paths->SetStringField(TEXT("type"), TEXT("array"));
		Paths->SetObjectField(TEXT("items"), Item);
		Paths->SetNumberField(TEXT("minItems"), 1);
		Paths->SetNumberField(TEXT("maxItems"), MaxPaths);
		Paths->SetStringField(TEXT("description"), FString::Printf(
			TEXT("Required, 1 to %d entries. Asset paths (/Game/Blueprints/BP_Door or /Game/Blueprints/BP_Door.BP_Door) and content folders (/Game/Blueprints, with its subfolders): the roots of the index."),
			MaxPaths));
		Properties->SetObjectField(TEXT("paths"), Paths);
	}
	auto Flag = [&Properties](const TCHAR* Name, bool bDefault, const TCHAR* Description)
	{
		const TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), TEXT("boolean"));
		Property->SetBoolField(TEXT("default"), bDefault);
		Property->SetStringField(TEXT("description"), Description);
		Properties->SetObjectField(Name, Property);
	};
	Flag(TEXT("includeCallGraph"), true, TEXT("Optional, default true. Load the Blueprints to add the edges only their graphs and variables hold (calls, macro, cast, variable-type). false reads the AssetRegistry alone and loads nothing (hard-package, parent and interface edges only)."));
	Flag(TEXT("includeReferenced"), true, TEXT("Optional, default true. Also index the project's own assets the roots depend on outside the given paths (root false), so the order puts them first."));
	Flag(TEXT("toFile"), false, TEXT("Optional, default false. Also write the full index as JSON under Saved/NodeToCode/Dependencies and return its path in savedTo."));

	const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("paths")));
	Schema->SetArrayField(TEXT("required"), Required);
	Definition.InputSchema = Schema;

	{
		using namespace N2CMcpOutputSchema;
		Definition.OutputSchema = UntrustedObject({
			{ TEXT("roots"), Array(String(), TEXT("The paths as given")) },
			{ TEXT("nodeCount"), Integer() },
			{ TEXT("edgeCount"), Integer() },
			{ TEXT("nodes"), Array(Object({
				{ TEXT("path"), String(TEXT("Object path, e.g. /Game/Blueprints/BP_Door.BP_Door")) },
				{ TEXT("name"), String() },
				{ TEXT("kind"), Enum({ TEXT("enum"), TEXT("struct"), TEXT("interface"), TEXT("macro-library"), TEXT("blueprint") }) },
				{ TEXT("root"), Boolean(TEXT("Under the given paths (false: pulled in because a root depends on it)")) },
			}, { TEXT("path"), TEXT("name"), TEXT("kind"), TEXT("root") })) },
			{ TEXT("edges"), Array(Object({
				{ TEXT("from"), String(TEXT("Object path of the asset that depends")) },
				{ TEXT("to"), String(TEXT("Object path of the asset it depends on (comes first in order)")) },
				{ TEXT("kind"), Enum({ TEXT("hard-package"), TEXT("parent"), TEXT("interface"), TEXT("calls"), TEXT("macro"), TEXT("cast"), TEXT("variable-type") }) },
				{ TEXT("graph"), String(TEXT("For calls, macro, cast and a local variable's variable-type: the graph of from that holds it")) },
				{ TEXT("node"), String(TEXT("For calls, macro and cast: the node's GUID; for variable-type: the variable's name")) },
			}, { TEXT("from"), TEXT("to"), TEXT("kind") })) },
			{ TEXT("order"), Array(String(), TEXT("Every node's path once, dependencies first")) },
			{ TEXT("cycleGroups"), Array(Array(String()), TEXT("Groups of paths that depend on each other, each sorted by path")) },
			{ TEXT("warnings"), Array(String(), TEXT("Paths that named nothing, assets that did not load")) },
			{ TEXT("inline"), Boolean(TEXT("false when nodes, edges, order and cycleGroups were left out for size: read savedTo")) },
			{ TEXT("savedTo"), String(TEXT("The JSON export (toFile, or over the inline limit)")) },
			{ TEXT("bytes"), Integer() },
			{ TEXT("cycleGroupCount"), Integer(TEXT("When inline is false")) },
			{ TEXT("reason"), String(TEXT("When inline is false: why the lists are in the file")) },
		}, { TEXT("roots"), TEXT("nodeCount"), TEXT("edgeCount"), TEXT("warnings"), TEXT("inline") });
	}

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	// Results carry names from the project's Blueprints (asset, graph and variable names): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpGetBlueprintDependenciesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	N2CMcpDependencies::FRequest Request;
	Request.MaxInlineTokens = N2CMcpBlueprintExport::MaxInlineTokens;
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("paths"), Paths) || Paths->Num() == 0)
	{
		return FMcpToolCallResult::CreateErrorResult(
			TEXT("INVALID_ARGUMENT: paths is required, an array of asset paths or content folders such as [\"/Game/Blueprints\"]"));
	}
	if (Paths->Num() > MaxPaths)
	{
		return FMcpToolCallResult::CreateErrorResult(FString::Printf(
			TEXT("INVALID_ARGUMENT: paths has %d entries, at most %d: pass a content folder instead of its assets"), Paths->Num(), MaxPaths));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Paths)
	{
		FString Path;
		if (!Value.IsValid() || Value->Type != EJson::String || !Value->TryGetString(Path))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: every entry of paths must be a string"));
		}
		Request.Paths.Add(Path);
	}
	for (const TPair<const TCHAR*, bool*>& Flag : TArray<TPair<const TCHAR*, bool*>>({
		{ TEXT("includeCallGraph"), &Request.bIncludeCallGraph },
		{ TEXT("includeReferenced"), &Request.bIncludeReferenced },
		{ TEXT("toFile"), &Request.bToFile } }))
	{
		// A JSON boolean only: TryGetBoolField would also read the string "yes" or "true"
		const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Flag.Key);
		if (Value.IsValid() && Value->Type == EJson::Boolean)
		{
			*Flag.Value = Value->AsBool();
		}
		else if (Value.IsValid() && Value->Type != EJson::Null)
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("INVALID_ARGUMENT: %s must be true or false"), Flag.Key));
		}
	}

	return ExecuteOnGameThread([Request]() -> FMcpToolCallResult
	{
		FString Error;
		const TSharedPtr<FJsonObject> Json = N2CMcpDependencies::Run(Request, Error);
		if (!Json.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}
		return FMcpToolCallResult::CreateJsonResult(Json.ToSharedRef());
	}, TimeoutSeconds);
}
