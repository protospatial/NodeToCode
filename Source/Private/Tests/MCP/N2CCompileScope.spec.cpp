// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace N2CCompileScopeSpec
{
	/** Counts the full compiles of one Blueprint (UBlueprint::OnCompiled fires once per full compile, never for a
	 *  skeleton-only regeneration), from construction until destruction */
	struct FCompileCounter
	{
		UBlueprint* Blueprint = nullptr;
		FDelegateHandle Handle;
		int32 Count = 0;

		explicit FCompileCounter(UBlueprint* InBlueprint)
			: Blueprint(InBlueprint)
		{
			Handle = Blueprint->OnCompiled().AddLambda([this](UBlueprint*) { ++Count; });
		}
		~FCompileCounter()
		{
			Blueprint->OnCompiled().Remove(Handle);
		}
	};

	/** A custom event wired to a chain of Count Print String calls; returns the nodes in order (event first) */
	TArray<UEdGraphNode*> MakePrintChain(N2CTest::FTestBlueprint& Test, int32 Count)
	{
		TArray<UEdGraphNode*> Nodes;
		UEdGraph* Graph = Test.EventGraph();
		Nodes.Add(Test.AddCustomEvent(Graph, FName(TEXT("N2CCompileScopeEvent"))));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Nodes.Add(Test.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
		}
		return Nodes;
	}

	UEdGraphPin* ExecOut(UEdGraphNode* Node)
	{
		return Node ? Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output) : nullptr;
	}

	UEdGraphPin* ExecIn(UEdGraphNode* Node)
	{
		return Node ? Node->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr;
	}
}

/**
 * One compile per tool call and real compiler diagnostics (card 0151, review mcp-tools-graph-functions-16/-17,
 * python-bridge-26, mcp-tools-variables-21, imp-mcp-tools-graph-functions-5/-6, imp-mcp-tools-variables-5,
 * imp-mcp-tools-bp-structure-4). FN2CMcpToolCallScope collects the Blueprints a call marks modified and compiles each
 * once when the call ends; FN2CMcpBlueprintUtils::Compile reports the compiler's messages with the node each names;
 * action-database refreshes are coalesced into one deferred RefreshAssetActions pass and read-only calls queue none.
 * The tool-level checks (connect-pins with 20 connections, timings) are in NodeToCode.MCP.CompileOnce.
 */
BEGIN_DEFINE_SPEC(FN2CCompileScopeSpec, "NodeToCode.MCP.CompileScope", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CCompileScopeSpec)

