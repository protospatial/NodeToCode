// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Runtime/Launch/Resources/Version.h"

class FJsonObject;
class FJsonValue;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node_EditablePinBase;

/**
 * Whether a Blueprint editor opened in a headless test (-nullrhi) can be closed again. UE 5.6 and 5.7 cannot: closing
 * the editor's window, or a later Slate layout pass while it is open, asks the null native window for its restored
 * size, a fatal error in GenericWindow.cpp. Cases that need a focused Blueprint editor (the Python bridge, the
 * variable node tools, get-focused-blueprint) are compiled only where this is 1 and say so with a skip case
 * elsewhere; tools that take blueprintPath and graphGuid need no editor and run on every engine.
 */
#define N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

/**
 * The MCP tool spec harness (card 0147): one way for the NodeToCode.MCP.* specs to build a scratch Blueprint, call a
 * tool by name the way an MCP client's tools/call does, read its result and check the graph invariants every editing
 * tool must keep. See README.md in this folder.
 */
namespace N2CTest
{
	// ---- Calling tools ----

	/**
	 * Runs a tool by name through FN2CMcpToolManager::ExecuteTool, as tools/call does: the tool's game-thread logic runs
	 * in one FN2CMcpToolCallScope (one compile per call, diagnostics in the result) and a tool that declares
	 * bReturnsUntrustedContent has its result marked. A tool compiled into the module but not in the active tool set
	 * is registered for the call and unregistered after it. An error result naming the tool when there is none.
	 */
	FMcpToolCallResult RunTool(const FString& Name, const TSharedPtr<FJsonObject>& Args);

	/**
	 * Runs a tool by name with IN2CMcpTool::Execute: the same call scope and compile, without the untrusted-content
	 * mark. For specs that compare a result's text byte for byte with what the tool builds.
	 */
	FMcpToolCallResult RunToolUnmarked(const FString& Name, const TSharedPtr<FJsonObject>& Args);

	/** One tool call around Edit, as FN2CMcpToolBase::ExecuteOnGameThread runs a tool: returns the compiles it ran */
	template <typename FEdit>
	TArray<FN2CCompileDiagnostics> InToolCall(FEdit&& Edit)
	{
		FN2CMcpToolCallScope CallScope;
		Edit();
		return CallScope.Flush();
	}

	// ---- Arguments ----

	/** {blueprintPath, graphGuid} of a graph: the explicit target every Graph and Functions tool takes */
	TSharedPtr<FJsonObject> TargetArgs(const UBlueprint* Blueprint, const UEdGraph* Graph);

	/** {nodeGuid, pinGuid, pinName} of a pin, as connect-pins takes it */
	TSharedPtr<FJsonValue> PinRef(const UEdGraphPin* Pin);

	/** connect-pins arguments linking each Key to its Value, with Base's fields (a target, options) when given */
	TSharedPtr<FJsonObject> ConnectArgs(const TArray<TPair<UEdGraphPin*, UEdGraphPin*>>& Links, const TSharedPtr<FJsonObject>& Base = nullptr);

	// ---- Reading results ----

	/** The first text item of a result as a JSON object, or null */
	TSharedPtr<FJsonObject> ResultJson(const FMcpToolCallResult& Result);

	/** The "target" object of a result (a field of its JSON, or of an extra text item of an error), or null */
	TSharedPtr<FJsonObject> ResultTarget(const FMcpToolCallResult& Result);

	/** The "compile" object a tool call adds to its result (in its JSON, or as its own item), or null */
	TSharedPtr<FJsonObject> ResultCompile(const FMcpToolCallResult& Result);

	/** The result's JSON starts with the untrusted-content marker (FN2CMcpUntrustedContent::MarkerField) */
	bool IsMarkedUntrusted(const FMcpToolCallResult& Result);

	/**
	 * Checks that a tool result acted on Blueprint (and Graph, when given): its target names that Blueprint's path and
	 * that graph's GUID, and its compile object, when it has one, names the Blueprint. Returns false on a mismatch.
	 */
	bool ExpectResultTargets(FAutomationTestBase& Test, const FString& What, const FMcpToolCallResult& Result,
		const UBlueprint* Blueprint, const UEdGraph* Graph = nullptr);

	/** Checks that the result is not an error and its compile object reports success and no errors */
	bool ExpectToolCompiled(FAutomationTestBase& Test, const FString& What, const FMcpToolCallResult& Result);

	// ---- Graph invariants ----

