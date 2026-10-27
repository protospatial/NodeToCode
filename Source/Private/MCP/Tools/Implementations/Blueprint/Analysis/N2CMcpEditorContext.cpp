// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpEditorContext.h"

#include "Core/N2CBlueprintEditorTracker.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "Engine/World.h"
#include "K2Node_Composite.h"
#include "Misc/EngineVersion.h"
#include "UObject/Package.h"

FString N2CMcpEditorContext::GraphType(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("Graph");
	}
	if (Blueprint)
	{
		if (Blueprint->DelegateSignatureGraphs.Contains(Graph))
		{
			return TEXT("Delegate");
		}
		for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
		{
			if (Interface.Graphs.Contains(Graph))
			{
				return TEXT("Interface");
			}
		}
	}
	// A collapsed graph's page: the K2 schema reports the type of the graph that holds it
	if (Cast<UK2Node_Composite>(Graph->GetOuter()))
	{
		return TEXT("Collapsed");
	}
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		return TEXT("Graph");
	}
	switch (Schema->GetGraphType(Graph))
	{
	case GT_Ubergraph: return TEXT("Event");
	case GT_Function: return TEXT("Function");
	case GT_Macro: return TEXT("Macro");
	case GT_Animation: return TEXT("Animation");
	case GT_StateMachine: return TEXT("StateMachine");
	default: return TEXT("Graph");
	}
}

TArray<UEdGraph*> N2CMcpEditorContext::TopLevelGraphs(const UBlueprint& Blueprint)
{
	TArray<UEdGraph*> Graphs;
	auto AddAll = [&Graphs](const TArray<TObjectPtr<UEdGraph>>& From)
	{
		for (UEdGraph* Graph : From)
		{
			if (Graph)
			{
				Graphs.AddUnique(Graph);
			}
		}
	};
	AddAll(Blueprint.UbergraphPages);
	AddAll(Blueprint.FunctionGraphs);
	AddAll(Blueprint.MacroGraphs);
	for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
	{
		AddAll(Interface.Graphs);
	}
	AddAll(Blueprint.DelegateSignatureGraphs);
	return Graphs;
}

TSharedRef<FJsonObject> N2CMcpEditorContext::GraphJson(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (Graph)
	{
		Json->SetStringField(TEXT("name"), Graph->GetName());
		// The format list-blueprint-graphs returns and the graph tools take
		Json->SetStringField(TEXT("graphGuid"), Graph->GraphGuid.ToString());
		Json->SetStringField(TEXT("type"), GraphType(Blueprint, Graph));
	}
	return Json;
}

FString N2CMcpEditorContext::CompileStatus(EBlueprintStatus Status)
{
	switch (Status)
	{
	case BS_UpToDate: return TEXT("upToDate");
	case BS_UpToDateWithWarnings: return TEXT("upToDateWithWarnings");
	case BS_Dirty: return TEXT("dirty");
	case BS_Error: return TEXT("error");
	case BS_BeingCreated: return TEXT("beingCreated");
	default: return TEXT("unknown");
	}
}

FString N2CMcpEditorContext::BlueprintTypeString(EBlueprintType Type)
{
	switch (Type)
	{
	case BPTYPE_Normal: return TEXT("Normal");
	case BPTYPE_Const: return TEXT("Const");
	case BPTYPE_MacroLibrary: return TEXT("MacroLibrary");
	case BPTYPE_Interface: return TEXT("Interface");
	case BPTYPE_LevelScript: return TEXT("LevelScript");
	case BPTYPE_FunctionLibrary: return TEXT("FunctionLibrary");
	default: return TEXT("Unknown");
	}
}

bool N2CMcpEditorContext::IsPackageDirty(const UBlueprint& Blueprint)
{
	const UPackage* Package = Blueprint.GetPackage();
	return Package && Package->IsDirty();
}

