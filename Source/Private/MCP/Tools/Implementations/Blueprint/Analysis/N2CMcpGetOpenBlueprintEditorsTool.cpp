// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetOpenBlueprintEditorsTool.h"
#include "N2CMcpEditorContext.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetOpenBlueprintEditorsTool)

FMcpToolDefinition FN2CMcpGetOpenBlueprintEditorsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-open-blueprint-editors"),
		TEXT("Returns every open Blueprint editor (Widget and Animation Blueprint editors included) with its asset path, ")
		TEXT("its graphs (event graphs, functions, macros, interface implementations and delegate signatures, each with ")
		TEXT("graphGuid and type), the graph it shows, whether it is the focused editor, unsaved changes (packageDirty) and ")
		TEXT("compile status. For the focused graph and the selected nodes in one call, use get-editor-context."),
		N2CMcpToolsets::BlueprintDiscovery
	);

	// No input parameters required for this tool
	Definition.InputSchema = BuildEmptyObjectSchema();

	{
		using namespace N2CMcpOutputSchema;
		const TArray<FString> GraphTypes = { TEXT("Event"), TEXT("Function"), TEXT("Macro"), TEXT("Interface"),
			TEXT("Delegate"), TEXT("Collapsed"), TEXT("Animation"), TEXT("StateMachine"), TEXT("Graph") };
		const TSharedRef<FJsonObject> GraphEntry = Object({
			{ TEXT("name"), String() },
			{ TEXT("graphGuid"), String(TEXT("Stable across renames; unique inside its Blueprint")) },
			{ TEXT("type"), Enum(GraphTypes) },
			{ TEXT("isEditable"), Boolean(TEXT("False for a graph the Blueprint editor shows read-only")) },
		}, { TEXT("name"), TEXT("graphGuid"), TEXT("type"), TEXT("isEditable") });
		Definition.OutputSchema = Object({
			{ TEXT("editors"), Array(Object({
				{ TEXT("blueprintName"), String() },
				{ TEXT("assetPath"), String() },
				{ TEXT("blueprintType"), String() },
				{ TEXT("blueprintClass"), String(TEXT("Blueprint, WidgetBlueprint, AnimBlueprint, ...")) },
				{ TEXT("editorName"), String(TEXT("BlueprintEditor, WidgetBlueprintEditor, AnimationBlueprintEditor, ...")) },
				{ TEXT("parentClass"), String() },
				{ TEXT("focusedGraph"), String(TEXT("The graph this editor shows")) },
				{ TEXT("focusedGraphGuid"), String() },
				{ TEXT("focusedGraphType"), Enum(GraphTypes) },
				{ TEXT("graphs"), Array(GraphEntry) },
				{ TEXT("isFocused"), Boolean(TEXT("The editor the focus-based tools act on")) },
				{ TEXT("packageDirty"), Boolean(TEXT("Unsaved changes")) },
				{ TEXT("compileStatus"), Enum({ TEXT("upToDate"), TEXT("upToDateWithWarnings"), TEXT("dirty"), TEXT("error"),
					TEXT("beingCreated"), TEXT("unknown") }, TEXT("dirty: changed since the last compile")) },
				{ TEXT("graph_counts"), Object({
					{ TEXT("event_graphs"), Integer() },
					{ TEXT("function_graphs"), Integer() },
					{ TEXT("macro_graphs"), Integer() },
					{ TEXT("interface_graphs"), Integer() },
					{ TEXT("delegate_graphs"), Integer() },
				}) },
			}, { TEXT("blueprintName"), TEXT("assetPath"), TEXT("blueprintType"), TEXT("blueprintClass"), TEXT("editorName"),
				TEXT("graphs"), TEXT("isFocused"), TEXT("packageDirty"), TEXT("compileStatus"), TEXT("graph_counts") })) },
			{ TEXT("count"), Integer() },
		}, { TEXT("editors"), TEXT("count") });
	}

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	return Definition;
}

