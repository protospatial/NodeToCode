// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpGetBlueprintN2CJsonTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

REGISTER_MCP_TOOL(FN2CMcpGetBlueprintN2CJsonTool)

namespace N2CMcpGetBlueprintN2CJsonToolPrivate
{
	void Describe(const TSharedPtr<FJsonObject>& Schema, const TCHAR* Name, const TCHAR* Description)
	{
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		const TSharedPtr<FJsonObject>* Property = nullptr;
		if (Schema->TryGetObjectField(TEXT("properties"), Properties) && (*Properties)->TryGetObjectField(Name, Property))
		{
			(*Property)->SetStringField(TEXT("description"), Description);
		}
	}
}

FMcpToolDefinition FN2CMcpGetBlueprintN2CJsonTool::GetDefinition() const
{
	using namespace N2CMcpGetBlueprintN2CJsonToolPrivate;

	FMcpToolDefinition Definition(
		TEXT("get-blueprint-n2cjson"),
		TEXT("Reads any Blueprint in the project as NodeToCode's N2C JSON by asset path, with no Blueprint editor open and ")
		TEXT("without changing focus: one graph (graphName or graphGuid, see list-blueprint-graphs) or, with neither, ")
		TEXT("every graph of the Blueprint in one document. The document is the one get-focused-blueprint returns (nodes ")
		TEXT("and pins with ids {short, guid}, flows, the guids table). When it would exceed about 20000 tokens, or with ")
		TEXT("toFile, it is written to a file under Saved/NodeToCode/Exports and the result is {path, bytes, ")
		TEXT("estimatedTokens, reason, graphCount, nodeCount}: read that file in parts."),
		TEXT("Blueprint Discovery")
	);

	TMap<FString, FString> Properties;
	Properties.Add(TEXT("blueprintPath"), TEXT("string"));
	Properties.Add(TEXT("graphName"), TEXT("string"));
	Properties.Add(TEXT("graphGuid"), TEXT("string"));
	Properties.Add(TEXT("depth"), TEXT("integer"));
	Properties.Add(TEXT("compact"), TEXT("boolean"));
	Properties.Add(TEXT("toFile"), TEXT("boolean"));
	const TSharedPtr<FJsonObject> Schema = BuildInputSchema(Properties, { TEXT("blueprintPath") });

	Describe(Schema, TEXT("blueprintPath"), TEXT("Required. Asset path of the Blueprint, e.g. /Game/Blueprints/BP_Door or /Game/Blueprints/BP_Door.BP_Door."));
	Describe(Schema, TEXT("graphName"), TEXT("Optional. Name of one graph (event graph, function, macro, collapsed, delegate or interface graph). With neither graphName nor graphGuid, every graph is read."));
	Describe(Schema, TEXT("graphGuid"), TEXT("Optional. The graph's GraphGuid from list-blueprint-graphs (a function's ID); stable across renames. Wins over graphName."));
	Describe(Schema, TEXT("depth"), TEXT("Optional, 0 to 5. Also capture the functions, macros and collapsed graphs the graph uses up to this many calls away (Max Translation Depth). Default: the project's Max Translation Depth setting."));
	Describe(Schema, TEXT("compact"), TEXT("Optional, default true. No whitespace; false indents the JSON as get-focused-blueprint does."));
	Describe(Schema, TEXT("toFile"), TEXT("Optional, default false. Always write the document to a file and return its path."));
	const TSharedPtr<FJsonObject>* SchemaProperties = nullptr;
	if (Schema->TryGetObjectField(TEXT("properties"), SchemaProperties))
	{
		const TSharedPtr<FJsonObject> Depth = (*SchemaProperties)->GetObjectField(TEXT("depth"));
		Depth->SetNumberField(TEXT("minimum"), 0);
		Depth->SetNumberField(TEXT("maximum"), 5);
		(*SchemaProperties)->GetObjectField(TEXT("compact"))->SetBoolField(TEXT("default"), true);
		(*SchemaProperties)->GetObjectField(TEXT("toFile"))->SetBoolField(TEXT("default"), false);
	}
	Definition.InputSchema = Schema;

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	// Results carry Blueprint text (node comments, titles, names and string literals): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpGetBlueprintN2CJsonTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([Arguments]() -> FMcpToolCallResult
	{
		FN2CBlueprintReadRequest Request;
		if (Arguments.IsValid())
		{
			const FN2CGraphTargetArgs Target = FN2CGraphTargetArgs::FromJson(Arguments);
			Request.BlueprintPath = Target.BlueprintPath;
			Request.GraphName = Target.GraphName;
			Request.GraphGuid = Target.GraphGuid;

			if (Arguments->HasField(TEXT("depth")))
			{
				double Depth = 0.0;
				if (!Arguments->TryGetNumberField(TEXT("depth"), Depth) || Depth != FMath::RoundToDouble(Depth) || Depth < 0.0 || Depth > 5.0)
				{
					return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: depth must be a whole number from 0 to 5"));
				}
				Request.Depth = static_cast<int32>(Depth);
			}
			bool bValue = false;
			if (Arguments->TryGetBoolField(TEXT("compact"), bValue))
			{
				Request.bCompact = bValue;
			}
			if (Arguments->TryGetBoolField(TEXT("toFile"), bValue))
			{
				Request.bToFile = bValue;
			}
		}

		const FN2CBlueprintReadResult Result = N2CMcpBlueprintExport::Read(Request);
		if (!Result.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Result.Error);
		}

		if (Result.WroteFile())
		{
			const TSharedRef<FJsonObject> FileJson = Result.FileResultJson();
			if (Result.Target.bLegacyId)
			{
				FileJson->SetObjectField(TEXT("target"), Result.Target.ToJson());
			}
			FString Text;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(FileJson, Writer);
			return FMcpToolCallResult::CreateTextResult(Text);
		}

		// The document as get-focused-blueprint returns it; a graph found by an old function ID also says so
		FMcpToolCallResult Inline = FMcpToolCallResult::CreateTextResult(Result.Json);
		return Result.Target.bLegacyId ? Result.Target.Finish(Inline) : Inline;
	}, 30.0f);
}