TSharedPtr<FJsonObject> N2CMcpEditorContext::EditorJson(const TSharedPtr<FBlueprintEditor>& Editor, bool bFocused)
{
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	if (!Blueprint)
	{
		return nullptr;
	}
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	Json->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
	Json->SetStringField(TEXT("blueprintClass"), Blueprint->GetClass()->GetName());
	Json->SetStringField(TEXT("editorName"), Editor->GetToolkitFName().ToString());
	if (const UEdGraph* Focused = Editor->GetFocusedGraph())
	{
		Json->SetObjectField(TEXT("focusedGraph"), GraphJson(Blueprint, Focused));
	}
	Json->SetBoolField(TEXT("isFocused"), bFocused);
	Json->SetBoolField(TEXT("packageDirty"), IsPackageDirty(*Blueprint));
	Json->SetStringField(TEXT("compileStatus"), CompileStatus(Blueprint->Status));
	return Json;
}

TArray<TSharedPtr<FJsonValue>> N2CMcpEditorContext::SelectedNodesJson(const TSharedPtr<FBlueprintEditor>& Editor)
{
	TArray<TSharedPtr<FJsonValue>> Nodes;
	const UEdGraph* Graph = Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
	if (!Graph)
	{
		return Nodes;
	}
	const FGraphPanelSelectionSet Selection = Editor->GetSelectedNodes();
	// Graph order, so the same selection always reads the same
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || !Selection.Contains(const_cast<UEdGraphNode*>(Node)))
		{
			continue;
		}
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		// The format N2C JSON's ids.guid uses
		Entry->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Entry->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Entry->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		Nodes.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Nodes;
}

TSharedRef<FJsonObject> N2CMcpEditorContext::Build()
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	const TSharedPtr<FBlueprintEditor> Focused = FN2CBlueprintEditorTracker::Get().GetFocusedEditor();
	UBlueprint* FocusedBlueprint = Focused.IsValid() ? Focused->GetBlueprintObj() : nullptr;

	if (FocusedBlueprint)
	{
		const TSharedRef<FJsonObject> BlueprintJson = MakeShared<FJsonObject>();
		BlueprintJson->SetStringField(TEXT("name"), FocusedBlueprint->GetName());
		BlueprintJson->SetStringField(TEXT("path"), FocusedBlueprint->GetPathName());
		BlueprintJson->SetStringField(TEXT("blueprintClass"), FocusedBlueprint->GetClass()->GetName());
		Json->SetObjectField(TEXT("focusedBlueprint"), BlueprintJson);
		if (const UEdGraph* Graph = Focused->GetFocusedGraph())
		{
			Json->SetObjectField(TEXT("focusedGraph"), GraphJson(FocusedBlueprint, Graph));
		}
	}

	const TArray<TSharedPtr<FJsonValue>> Selected = FocusedBlueprint ? SelectedNodesJson(Focused) : TArray<TSharedPtr<FJsonValue>>();
	Json->SetArrayField(TEXT("selectedNodes"), Selected);
	Json->SetNumberField(TEXT("selectedNodeCount"), Selected.Num());

	TArray<TSharedPtr<FJsonValue>> Editors;
	for (const TSharedPtr<FBlueprintEditor>& Editor : FN2CBlueprintEditorTracker::GetOpenBlueprintEditors())
	{
		if (const TSharedPtr<FJsonObject> Entry = EditorJson(Editor, Editor == Focused))
		{
			Editors.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	Json->SetArrayField(TEXT("openEditors"), Editors);
	Json->SetNumberField(TEXT("openEditorCount"), Editors.Num());

	const TSharedRef<FJsonObject> Pie = MakeShared<FJsonObject>();
	const UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;
	Pie->SetBoolField(TEXT("active"), PlayWorld != nullptr);
	Pie->SetBoolField(TEXT("simulating"), PlayWorld != nullptr && GEditor->bIsSimulatingInEditor);
	Pie->SetBoolField(TEXT("paused"), PlayWorld != nullptr && PlayWorld->bDebugPauseExecution);
	Json->SetObjectField(TEXT("playInEditor"), Pie);

	Json->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
	return Json;
}