FMcpToolCallResult FN2CMcpGetOpenBlueprintEditorsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([]() -> FMcpToolCallResult
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Editors;

		// Every open Blueprint editor whatever its toolkit, each with its own focused graph (card 0275: a check on the
		// editor name "BlueprintEditor" left out Widget and Animation Blueprint editors)
		const TSharedPtr<FBlueprintEditor> FocusedEditor = FN2CBlueprintEditorTracker::Get().GetFocusedEditor();
		for (const TSharedPtr<FBlueprintEditor>& Editor : FN2CBlueprintEditorTracker::GetOpenBlueprintEditors())
		{
			if (const TSharedPtr<FJsonObject> EditorInfo = CollectEditorInfo(Editor, Editor == FocusedEditor))
			{
				Editors.Add(MakeShared<FJsonValueObject>(EditorInfo));
			}
		}

		Result->SetArrayField(TEXT("editors"), Editors);
		Result->SetNumberField(TEXT("count"), Editors.Num());
		FN2CLogger::Get().Log(FString::Printf(TEXT("get-open-blueprint-editors: Found %d open Blueprint editors"), Editors.Num()), EN2CLogSeverity::Debug);
		return FMcpToolCallResult::CreateJsonResult(Result, /*bIndented*/ true);
	});
}

TSharedPtr<FJsonObject> FN2CMcpGetOpenBlueprintEditorsTool::CollectEditorInfo(const TSharedPtr<FBlueprintEditor>& Editor, bool bFocused)
{
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	if (!Blueprint)
	{
		return nullptr;
	}

	const TSharedRef<FJsonObject> EditorInfo = MakeShared<FJsonObject>();
	EditorInfo->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	EditorInfo->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
	EditorInfo->SetStringField(TEXT("blueprintType"), N2CMcpEditorContext::BlueprintTypeString(Blueprint->BlueprintType));
	EditorInfo->SetStringField(TEXT("blueprintClass"), Blueprint->GetClass()->GetName());
	EditorInfo->SetStringField(TEXT("editorName"), Editor->GetToolkitFName().ToString());
	if (Blueprint->ParentClass)
	{
		EditorInfo->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetName());
	}

	// The graph this editor shows, classified by its schema (card 0275: the EventGraph is Event, not Unknown)
	if (const UEdGraph* FocusedGraph = Editor->GetFocusedGraph())
	{
		EditorInfo->SetStringField(TEXT("focusedGraph"), FocusedGraph->GetName());
		EditorInfo->SetStringField(TEXT("focusedGraphGuid"), FocusedGraph->GraphGuid.ToString());
		EditorInfo->SetStringField(TEXT("focusedGraphType"), N2CMcpEditorContext::GraphType(Blueprint, FocusedGraph));
	}

	EditorInfo->SetArrayField(TEXT("graphs"), CollectBlueprintGraphs(*Blueprint));
	EditorInfo->SetBoolField(TEXT("isFocused"), bFocused);
	// Unsaved changes, and apart from them the compile status (before card 0275 isDirty was the compile status)
	EditorInfo->SetBoolField(TEXT("packageDirty"), N2CMcpEditorContext::IsPackageDirty(*Blueprint));
	EditorInfo->SetStringField(TEXT("compileStatus"), N2CMcpEditorContext::CompileStatus(Blueprint->Status));

	int32 InterfaceGraphs = 0;
	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		InterfaceGraphs += Interface.Graphs.Num();
	}
	const TSharedRef<FJsonObject> GraphCounts = MakeShared<FJsonObject>();
	GraphCounts->SetNumberField(TEXT("event_graphs"), Blueprint->UbergraphPages.Num());
	GraphCounts->SetNumberField(TEXT("function_graphs"), Blueprint->FunctionGraphs.Num());
	GraphCounts->SetNumberField(TEXT("macro_graphs"), Blueprint->MacroGraphs.Num());
	GraphCounts->SetNumberField(TEXT("interface_graphs"), InterfaceGraphs);
	GraphCounts->SetNumberField(TEXT("delegate_graphs"), Blueprint->DelegateSignatureGraphs.Num());
	EditorInfo->SetObjectField(TEXT("graph_counts"), GraphCounts);

	return EditorInfo;
}

TArray<TSharedPtr<FJsonValue>> FN2CMcpGetOpenBlueprintEditorsTool::CollectBlueprintGraphs(const UBlueprint& Blueprint)
{
	TArray<TSharedPtr<FJsonValue>> GraphList;
	for (UEdGraph* Graph : N2CMcpEditorContext::TopLevelGraphs(Blueprint))
	{
		const TSharedRef<FJsonObject> GraphInfo = N2CMcpEditorContext::GraphJson(&Blueprint, Graph);
		GraphInfo->SetBoolField(TEXT("isEditable"), Graph->bEditable && !FBlueprintEditorUtils::IsGraphReadOnly(Graph));
		GraphList.Add(MakeShared<FJsonValueObject>(GraphInfo));
	}
	return GraphList;
}