void FN2CCompileScopeSpec::Define()
{
	using namespace N2CCompileScopeSpec;

	It(TEXT("compiles a Blueprint once per call, after 20 edits in their own transactions, and leaves it dirty"), [this]()
	{
		N2CTest::FTestBlueprint Test(TEXT("BP_N2CCompileScope"), TEXT("/Game"));
		const TArray<UEdGraphNode*> Nodes = MakePrintChain(Test, 20);
		UBlueprint* Blueprint = Test.GetBlueprint();
		// FTestBlueprint packages are transient, and MarkPackageDirty skips transient objects: clear the flag so the
		// dirty checks see what a saved Blueprint would, and put it back after
		Test.GetPackage()->ClearFlags(RF_Transient);
		ON_SCOPE_EXIT { Test.GetPackage()->SetDirtyFlag(false); Test.GetPackage()->SetFlags(RF_Transient); };
		if (!TestTrue(TEXT("test graph built: ") + FString::Join(Test.Errors(), TEXT("; ")), Test.Errors().Num() == 0))
		{
			return;
		}
		Test.GetPackage()->SetDirtyFlag(false);

		FCompileCounter Compiles(Blueprint);
		const int32 UtilCompilesBefore = FN2CMcpBlueprintUtils::GetCompileCount();
		TArray<FN2CCompileDiagnostics> Reported;
		{
			FN2CMcpToolCallScope CallScope;
			TestTrue(TEXT("the scope is outermost"), CallScope.IsOutermost());
			TestTrue(TEXT("a scope is active"), FN2CMcpToolCallScope::IsActive());
			for (int32 Index = 0; Index + 1 < Nodes.Num(); ++Index)
			{
				const FScopedTransaction Transaction(NSLOCTEXT("NodeToCode", "N2CCompileScopeTest", "Connect (test)"));
				TestTrue(FString::Printf(TEXT("link %d made"), Index),
					GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(ExecOut(Nodes[Index]), ExecIn(Nodes[Index + 1])));
				FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);
			}
			TestEqual(TEXT("no compile while the call is open"), Compiles.Count, 0);
			TestTrue(TEXT("dirty before the compile"), Test.GetPackage()->IsDirty());

			Reported = CallScope.Flush();
			TestEqual(TEXT("one full compile when the call ends"), Compiles.Count, 1);
		}
		TestFalse(TEXT("no scope after the call"), FN2CMcpToolCallScope::IsActive());
		TestEqual(TEXT("still one compile after the scope closed"), Compiles.Count, 1);
		TestEqual(TEXT("Compile ran once"), FN2CMcpBlueprintUtils::GetCompileCount() - UtilCompilesBefore, 1);
		TestEqual(TEXT("one diagnostics entry"), Reported.Num(), 1);
		if (Reported.Num() == 1)
		{
			TestTrue(TEXT("the chain compiles cleanly"), Reported[0].bSuccess);
			TestEqual(TEXT("no errors"), Reported[0].ErrorCount, 0);
			TestEqual(TEXT("reports this Blueprint"), Reported[0].BlueprintPath, Blueprint->GetPathName());
		}
		TestTrue(TEXT("package dirty after the call"), Test.GetPackage()->IsDirty());
		TestEqual(TEXT("Blueprint up to date"), static_cast<int32>(Blueprint->Status), static_cast<int32>(BS_UpToDate));
	});

	It(TEXT("leaves the compile to the outermost call when calls nest, and compiles on an early return"), [this]()
	{
		N2CTest::FTestBlueprint Test(TEXT("BP_N2CCompileNested"), TEXT("/Game"));
		const TArray<UEdGraphNode*> Nodes = MakePrintChain(Test, 2);
		UBlueprint* Blueprint = Test.GetBlueprint();
		ON_SCOPE_EXIT { Test.GetPackage()->SetDirtyFlag(false); };
		FCompileCounter Compiles(Blueprint);
		{
			FN2CMcpToolCallScope Outer;
			{
				FN2CMcpToolCallScope Inner;
				TestFalse(TEXT("the inner scope is not outermost"), Inner.IsOutermost());
				GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(ExecOut(Nodes[0]), ExecIn(Nodes[1]));
				FN2CMcpBlueprintUtils::MarkModified(Blueprint, false);
				TestEqual(TEXT("the inner flush compiles nothing"), Inner.Flush().Num(), 0);
			}
			TestEqual(TEXT("no compile when the inner call ends"), Compiles.Count, 0);
			GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(ExecOut(Nodes[1]), ExecIn(Nodes[2]));
			FN2CMcpBlueprintUtils::MarkModified(Blueprint, false);
			TestEqual(TEXT("the outer flush compiles the Blueprint once"), Outer.Flush().Num(), 1);
		}
		TestEqual(TEXT("one compile for both calls"), Compiles.Count, 1);

		// A call that returns without flushing (an error path) still compiles what it changed, once
		{
			FN2CMcpToolCallScope CallScope;
			Nodes[2]->NodePosX += 10;
			FN2CMcpBlueprintUtils::MarkModified(Blueprint, false);
		}
		TestEqual(TEXT("the unflushed call compiled when it closed"), Compiles.Count, 2);

		// Outside any call, MarkModified compiles at once (the editor's Python console calling the bridge directly)
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, false);
		TestEqual(TEXT("compiled at once outside a call"), Compiles.Count, 3);
	});

	It(TEXT("reports the compiler's error with the node, graph and pin it names"), [this]()
	{
		N2CTest::FTestBlueprint Test(TEXT("BP_N2CCompileBroken"), TEXT("/Game"));
		UEdGraph* Graph = Test.EventGraph();
		UEdGraphNode* Event = Test.AddCustomEvent(Graph, FName(TEXT("N2CBrokenEvent")));
		// An Actor Blueprint is not a StaticMeshComponent, so Set Static Mesh needs its Target wired: an error
		UK2Node_CallFunction* SetMesh = Test.AddCall(Graph, UStaticMeshComponent::StaticClass(), GET_FUNCTION_NAME_CHECKED(UStaticMeshComponent, SetStaticMesh));
		ON_SCOPE_EXIT { Test.GetPackage()->SetDirtyFlag(false); };
		if (!TestNotNull(TEXT("Set Static Mesh node"), SetMesh) || !TestNotNull(TEXT("event"), Event))
		{
			return;
		}

		TArray<FN2CCompileDiagnostics> Reported;
		{
			FN2CMcpToolCallScope CallScope;
			TestTrue(TEXT("exec linked"), GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(ExecOut(Event), ExecIn(SetMesh)));
			FN2CMcpBlueprintUtils::MarkModified(Test.GetBlueprint(), false);
			Reported = CallScope.Flush();
		}
		if (!TestEqual(TEXT("one compile reported"), Reported.Num(), 1))
		{
			return;
		}
		const FN2CCompileDiagnostics& Diagnostics = Reported[0];
		TestFalse(TEXT("the compile failed"), Diagnostics.bSuccess);
		TestTrue(TEXT("at least one error"), Diagnostics.ErrorCount >= 1);
		TestEqual(TEXT("status Error"), Diagnostics.Status, FString(TEXT("Error")));

		const FN2CCompilerMessage* Error = Diagnostics.Messages.FindByPredicate([](const FN2CCompilerMessage& Message)
		{
			return Message.Severity == TEXT("Error") && !Message.NodeGuid.IsEmpty();
		});
		if (!TestNotNull(TEXT("an error message naming a node"), Error))
		{
			for (const FN2CCompilerMessage& Message : Diagnostics.Messages)
			{
				AddInfo(FString::Printf(TEXT("%s: %s"), *Message.Severity, *Message.Message));
			}
			return;
		}
		TestEqual(TEXT("the error names the Set Static Mesh node"), Error->NodeGuid, SetMesh->NodeGuid.ToString());
		TestEqual(TEXT("in the event graph"), Error->GraphName, Graph->GetName());
		TestFalse(TEXT("with its text"), Error->Message.IsEmpty());
		AddInfo(FString::Printf(TEXT("error: %s (node %s, pin '%s')"), *Error->Message, *Error->NodeTitle, *Error->PinName));

		// The JSON a tool result carries
		const TSharedRef<FJsonObject> Json = Diagnostics.ToJson();
		TestEqual(TEXT("json errorCount"), static_cast<int32>(Json->GetNumberField(TEXT("errorCount"))), Diagnostics.ErrorCount);
		TestFalse(TEXT("json success"), Json->GetBoolField(TEXT("success")));
		bool bJsonNamesNode = false;
		for (const TSharedPtr<FJsonValue>& Value : Json->GetArrayField(TEXT("messages")))
		{
			FString Guid;
			if (Value->AsObject()->TryGetStringField(TEXT("nodeGuid"), Guid) && Guid == SetMesh->NodeGuid.ToString())
			{
				bJsonNamesNode = Value->AsObject()->GetStringField(TEXT("graph")) == Graph->GetName();
			}
		}
		TestTrue(TEXT("json message has nodeGuid and graph"), bJsonNamesNode);
	});

	It(TEXT("adds the diagnostics to a JSON tool result, and as their own item to any other result"), [this]()
	{
		FN2CCompileDiagnostics Diagnostics;
		Diagnostics.bCompiled = true;
		Diagnostics.BlueprintName = TEXT("BP_X");
		Diagnostics.ErrorCount = 1;
		Diagnostics.Status = TEXT("Error");
		FN2CCompilerMessage Message(TEXT("Error"), TEXT("broken"));
		Message.NodeGuid = TEXT("ABC");
		Message.GraphName = TEXT("EventGraph");
		Diagnostics.Messages.Add(Message);

		FMcpToolCallResult JsonResult = FMcpToolCallResult::CreateTextResult(TEXT("{\"success\":true,\"nodeGuid\":\"N1\"}"));
		FN2CMcpBlueprintUtils::AttachCompileDiagnostics(JsonResult, { Diagnostics });
		TestEqual(TEXT("still one content item"), JsonResult.Content.Num(), 1);
		const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(N2CTest::ResultText(JsonResult));
		const TSharedPtr<FJsonObject>* Compile = nullptr;
		if (TestTrue(TEXT("compile field added"), Parsed.IsValid() && Parsed->TryGetObjectField(TEXT("compile"), Compile)))
		{
			TestEqual(TEXT("errorCount"), static_cast<int32>((*Compile)->GetNumberField(TEXT("errorCount"))), 1);
			TestEqual(TEXT("the result's own fields stay"), Parsed->GetStringField(TEXT("nodeGuid")), FString(TEXT("N1")));
		}

		FMcpToolCallResult ErrorResult = FMcpToolCallResult::CreateErrorResult(TEXT("Failed to delete nodes"));
		FN2CMcpBlueprintUtils::AttachCompileDiagnostics(ErrorResult, { Diagnostics });
		TestEqual(TEXT("an error result gets a second item"), ErrorResult.Content.Num(), 2);
		TestTrue(TEXT("the error stays an error"), ErrorResult.bIsError);

		FMcpToolCallResult Untouched = FMcpToolCallResult::CreateTextResult(TEXT("{}"));
		FN2CMcpBlueprintUtils::AttachCompileDiagnostics(Untouched, {});
		TestEqual(TEXT("nothing added when nothing compiled"), N2CTest::ResultText(Untouched), FString(TEXT("{}")));
	});

	It(TEXT("queues one deferred action-database refresh for any number of structural edits, per Blueprint once"), [this]()
	{
		N2CTest::FTestBlueprint A(TEXT("BP_N2CRefreshA"), TEXT("/Game"));
		N2CTest::FTestBlueprint* B = A.AddSiblingBlueprint(TEXT("BP_N2CRefreshB"));
		ON_SCOPE_EXIT { A.GetPackage()->SetDirtyFlag(false); };
		if (!TestNotNull(TEXT("second Blueprint"), B))
		{
			return;
		}
		// Start from nothing queued
		FN2CMcpBlueprintUtils::FlushActionDatabaseRefreshForTest();
		const int32 RequestsBefore = FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRequestCount();
		const int32 RunsBefore = FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRunCount();

		{
			FN2CMcpToolCallScope CallScope;
			for (int32 Round = 0; Round < 3; ++Round)
			{
				A.AddMemberVariable(*FString::Printf(TEXT("N2CVarA%d"), Round), N2CTest::FTestBlueprint::IntPinType());
				FN2CMcpBlueprintUtils::MarkModified(A.GetBlueprint(), /*bStructural*/ true);
				B->AddMemberVariable(*FString::Printf(TEXT("N2CVarB%d"), Round), N2CTest::FTestBlueprint::IntPinType());
				FN2CMcpBlueprintUtils::MarkModified(B->GetBlueprint(), /*bStructural*/ true);
			}
			TestEqual(TEXT("two Blueprints compiled once each"), CallScope.Flush().Num(), 2);
		}
		TestEqual(TEXT("six refresh requests"), FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRequestCount() - RequestsBefore, 6);
		TestTrue(TEXT("one refresh queued"), FN2CMcpBlueprintUtils::IsActionDatabaseRefreshPending());
		const TArray<TWeakObjectPtr<UBlueprint>> Pending = FN2CMcpBlueprintUtils::GetPendingActionDatabaseRefreshes();
		TestEqual(TEXT("for the two Blueprints, once each"), Pending.Num(), 2);
		TestTrue(TEXT("A queued"), Pending.Contains(A.GetBlueprint()));
		TestTrue(TEXT("B queued"), Pending.Contains(B->GetBlueprint()));

		TestTrue(TEXT("the queued refresh runs"), FN2CMcpBlueprintUtils::FlushActionDatabaseRefreshForTest());
		TestEqual(TEXT("one refresh ran"), FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRunCount() - RunsBefore, 1);
		TestFalse(TEXT("nothing queued after it"), FN2CMcpBlueprintUtils::IsActionDatabaseRefreshPending());

		// Node edits (non-structural) change no actions: no refresh
		{
			FN2CMcpToolCallScope CallScope;
			FN2CMcpBlueprintUtils::MarkModified(A.GetBlueprint(), /*bStructural*/ false);
		}
		TestFalse(TEXT("a node edit queues no refresh"), FN2CMcpBlueprintUtils::IsActionDatabaseRefreshPending());
	});

	It(TEXT("runs no compile and queues no refresh for a search, through the tool or the Python bridge"), [this]()
	{
		// A graph to search for: without one a search is refused (card 0434)
		N2CTest::FTestBlueprint Test(TEXT("BP_N2CCompileSearch"), TEXT("/Game"));
		FN2CMcpBlueprintUtils::FlushActionDatabaseRefreshForTest();
		const int32 RequestsBefore = FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRequestCount();
		const int32 CompilesBefore = FN2CMcpBlueprintUtils::GetCompileCount();

		const TSharedPtr<IN2CMcpTool> Search = N2CTest::FindRegisteredTool(TEXT("search-blueprint-nodes"));
		if (!TestTrue(TEXT("search-blueprint-nodes registered"), Search.IsValid()))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("searchTerm"), TEXT("print string"));
		Args->SetBoolField(TEXT("contextSensitive"), false);
		Args->SetStringField(TEXT("blueprintPath"), Test.GetBlueprint()->GetPathName());
		Args->SetNumberField(TEXT("maxResults"), 5);
		const FMcpToolCallResult Result = Search->Execute(Args);
		TestFalse(TEXT("the search succeeds: ") + N2CTest::ResultText(Result).Left(200), Result.bIsError);
		TestFalse(TEXT("no compile field on a search"), N2CTest::ResultText(Result).Contains(TEXT("\"compile\"")));
		TestTrue(TEXT("the search finds Print String"), N2CTest::ResultText(Result).Contains(TEXT("KismetSystemLibrary:PrintString")));

		// The bridge searches the focused graph; this test has none, so it refuses (an answer, not an empty list)
		const FString BridgeResult = UN2CPythonBridge::SearchBlueprintNodes(TEXT("print string"), /*bContextSensitive*/ false, 5);
		const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(BridgeResult);
		TestTrue(TEXT("the bridge answers: ") + BridgeResult.Left(200), Bridge.IsValid() && Bridge->HasField(TEXT("success")));

		TestEqual(TEXT("no refresh requested"), FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRequestCount(), RequestsBefore);
		TestFalse(TEXT("no refresh queued"), FN2CMcpBlueprintUtils::IsActionDatabaseRefreshPending());
		TestEqual(TEXT("no compile"), FN2CMcpBlueprintUtils::GetCompileCount(), CompilesBefore);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
