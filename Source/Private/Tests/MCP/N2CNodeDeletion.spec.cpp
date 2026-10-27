// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Knot.h"
#include "K2Node_Tunnel.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CNodeDeletionSpec
{
	TSharedPtr<FJsonObject> DeleteArgs(UBlueprint* Blueprint, UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes, bool bPreserve)
	{
		const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
		TArray<TSharedPtr<FJsonValue>> Guids;
		for (const UEdGraphNode* Node : Nodes)
		{
			Guids.Add(MakeShared<FJsonValueString>(Node->NodeGuid.ToString()));
		}
		Args->SetArrayField(TEXT("nodeGuids"), Guids);
		if (bPreserve)
		{
			Args->SetBoolField(TEXT("preserveConnections"), true);
		}
		return Args;
	}

	/** deletedNodes[Index].preservedConnections of a delete result (the tool's JSON, or the bridge's data), or empty */
	TArray<TSharedPtr<FJsonValue>> Bridged(const TSharedPtr<FJsonObject>& Json, int32 Index = 0)
	{
		const TArray<TSharedPtr<FJsonValue>>* Deleted = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Links = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(TEXT("deletedNodes"), Deleted) && Deleted->IsValidIndex(Index)
			&& (*Deleted)[Index]->AsObject()->TryGetArrayField(TEXT("preservedConnections"), Links))
		{
			return *Links;
		}
		return {};
	}

	bool IsLink(const TSharedPtr<FJsonValue>& Link, const UEdGraphPin* From, const UEdGraphPin* To)
	{
		const TSharedPtr<FJsonObject> Object = Link.IsValid() ? Link->AsObject() : nullptr;
		return Object.IsValid()
			&& Object->GetStringField(TEXT("fromNode")) == From->GetOwningNode()->NodeGuid.ToString()
			&& Object->GetStringField(TEXT("fromPin")) == From->PinName.ToString()
			&& Object->GetStringField(TEXT("toNode")) == To->GetOwningNode()->NodeGuid.ToString()
			&& Object->GetStringField(TEXT("toPin")) == To->PinName.ToString()
			&& Object->GetStringField(TEXT("type")) == From->PinType.PinCategory.ToString();
	}

	bool HasGraphNamed(UBlueprint* Blueprint, const FString& Name)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		return Graphs.ContainsByPredicate([&Name](const UEdGraph* Graph) { return Graph && Graph->GetName() == Name; });
	}
}

/**
 * Node deletion as the editor's Delete does it, shared by delete-blueprint-node and the Python bridge's DeleteNodes
 * (card 0279; review mcp-tools-graph-functions-12, -13, python-bridge-22). Protection is the node's own
 * CanUserDeleteNode: a collapsed graph's entry and exit tunnels are refused and the collapsed-graph node itself is
 * not. Nodes go through FBlueprintEditorUtils::RemoveNode (breakpoints and pin watches removed). preserveConnections
 * bridges the exec line through a node with one exec input and output and a data line through a reroute knot,
 * listing each link it made. The tool cases name the graph by blueprintPath and graphGuid and run on every engine; the
 * bridge acts on the focused graph and runs where a headless Blueprint editor can be closed (5.8).
 */
