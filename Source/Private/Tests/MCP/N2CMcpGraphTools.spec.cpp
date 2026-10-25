// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetSystemLibrary.h"

namespace N2CMcpGraphToolsSpec
{
	/** The connect-pins result's failed entries */
	TArray<TSharedPtr<FJsonValue>> FailedOf(const FMcpToolCallResult& Result)
	{
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		const TArray<TSharedPtr<FJsonValue>>* Failed = nullptr;
		return Json.IsValid() && Json->TryGetArrayField(TEXT("failed"), Failed) ? *Failed : TArray<TSharedPtr<FJsonValue>>();
	}

	/** A connect-pins entry with its pins named by pinName only: no pinGuid (a malformed entry) */
	TSharedPtr<FJsonValue> EntryWithoutPinGuid(const UEdGraphPin* From, const UEdGraphPin* To)
	{
		auto Side = [](const UEdGraphPin* Pin)
		{
			const TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
			Ref->SetStringField(TEXT("nodeGuid"), Pin->GetOwningNode()->NodeGuid.ToString());
			Ref->SetStringField(TEXT("pinId"), Pin->PinId.ToString());
			return Ref;
		};
		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetObjectField(TEXT("from"), Side(From));
		Entry->SetObjectField(TEXT("to"), Side(To));
		return MakeShared<FJsonValueObject>(Entry);
	}
}

/**
 * The Graph tools through the tool manager, as an MCP client calls them (card 0147, review imp-mcp-tools-graph-functions-9),
 * on a Blueprint built in code and named by blueprintPath plus graphGuid, so every case runs on every engine with no
 * Blueprint editor open. Each edit leaves the package dirty, one undo step, symmetric links and a Blueprint that
 * compiles with no errors, and Undo then Redo restore every node, pin and link exactly (N2CTest::Snapshot). Already
 * covered elsewhere and not repeated: the stable action IDs and search -> add for many node kinds
 * (NodeToCode.MCP.NodeActionIds), the read-only contract of a context-insensitive search by path and of the focused
 * search (NodeActionIds, NodeToCode.Robustness.SearchIsReadOnly), the target echo and READ_ONLY_GRAPH
 * (NodeToCode.MCP.GraphTarget), one compile per call and its diagnostics (NodeToCode.MCP.CompileOnce/CompileScope),
 * and create-comment-node (NodeToCode.MCP.SpawnedNodes).
 */
BEGIN_DEFINE_SPEC(FN2CMcpGraphToolsSpec, "NodeToCode.MCP.Graph", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UEdGraph* Function = nullptr;
	UK2Node_Event* BeginPlay = nullptr;
	UK2Node_CustomEvent* Go = nullptr;
	UK2Node_IfThenElse* Branch = nullptr;
	UK2Node_CallFunction* PrintA = nullptr;
	UK2Node_CallFunction* PrintB = nullptr;

	/**
	 * Event graph: BeginPlay (enabled) -> PrintB; Go -> Branch, Branch.True -> PrintA -> PrintB; Branch.False unlinked.
	 * A function N2CGraphSpecFunction with its entry. Compiled, package clean.
	 */
	bool Build(const TCHAR* Name);
	/** search-blueprint-nodes in the event graph for Print String's action ID and node name */
	bool FindPrintString(FString& OutActionId, FString& OutNodeName);
END_DEFINE_SPEC(FN2CMcpGraphToolsSpec)

bool FN2CMcpGraphToolsSpec::Build(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	Graph = Test->EventGraph();
	Function = Test->AddFunction(FName(TEXT("N2CGraphSpecFunction")));
	BeginPlay = Test->FindEvent(Graph, FName(TEXT("ReceiveBeginPlay")));
	Go = Test->AddCustomEvent(Graph, FName(TEXT("N2CGraphSpecGo")));
	Branch = Test->AddBranch(Graph);
	PrintA = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	PrintB = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	if (!TestTrue(TEXT("nodes"), Function && BeginPlay && Go && Branch && PrintA && PrintB))
	{
		return false;
	}
	BeginPlay->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction*/ true);
	Test->Link(BeginPlay, UEdGraphSchema_K2::PN_Then, PrintB, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Go, UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Branch, UEdGraphSchema_K2::PN_Then, PrintA, UEdGraphSchema_K2::PN_Execute);
	Test->Link(PrintA, UEdGraphSchema_K2::PN_Then, PrintB, UEdGraphSchema_K2::PN_Execute);
	if (!TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
	{
		return false;
	}
	Test->Settle();
	return N2CTest::ExpectCompilesClean(*this, TEXT("baseline"), Blueprint) && (Test->Settle(), true);
}

bool FN2CMcpGraphToolsSpec::FindPrintString(FString& OutActionId, FString& OutNodeName)
{
	const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
	Args->SetStringField(TEXT("searchTerm"), TEXT("print string"));
	const FMcpToolCallResult Search = N2CTest::RunTool(TEXT("search-blueprint-nodes"), Args);
	const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Search);
	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (Json.IsValid() && Json->TryGetArrayField(TEXT("nodes"), Nodes))
	{
		for (const TSharedPtr<FJsonValue>& Node : *Nodes)
		{
			const TSharedPtr<FJsonObject> Spawn = Node->AsObject()->GetObjectField(TEXT("spawnMetadata"));
			if (Spawn->GetStringField(TEXT("actionIdentifier")).Contains(TEXT("KismetSystemLibrary:PrintString")))
			{
				OutActionId = Spawn->GetStringField(TEXT("actionIdentifier"));
				OutNodeName = Spawn->GetStringField(TEXT("nodeName"));
				return true;
			}
		}
	}
	return TestTrue(TEXT("Print String found: ") + N2CTest::ResultText(Search).Left(300), false);
}