	/**
	 * Every graph of the Blueprint, as text: each node (GUID, class, name) in UEdGraph::Nodes order, each of its pins
	 * (ID, name, direction, type, default value, orphaned and hidden flags, and the pins it links to), the
	 * UserDefinedPins of editable nodes, and the Blueprint's variables, timelines and component templates. Two equal
	 * snapshots mean the same nodes, pins, links and signatures.
	 */
	FString Snapshot(UBlueprint* Blueprint);

	/** The first line where two snapshots differ, both sides, or empty when they are equal */
	FString FirstSnapshotDifference(const FString& Expected, const FString& Actual);

	/** Every link in the Blueprint as "<node GUID>.<pin> -> <node GUID>.<pin>", from the output side */
	TSet<FString> AllLinks(UBlueprint* Blueprint);

	/**
	 * Every one-sided or dangling link in the Blueprint: a pin in another pin's LinkedTo that does not link back, is
	 * null, or belongs to a node that is no longer in its graph. Empty when every link is symmetric.
	 */
	TArray<FString> AsymmetricLinks(UBlueprint* Blueprint);

	/** Checks AsymmetricLinks is empty, reporting the first few */
	bool ExpectSymmetricLinks(FAutomationTestBase& Test, const FString& What, UBlueprint* Blueprint);

	/**
	 * Undo, then Redo, of the last transaction: after Undo the snapshot must be Before exactly, after Redo the snapshot
	 * taken when this was called. Fails (with the first differing line) otherwise.
	 */
	bool ExpectUndoRedoExact(FAutomationTestBase& Test, const FString& What, UBlueprint* Blueprint, const FString& Before);

	/** Compiles the Blueprint (FN2CMcpBlueprintUtils::Compile) and checks for zero errors, logging the messages */
	bool ExpectCompilesClean(FAutomationTestBase& Test, const FString& What, UBlueprint* Blueprint);

	/** The node of the Blueprint with this GUID (any format), or null */
	UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FString& GuidText);

	/** The editable node has a user-defined pin of this name */
	bool HasUserPin(const UK2Node_EditablePinBase* Node, const FName Name);

	/** Exec in and out pins of a node, or null */
	UEdGraphPin* ExecIn(UEdGraphNode* Node);
	UEdGraphPin* ExecOut(UEdGraphNode* Node);

	/** Length of the editor's undo queue (0 without a transaction buffer) */
	int32 UndoQueueLength();

	/** Full compiles of one Blueprint (UBlueprint::OnCompiled, never a skeleton-only regeneration), while alive */
	struct FCompileCounter
	{
		UBlueprint* Blueprint = nullptr;
		FDelegateHandle Handle;
		int32 Count = 0;

		explicit FCompileCounter(UBlueprint* InBlueprint);
		~FCompileCounter();

		FCompileCounter(const FCompileCounter&) = delete;
		FCompileCounter& operator=(const FCompileCounter&) = delete;
	};

	// ---- Scratch Blueprint ----

	/**
	 * An FTestBlueprint for tool specs: a transient Actor Blueprint in its own package under MountRoot (never saved or
	 * registered), built with FTestBlueprint's helpers. With bTrackDirty (the default) its package's RF_Transient flag is
	 * cleared, because MarkPackageDirty skips transient objects and the specs check that edits dirty the package as
	 * they would a saved Blueprint. On destruction it closes the Blueprint editor it opened, cleans the package and
	 * gives it RF_Transient back, and resets the editor's undo buffer (which holds the Blueprint's objects) so the
	 * garbage collector can take it.
	 */
	class FToolSpecBlueprint : public FTestBlueprint
	{
	public:
		explicit FToolSpecBlueprint(const FString& BlueprintName, const FString& MountRoot = TEXT("/Temp"), bool bInTrackDirty = true);
		~FToolSpecBlueprint();

		/** The Blueprint's object path, the blueprintPath tools take */
		FString Path() const;
		bool IsDirty() const;
		/** Compiles the Blueprint and cleans its package: a baseline before the edit under test */
		void Settle();

		/**
		 * Opens the Blueprint in its editor and focuses Graph, for the cases that act on the focused graph. Only where
		 * N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR: elsewhere it adds the reason as info and returns false, and the
		 * case skips. Leaves the package clean when bClean.
		 */
		bool OpenInEditor(FAutomationTestBase& Test, UEdGraph* Graph, bool bClean = true);
		bool IsEditorOpen() const { return bEditorOpen; }
		/** Closes the editor OpenInEditor opened (the destructor does too) */
		void CloseEditor();

	private:
		bool bTrackDirty = true;
		bool bEditorOpen = false;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