BEGIN_DEFINE_SPEC(FN2CNodeDeletionSpec, "NodeToCode.MCP.NodeDeletion", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UK2Node_CustomEvent* Go = nullptr;
	UK2Node_CallFunction* PrintA = nullptr;
	UK2Node_CallFunction* PrintB = nullptr;
	UK2Node_CallFunction* PrintC = nullptr;

	/** Event graph: Go -> PrintA -> PrintB -> PrintC. Compiled, package clean. */
	bool Build(const TCHAR* Name);
END_DEFINE_SPEC(FN2CNodeDeletionSpec)

bool FN2CNodeDeletionSpec::Build(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	Graph = Test->EventGraph();
	Go = Test->AddCustomEvent(Graph, FName(TEXT("N2CDeleteSpecGo")));
	PrintA = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	PrintB = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	PrintC = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	if (!TestTrue(TEXT("nodes"), Go && PrintA && PrintB && PrintC))
	{
		return false;
	}
	Test->Link(Go, UEdGraphSchema_K2::PN_Then, PrintA, UEdGraphSchema_K2::PN_Execute);
	Test->Link(PrintA, UEdGraphSchema_K2::PN_Then, PrintB, UEdGraphSchema_K2::PN_Execute);
	Test->Link(PrintB, UEdGraphSchema_K2::PN_Then, PrintC, UEdGraphSchema_K2::PN_Execute);
	if (!TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
	{
		return false;
	}
	Test->Settle();
	return N2CTest::ExpectCompilesClean(*this, TEXT("baseline"), Blueprint) && (Test->Settle(), true);
}

void FN2CNodeDeletionSpec::Define()
{
	AfterEach([this]()
	{
		if (Blueprint)
		{
			FKismetDebugUtilities::ClearBreakpoints(Blueprint);
		}
		Test.Reset();
		Blueprint = nullptr;
		Graph = nullptr;
		Go = nullptr;
		PrintA = PrintB = PrintC = nullptr;
	});

	Describe(TEXT("delete-blueprint-node"), [this]()
	{
		It(TEXT("with preserveConnections bridges the exec chain through a mid-chain Print String, removes its breakpoint, and Undo restores it"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteSpecPreserve")))
			{
				return;
			}
			FKismetDebugUtilities::CreateBreakpoint(Blueprint, PrintB);
			if (!TestNotNull(TEXT("breakpoint set"), FKismetDebugUtilities::FindBreakpointForNode(PrintB, Blueprint)))
			{
				return;
			}
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("delete-blueprint-node"),
				N2CNodeDeletionSpec::DeleteArgs(Blueprint, Graph, { PrintB }, /*bPreserve*/ true));
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("delete"), Result))
			{
				return;
			}
			TestFalse(TEXT("PrintB is gone"), Graph->Nodes.Contains(PrintB));
			TestTrue(TEXT("PrintA.then -> PrintC.execute"), N2CTest::ExecOut(PrintA)->LinkedTo.Contains(N2CTest::ExecIn(PrintC)));
			TestEqual(TEXT("PrintC has one exec source"), N2CTest::ExecIn(PrintC)->LinkedTo.Num(), 1);
			const TArray<TSharedPtr<FJsonValue>> Links = N2CNodeDeletionSpec::Bridged(N2CTest::ResultJson(Result));
			TestTrue(FString::Printf(TEXT("one preserved connection, PrintA.then -> PrintC.execute (exec): %s"), *N2CTest::ResultText(Result).Left(600)),
				Links.Num() == 1 && N2CNodeDeletionSpec::IsLink(Links[0], N2CTest::ExecOut(PrintA), N2CTest::ExecIn(PrintC)));
			// BlueprintHasBreakpoints stays true for an emptied list; ask for the node's own breakpoint
			TestNull(TEXT("the breakpoint went with the node"), FKismetDebugUtilities::FindBreakpointForNode(PrintB, Blueprint));
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after delete"), Blueprint);
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
			N2CTest::ExpectUndoRedoExact(*this, TEXT("delete with preserveConnections"), Blueprint, Before);
			N2CTest::ExpectCompilesClean(*this, TEXT("after redo"), Blueprint);
		});

		It(TEXT("with preserveConnections bridges a data line through a reroute knot"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteSpecKnot")))
			{
				return;
			}
			UK2Node_CallFunction* AddA = Test->AddCall(Graph, UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
			UK2Node_CallFunction* AddB = Test->AddCall(Graph, UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
			UK2Node_Knot* Knot = Test->AddKnot(Graph);
			if (!TestTrue(TEXT("nodes"), AddA && AddB && Knot))
			{
				return;
			}
			UEdGraphPin* Source = AddA->GetReturnValuePin();
			UEdGraphPin* Target = AddB->FindPin(TEXT("A"), EGPD_Input);
			Test->Link(Source, Knot->GetInputPin());
			Test->Link(Knot->GetOutputPin(), Target);
			if (!TestTrue(TEXT("knot wired: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
			{
				return;
			}
			Test->Settle();
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("delete-blueprint-node"),
				N2CNodeDeletionSpec::DeleteArgs(Blueprint, Graph, { Knot }, /*bPreserve*/ true));
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("delete knot"), Result))
			{
				return;
			}
			TestTrue(TEXT("AddA.ReturnValue -> AddB.A"), Source->LinkedTo.Contains(Target));
			const TArray<TSharedPtr<FJsonValue>> Links = N2CNodeDeletionSpec::Bridged(N2CTest::ResultJson(Result));
			TestTrue(TEXT("one preserved int connection: ") + N2CTest::ResultText(Result).Left(600),
				Links.Num() == 1 && N2CNodeDeletionSpec::IsLink(Links[0], Source, Target));
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after delete knot"), Blueprint);
		});

		It(TEXT("refuses the entry and exit tunnels of a collapsed graph and deletes the collapsed-graph node without force"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteSpecCollapsed")))
			{
				return;
			}
			const FName CollapsedName(TEXT("N2CDeleteSpecCollapsed"));
			UK2Node_Composite* Composite = Test->AddCollapsedGraph(Graph, CollapsedName);
			UEdGraph* Collapsed = Composite ? Composite->BoundGraph.Get() : nullptr;
			UK2Node_Tunnel* Entry = Test->TunnelEntry(Collapsed);
			UK2Node_Tunnel* Exit = Test->TunnelExit(Collapsed);
			if (!TestTrue(TEXT("collapsed graph with its tunnels: ") + FString::Join(Test->Errors(), TEXT("; ")), Composite && Collapsed && Entry && Exit))
			{
				return;
			}
			Test->Settle();
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			for (UK2Node_Tunnel* Tunnel : { Entry, Exit })
			{
				const FMcpToolCallResult Refused = N2CTest::RunTool(TEXT("delete-blueprint-node"),
					N2CNodeDeletionSpec::DeleteArgs(Blueprint, Collapsed, { Tunnel }, false));
				TestTrue(TEXT("tunnel refused as protected: ") + N2CTest::ResultText(Refused).Left(300),
					Refused.bIsError && N2CTest::ResultText(Refused).Contains(TEXT("protected")));
			}
			const FString Difference = N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint));
			TestTrue(TEXT("nothing deleted ") + Difference, Difference.IsEmpty());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), QueueBefore);

			const FMcpToolCallResult Deleted = N2CTest::RunTool(TEXT("delete-blueprint-node"),
				N2CNodeDeletionSpec::DeleteArgs(Blueprint, Graph, { Composite }, false));
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("delete the collapsed-graph node"), Deleted))
			{
				return;
			}
			TestFalse(TEXT("the collapsed-graph node is gone"), Graph->Nodes.Contains(Composite));
			TestFalse(TEXT("its graph is gone"), N2CNodeDeletionSpec::HasGraphNamed(Blueprint, CollapsedName.ToString()));
			N2CTest::ExpectCompilesClean(*this, TEXT("after deleting the collapsed graph"), Blueprint);
		});
	});

	Describe(TEXT("the Python bridge's DeleteNodes"), [this]()
	{
		It(TEXT("bridges the exec chain with preserve_connections and lists a GUID it did not delete under skipped"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteSpecBridge")) || !Test->OpenInEditor(*this, Graph))
			{
				return;
			}
			const FString Unknown = FGuid::NewGuid().ToString();
			const FString Json = UN2CPythonBridge::DeleteNodes(
				FString::Printf(TEXT("[\"%s\",\"%s\"]"), *PrintB->NodeGuid.ToString(), *Unknown), /*bPreserveConnections*/ true, /*bForce*/ false);
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (!TestTrue(TEXT("DeleteNodes succeeds: ") + Json.Left(600), FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid()
				&& Parsed->GetBoolField(TEXT("success")) && Parsed->TryGetObjectField(TEXT("data"), Data)))
			{
				return;
			}
			TestFalse(TEXT("PrintB is gone"), Graph->Nodes.Contains(PrintB));
			TestTrue(TEXT("PrintA.then -> PrintC.execute"), N2CTest::ExecOut(PrintA)->LinkedTo.Contains(N2CTest::ExecIn(PrintC)));
			const TArray<TSharedPtr<FJsonValue>> Links = N2CNodeDeletionSpec::Bridged(*Data);
			TestTrue(TEXT("one preserved connection: ") + Json.Left(600),
				Links.Num() == 1 && N2CNodeDeletionSpec::IsLink(Links[0], N2CTest::ExecOut(PrintA), N2CTest::ExecIn(PrintC)));
			const TArray<TSharedPtr<FJsonValue>>* Skipped = nullptr;
			TestTrue(TEXT("the unknown GUID is skipped: ") + Json.Left(600), (*Data)->TryGetArrayField(TEXT("skipped"), Skipped)
				&& Skipped->Num() == 1 && (*Skipped)[0]->AsObject()->GetStringField(TEXT("guid")) == Unknown);
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after bridge delete"), Blueprint);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
