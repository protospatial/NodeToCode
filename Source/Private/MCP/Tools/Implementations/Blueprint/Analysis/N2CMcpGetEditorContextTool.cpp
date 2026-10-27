// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpGetEditorContextTool.h"
#include "N2CMcpEditorContext.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Dom/JsonObject.h"

REGISTER_MCP_TOOL(FN2CMcpGetEditorContextTool)

FMcpToolDefinition FN2CMcpGetEditorContextTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-editor-context"),
		TEXT("Returns what the user is looking at in the Unreal Editor, in one call: the focused Blueprint (the Blueprint ")
		TEXT("editor the user last activated) and its focused graph (name, graphGuid, type), the GUIDs of the nodes ")
		TEXT("selected in that graph, every open Blueprint editor (Widget and Animation Blueprints included) with the graph ")
		TEXT("it shows, unsaved changes and compile status, whether Play In Editor is running, and the engine version. Reads ")
		TEXT("only; opens and focuses nothing. Pass the graphGuid to get-blueprint-n2cjson or the graph tools."),
		N2CMcpToolsets::BlueprintDiscovery
	);

	Definition.InputSchema = BuildEmptyObjectSchema();

	{
		using namespace N2CMcpOutputSchema;
		const TArray<FString> GraphTypes = { TEXT("Event"), TEXT("Function"), TEXT("Macro"), TEXT("Interface"),
			TEXT("Delegate"), TEXT("Collapsed"), TEXT("Animation"), TEXT("StateMachine"), TEXT("Graph") };
		auto GraphSchema = [&GraphTypes](const FString& Description)
		{
			return Object({
				{ TEXT("name"), String() },
				{ TEXT("graphGuid"), String(TEXT("Stable across renames; unique inside its Blueprint")) },
				{ TEXT("type"), Enum(GraphTypes) },
			}, { TEXT("name"), TEXT("graphGuid"), TEXT("type") }, Description);
		};
		Definition.OutputSchema = UntrustedObject({
			{ TEXT("focusedBlueprint"), Object({
				{ TEXT("name"), String() },
				{ TEXT("path"), String(TEXT("Object path of the Blueprint")) },
				{ TEXT("blueprintClass"), String(TEXT("Blueprint, WidgetBlueprint, AnimBlueprint, ...")) },
			}, { TEXT("name"), TEXT("path"), TEXT("blueprintClass") },
				TEXT("The Blueprint editor the user last activated; absent when no Blueprint editor is open")) },
			{ TEXT("focusedGraph"), GraphSchema(TEXT("The graph the focused Blueprint editor shows")) },
			{ TEXT("selectedNodes"), Array(Object({
				{ TEXT("nodeGuid"), String(TEXT("As ids.guid in N2C JSON")) },
				{ TEXT("title"), String() },
				{ TEXT("nodeClass"), String() },
			}, { TEXT("nodeGuid"), TEXT("title"), TEXT("nodeClass") }), TEXT("Selected in the focused graph, in graph order")) },
			{ TEXT("selectedNodeCount"), Integer() },
			{ TEXT("openEditors"), Array(Object({
				{ TEXT("blueprintName"), String() },
				{ TEXT("assetPath"), String() },
				{ TEXT("blueprintClass"), String() },
				{ TEXT("editorName"), String(TEXT("The asset editor: BlueprintEditor, WidgetBlueprintEditor, AnimationBlueprintEditor, ...")) },
				{ TEXT("focusedGraph"), GraphSchema(TEXT("The graph this editor shows")) },
				{ TEXT("isFocused"), Boolean() },
				{ TEXT("packageDirty"), Boolean(TEXT("Unsaved changes")) },
				{ TEXT("compileStatus"), Enum({ TEXT("upToDate"), TEXT("upToDateWithWarnings"), TEXT("dirty"), TEXT("error"),
					TEXT("beingCreated"), TEXT("unknown") }) },
			}, { TEXT("blueprintName"), TEXT("assetPath"), TEXT("blueprintClass"), TEXT("isFocused"), TEXT("packageDirty"), TEXT("compileStatus") })) },
			{ TEXT("openEditorCount"), Integer() },
			{ TEXT("playInEditor"), Object({
				{ TEXT("active"), Boolean() },
				{ TEXT("simulating"), Boolean() },
				{ TEXT("paused"), Boolean() },
			}, { TEXT("active"), TEXT("simulating"), TEXT("paused") }) },
			{ TEXT("engineVersion"), String(TEXT("major.minor.patch")) },
		}, { TEXT("selectedNodes"), TEXT("selectedNodeCount"), TEXT("openEditors"), TEXT("openEditorCount"),
			TEXT("playInEditor"), TEXT("engineVersion") });
	}

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	// Results carry Blueprint, graph and node names (a comment node's title is its text): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpGetEditorContextTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([]() -> FMcpToolCallResult
	{
		return FMcpToolCallResult::CreateJsonResult(N2CMcpEditorContext::Build());
	});
}