void FN2CMcpGraphToolsSpec::Define()
{
	using namespace N2CMcpGraphToolsSpec;

	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
		Graph = Function = nullptr;
		BeginPlay = nullptr;
		Go = nullptr;
		Branch = nullptr;
		PrintA = PrintB = nullptr;
	});

	Describe(TEXT("search-blueprint-nodes"), [this]()
	{
		It(TEXT("context-sensitive on an explicit event graph and function graph leaves the Blueprint, its undo buffer and dirty flag exactly as they were"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecSearch")))
			{
				return;
			}
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			N2CTest::FCompileCounter Compiles(Blueprint);
			for (UEdGraph* Target : { Graph, Function })
			{
				for (const TCHAR* Term : { TEXT("begin play"), TEXT("timeline"), TEXT("add static mesh component"), TEXT("print string") })
				{
					const FString What = FString::Printf(TEXT("'%s' in %s"), Term, *Target->GetName());
					const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Target);
					Args->SetStringField(TEXT("searchTerm"), Term);
					const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("search-blueprint-nodes"), Args);
					TestFalse(What + TEXT(" succeeds: ") + N2CTest::ResultText(Result).Left(200), Result.bIsError);
					N2CTest::ExpectResultTargets(*this, What, Result, Blueprint, Target);
					TestTrue(What + TEXT(": marked untrusted"), N2CTest::IsMarkedUntrusted(Result));
					const FString Difference = N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint));
					TestTrue(What + TEXT(": nodes, pins, links, timelines and components unchanged ") + Difference, Difference.IsEmpty());
					TestEqual(What + TEXT(": undo buffer unchanged"), N2CTest::UndoQueueLength(), QueueBefore);
					TestFalse(What + TEXT(": package not dirty"), Test->IsDirty());
				}
			}
			TestEqual(TEXT("no compile"), Compiles.Count, 0);
		});
	});

	Describe(TEXT("add-bp-node-to-active-graph"), [this]()
	{
		It(TEXT("adds one node in one undo step, dirty, compiling, and Undo then Redo restore the graph exactly"), [this]()
		{
			FString ActionId;
			FString NodeName;
			if (!Build(TEXT("BP_N2CGraphSpecAdd")) || !FindPrintString(ActionId, NodeName))
			{
				return;
			}
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const int32 NodesBefore = Graph->Nodes.Num();
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
			Args->SetStringField(TEXT("nodeName"), NodeName);
			Args->SetStringField(TEXT("actionIdentifier"), ActionId);
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("add-bp-node-to-active-graph"), Args);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("add"), Result))
			{
				return;
			}
			N2CTest::ExpectResultTargets(*this, TEXT("add"), Result, Blueprint, Graph);
			TestEqual(TEXT("one node more"), Graph->Nodes.Num(), NodesBefore + 1);
			FString Guid;
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			TestTrue(TEXT("nodeGuid names the new node"), Json.IsValid() && Json->TryGetStringField(TEXT("nodeGuid"), Guid)
				&& N2CTest::FindNodeByGuid(Blueprint, Guid) == Graph->Nodes.Last());
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after add"), Blueprint);
			N2CTest::ExpectUndoRedoExact(*this, TEXT("add"), Blueprint, Before);
			N2CTest::ExpectCompilesClean(*this, TEXT("after redo"), Blueprint);
		});
	});

	Describe(TEXT("connect-pins"), [this]()
	{
		It(TEXT("keeps exec fan-in: Branch True and False both into one Print String, in one undo step that Undo then Redo restore exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecFanIn")))
			{
				return;
			}
			UEdGraphPin* True = Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* False = Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output);
			UEdGraphPin* Execute = N2CTest::ExecIn(PrintA);
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("connect-pins"),
				N2CTest::ConnectArgs({ { False, Execute } }, N2CTest::TargetArgs(Blueprint, Graph)));
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("connect False"), Result))
			{
				return;
			}
			N2CTest::ExpectResultTargets(*this, TEXT("connect False"), Result, Blueprint, Graph);
			TestEqual(TEXT("no failed connection"), FailedOf(Result).Num(), 0);
			TestTrue(TEXT("False -> Print linked"), False->LinkedTo.Contains(Execute));
			TestTrue(TEXT("True -> Print kept (fan-in)"), True->LinkedTo.Contains(Execute));
			TestEqual(TEXT("Print's execute has both links"), Execute->LinkedTo.Num(), 2);
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after connect"), Blueprint);
			N2CTest::ExpectUndoRedoExact(*this, TEXT("connect"), Blueprint, Before);
			TestTrue(TEXT("after redo: both links"), True->LinkedTo.Contains(Execute) && False->LinkedTo.Contains(Execute));
			N2CTest::ExpectCompilesClean(*this, TEXT("after redo"), Blueprint);
		});

		It(TEXT("refuses to replace an exec output's link without breakExistingLinks and replaces it with it"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecReplace")))
			{
				return;
			}
			UEdGraphPin* GoThen = N2CTest::ExecOut(Go);
			UEdGraphPin* BranchIn = N2CTest::ExecIn(Branch);
			UEdGraphPin* PrintBIn = N2CTest::ExecIn(PrintB);
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();

			const FMcpToolCallResult Refused = N2CTest::RunTool(TEXT("connect-pins"),
				N2CTest::ConnectArgs({ { GoThen, PrintBIn } }, N2CTest::TargetArgs(Blueprint, Graph)));
			const TArray<TSharedPtr<FJsonValue>> Failed = FailedOf(Refused);
			TestFalse(TEXT("not an error result"), Refused.bIsError);
			TestTrue(TEXT("WOULD_BREAK_EXISTING_LINK: ") + N2CTest::ResultText(Refused).Left(300), Failed.Num() == 1
				&& Failed[0]->AsObject()->GetStringField(TEXT("errorCode")) == TEXT("WOULD_BREAK_EXISTING_LINK"));
			const FString Difference = N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint));
			TestTrue(TEXT("the graph is unchanged ") + Difference, Difference.IsEmpty());
			TestTrue(TEXT("Go -> Branch kept"), GoThen->LinkedTo.Contains(BranchIn));

			const TSharedPtr<FJsonObject> Args = N2CTest::ConnectArgs({ { GoThen, PrintBIn } }, N2CTest::TargetArgs(Blueprint, Graph));
			const TSharedPtr<FJsonObject> Options = MakeShared<FJsonObject>();
			Options->SetBoolField(TEXT("breakExistingLinks"), true);
			Args->SetObjectField(TEXT("options"), Options);
			const FMcpToolCallResult Replaced = N2CTest::RunTool(TEXT("connect-pins"), Args);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("replace"), Replaced))
			{
				return;
			}
			TestEqual(TEXT("replace: no failed connection"), FailedOf(Replaced).Num(), 0);
			TestTrue(TEXT("Go -> PrintB now"), GoThen->LinkedTo.Num() == 1 && GoThen->LinkedTo.Contains(PrintBIn));
			TestFalse(TEXT("Go -> Branch gone from both sides"), BranchIn->LinkedTo.Contains(GoThen));
			TestTrue(TEXT("PrintB keeps its other links (fan-in)"), PrintBIn->LinkedTo.Contains(N2CTest::ExecOut(PrintA)));
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after replace"), Blueprint);
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			TestTrue(TEXT("the replace is recorded"), N2CTest::UndoQueueLength() > QueueBefore);
			N2CTest::ExpectUndoRedoExact(*this, TEXT("replace"), Blueprint, Before);
		});

		// Known defect mcp-tools-graph-functions-25 (card 0276): an entry without pinGuid is skipped without a trace,
		// not counted in summary.totalRequested and not listed in failed. Enable (xIt -> It) with the fix.
		xIt(TEXT("reports a malformed connection entry in failed and counts it"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecMalformed")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Args = N2CTest::ConnectArgs(
				{ { Branch->FindPin(UEdGraphSchema_K2::PN_Else, EGPD_Output), N2CTest::ExecIn(PrintB) } }, N2CTest::TargetArgs(Blueprint, Graph));
			TArray<TSharedPtr<FJsonValue>> Connections = Args->GetArrayField(TEXT("connections"));
			Connections.Add(EntryWithoutPinGuid(N2CTest::ExecOut(PrintB), N2CTest::ExecIn(PrintA)));
			Args->SetArrayField(TEXT("connections"), Connections);
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("connect-pins"), Args);
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			const TSharedPtr<FJsonObject>* Summary = nullptr;
			if (TestTrue(TEXT("summary: ") + N2CTest::ResultText(Result).Left(300), Json.IsValid() && Json->TryGetObjectField(TEXT("summary"), Summary)))
			{
				TestEqual(TEXT("totalRequested counts both entries"), static_cast<int32>((*Summary)->GetNumberField(TEXT("totalRequested"))), 2);
			}
			TestEqual(TEXT("the malformed entry is in failed"), FailedOf(Result).Num(), 1);
		});
	});

	Describe(TEXT("delete-blueprint-node"), [this]()
	{
		It(TEXT("deletes a wired node with its links on both sides, in one undo step that Undo then Redo restore exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecDelete")))
			{
				return;
			}
			UEdGraphPin* True = Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* PrintBIn = N2CTest::ExecIn(PrintB);
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
			Args->SetArrayField(TEXT("nodeGuids"), { MakeShared<FJsonValueString>(PrintA->NodeGuid.ToString()) });
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("delete-blueprint-node"), Args);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("delete"), Result))
			{
				return;
			}
			N2CTest::ExpectResultTargets(*this, TEXT("delete"), Result, Blueprint, Graph);
			TestFalse(TEXT("the node is gone"), Graph->Nodes.Contains(PrintA));
			TestEqual(TEXT("Branch.True has no link left"), True->LinkedTo.Num(), 0);
			TestEqual(TEXT("PrintB keeps only BeginPlay's link"), PrintBIn->LinkedTo.Num(), 1);
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after delete"), Blueprint);
			TestTrue(TEXT("package dirty"), Test->IsDirty());
			TestEqual(TEXT("one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
			N2CTest::ExpectUndoRedoExact(*this, TEXT("delete"), Blueprint, Before);
			N2CTest::ExpectCompilesClean(*this, TEXT("after redo"), Blueprint);
		});

		It(TEXT("refuses an unknown GUID and a function entry without force, deleting nothing"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecDeleteRefused")))
			{
				return;
			}
			const FString Before = N2CTest::Snapshot(Blueprint);
			const int32 QueueBefore = N2CTest::UndoQueueLength();

			const TSharedPtr<FJsonObject> Unknown = N2CTest::TargetArgs(Blueprint, Graph);
			Unknown->SetArrayField(TEXT("nodeGuids"), { MakeShared<FJsonValueString>(PrintA->NodeGuid.ToString()),
				MakeShared<FJsonValueString>(FGuid::NewGuid().ToString()) });
			const FMcpToolCallResult UnknownResult = N2CTest::RunTool(TEXT("delete-blueprint-node"), Unknown);
			TestTrue(TEXT("unknown GUID refused: ") + N2CTest::ResultText(UnknownResult).Left(200),
				UnknownResult.bIsError && N2CTest::ResultText(UnknownResult).Contains(TEXT("not found in graph")));

			const TSharedPtr<FJsonObject> Entry = N2CTest::TargetArgs(Blueprint, Function);
			Entry->SetArrayField(TEXT("nodeGuids"), { MakeShared<FJsonValueString>(Test->FunctionEntry(Function)->NodeGuid.ToString()) });
			const FMcpToolCallResult EntryResult = N2CTest::RunTool(TEXT("delete-blueprint-node"), Entry);
			TestTrue(TEXT("function entry refused: ") + N2CTest::ResultText(EntryResult).Left(200),
				EntryResult.bIsError && N2CTest::ResultText(EntryResult).Contains(TEXT("protected")));

			const FString Difference = N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint));
			TestTrue(TEXT("nothing deleted ") + Difference, Difference.IsEmpty());
			TestEqual(TEXT("no undo step"), N2CTest::UndoQueueLength(), QueueBefore);
			TestFalse(TEXT("package clean"), Test->IsDirty());
		});

		// Known defect mcp-tools-graph-functions-12 (card 0279): preserveConnections compares two output pins and so
		// never reconnects anything. Enable (xIt -> It) with the fix.
		xIt(TEXT("with preserveConnections keeps the exec chain through a deleted Print String"), [this]()
		{
			if (!Build(TEXT("BP_N2CGraphSpecPreserve")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
			Args->SetArrayField(TEXT("nodeGuids"), { MakeShared<FJsonValueString>(PrintA->NodeGuid.ToString()) });
			Args->SetBoolField(TEXT("preserveConnections"), true);
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("delete-blueprint-node"), Args);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("delete"), Result))
			{
				return;
			}
			TestTrue(TEXT("Branch.True -> PrintB"), Branch->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output)->LinkedTo.Contains(N2CTest::ExecIn(PrintB)));
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after delete"), Blueprint);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
