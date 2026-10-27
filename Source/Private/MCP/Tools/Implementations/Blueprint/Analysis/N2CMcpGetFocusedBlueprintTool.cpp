// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetFocusedBlueprintTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CCapture.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Utils/N2CLogger.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Dom/JsonObject.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetFocusedBlueprintTool)

FMcpToolDefinition FN2CMcpGetFocusedBlueprintTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-focused-blueprint"),
		TEXT("Collects and serializes the currently focused Blueprint graph in the Unreal Editor into NodeToCode's N2CJSON format. ")
		TEXT("The focused Blueprint is the Blueprint editor the user last activated (clicked its tab or inside it) or that ")
		TEXT("open-blueprint-asset last opened; the graph is the one that editor shows. To read a Blueprint without ")
		TEXT("opening or focusing it, or a graph too large for one result, use get-blueprint-n2cjson."),
		N2CMcpToolsets::BlueprintDiscovery
	);
	
	// This tool takes no input parameters
	Definition.InputSchema = BuildEmptyObjectSchema();
	
	// The N2C JSON document as structuredContent, and as indented text (card 0207)
	Definition.OutputSchema = N2CMcpOutputSchema::N2CJsonDocument();

	// Read-only: reads editor or project state and changes nothing
	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
	
	// Results carry Blueprint text (node comments, titles and string literals): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpGetFocusedBlueprintTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	// A thin wrapper (card 0157): the focused graph, read as get-blueprint-n2cjson reads a graph by path, indented
	return ExecuteOnGameThread([]() -> FMcpToolCallResult
	{
		UBlueprint* OwningBlueprint = nullptr;
		UEdGraph* FocusedGraph = nullptr;
		FString GraphError;
		if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("get-focused-blueprint tool failed: %s"), *GraphError));
			return FMcpToolCallResult::CreateErrorResult(GraphError);
		}

		TValueOrError<FN2CCapture, FString> Capture = N2CMcpBlueprintExport::CaptureGraph(*FocusedGraph);
		if (Capture.HasError())
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("get-focused-blueprint tool failed: %s"), *Capture.GetError()));
			return FMcpToolCallResult::CreateErrorResult(Capture.GetError());
		}
		const TSharedPtr<FJsonObject> Document = N2CMcpOutputSchema::ParseObject(BuildResultJson(Capture.GetValue()));
		if (!Document.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize N2CBlueprint to JSON."));
		}
		return FMcpToolCallResult::CreateJsonResult(Document.ToSharedRef(), /*bIndented*/ true);
	});
}

FString FN2CMcpGetFocusedBlueprintTool::BuildResultJson(const FN2CCapture& Capture)
{
	return N2CMcpBlueprintExport::ToResultJson(Capture, /*bCompact*/ false);
}
