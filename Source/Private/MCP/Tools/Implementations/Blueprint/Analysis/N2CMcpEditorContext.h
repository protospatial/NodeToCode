// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"

class FBlueprintEditor;
class FJsonObject;
class FJsonValue;
class UEdGraph;

/**
 * What the user is looking at in the editor (card 0275): get-editor-context and get-open-blueprint-editors share it.
 * The focused Blueprint editor is FN2CBlueprintEditorTracker's (card 0150); every open Blueprint editor is the
 * tracker's GetOpenBlueprintEditors, so Widget and Animation Blueprint editors count as well as plain ones. Game
 * thread only.
 */
namespace N2CMcpEditorContext
{
	/**
	 * A graph's type: Delegate (DelegateSignatureGraphs), Interface (an ImplementedInterfaces graph), Collapsed (a
	 * collapsed graph's own page), otherwise its schema's UEdGraphSchema::GetGraphType: Event (an event graph page,
	 * UbergraphPages), Function, Macro, Animation (an Anim Blueprint's AnimGraph and its layers), StateMachine. Graph
	 * when it has no schema or the schema reports another type.
	 */
	FString GraphType(const UBlueprint* Blueprint, const UEdGraph* Graph);

	/**
	 * The graphs a Blueprint editor lists in My Blueprint, each once: the event graph pages (UbergraphPages), the
	 * functions (FunctionGraphs), the macros (MacroGraphs), the interface implementations (ImplementedInterfaces[].Graphs)
	 * and the delegate signatures (DelegateSignatureGraphs). Collapsed graphs are not listed (they live inside these).
	 */
	TArray<UEdGraph*> TopLevelGraphs(const UBlueprint& Blueprint);

	/** {name, graphGuid, type} */
	TSharedRef<FJsonObject> GraphJson(const UBlueprint* Blueprint, const UEdGraph* Graph);

	/** The Blueprint's compile status: upToDate, upToDateWithWarnings, dirty (changed since the last compile), error, beingCreated or unknown */
	FString CompileStatus(EBlueprintStatus Status);

	/** Normal, Const, MacroLibrary, Interface, LevelScript, FunctionLibrary or Unknown */
	FString BlueprintTypeString(EBlueprintType Type);

	/** True when the Blueprint's package has unsaved changes (what the editor's save star shows), not its compile status */
	bool IsPackageDirty(const UBlueprint& Blueprint);

	/**
	 * One open Blueprint editor: {blueprintName, assetPath, blueprintClass (UBlueprint, UWidgetBlueprint,
	 * UAnimBlueprint, ...), editorName (the toolkit, e.g. WidgetBlueprintEditor), focusedGraph {name, graphGuid, type}
	 * (the graph this editor shows, absent when none), isFocused, packageDirty, compileStatus}
	 */
	TSharedPtr<FJsonObject> EditorJson(const TSharedPtr<FBlueprintEditor>& Editor, bool bFocused);

	/** The focused editor's selected nodes in its focused graph: [{nodeGuid, title, nodeClass}], in graph order */
	TArray<TSharedPtr<FJsonValue>> SelectedNodesJson(const TSharedPtr<FBlueprintEditor>& Editor);

	/**
	 * get-editor-context's result: focusedBlueprint {name, path, blueprintClass} and focusedGraph {name, graphGuid,
	 * type} (both absent when no Blueprint editor is open), selectedNodes and selectedNodeCount, openEditors (EditorJson
	 * for each) and openEditorCount, playInEditor {active, simulating, paused} and engineVersion (5.8.0).
	 */
	TSharedRef<FJsonObject> Build();
}
